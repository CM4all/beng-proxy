// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Error.hxx"
#include "Public.hxx"
#include "http/Body.hxx"
#include "http/Status.hxx"
#include "http/WaitTracker.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketProtocolError.hxx"
#include "net/SocketAddress.hxx"
#include "event/CoarseTimerEvent.hxx"
#include "istream/Sink.hxx"
#include "pool/UniquePtr.hxx"
#include "util/Cancellable.hxx"
#include "util/DestructObserver.hxx"
#include "util/Exception.hxx"

#ifdef HAVE_URING
#include "io/uring/Operation.hxx"
#include <optional>
#endif

#include <cassert>
#include <string_view>

class SlicePool;
enum class HttpMethod : uint_least8_t;
struct HttpServerRequest;
class HttpHeaders;
class GrowingBuffer;
namespace Net::Log { enum class ContentType : uint8_t; }

struct HttpServerConnection final
	: BufferedSocketHandler, IstreamSink, DestructAnchor {

	/**
	 * The timeout of an idle connection (READ_START) up until
	 * request headers are received.
	 */
	static constexpr Event::Duration idle_timeout = std::chrono::seconds{30};

	/**
	 * The timeout for reading all request headers.  It is enabled
	 * as soon as the first byte of the request line is received.
	 */
	static constexpr Event::Duration request_header_timeout = std::chrono::seconds{10};

	/**
	 * The timeout for reading more request data (READ_BODY).
	 */
	static constexpr Event::Duration read_timeout = std::chrono::seconds{30};

	/**
	 * The timeout for writing more response data (READ_BODY,
	 * READ_END).
	 */
	static constexpr Event::Duration write_timeout = std::chrono::minutes{2};

	enum class BucketResult {
		/**
		 * No bucket data is available.  Fall back to
		 * Istream::Read().
		 */
		FALLBACK,

		/**
		 * No data is available right now.  Wait for the
		 * IstreamHandler::OnIstreamReady() call.
		 */
		LATER,

		/**
		 * Some data has been transferred, more data will be
		 * available later.
		 */
		MORE,

		/**
		 * Writing to our socket blocks.
		 */
		BLOCKING,

		/**
		 * The #Istream is now empty.
		 */
		DEPLETED,

		/**
		 * This object has been destroyed inside the function.
		 */
		DESTROYED,
	};

	struct RequestBodyReader : HttpBodyReader {
		HttpServerConnection &connection;

		RequestBodyReader(struct pool &_pool,
				  HttpServerConnection &_connection) noexcept
			:HttpBodyReader(_pool),
			 connection(_connection) {}

		/* virtual methods from class Istream */

		off_t _GetAvailable(bool partial) noexcept override;
		void _Read() noexcept override;
		void _ConsumeDirect(std::size_t nbytes) noexcept override;
		void _Close() noexcept override;
	};

	struct pool *const pool;

	SlicePool &request_slice_pool;

	/* I/O */
	UniquePoolPtr<FilteredSocket> socket;

	/**
	 * Track the total time for idle periods plus receiving all
	 * headers from the client.  Unlike the #FilteredSocket read
	 * timeout, it is not refreshed after receiving some header data.
	 */
	CoarseTimerEvent idle_timer;

	/**
	 * A timer which fires when reading the request body times
	 * out.  It is refreshed each time request body data is
	 * received, and is disabled as long as the #Istream handler
	 * blocks.
	 */
	CoarseTimerEvent read_timer;

	/* handler */
	HttpServerConnectionHandler *handler;

	HttpServerRequestHandler &request_handler;

	/* info */

	const SocketAddress local_address, remote_address;

	const char *const local_host_and_port;
	const char *const remote_host;

	WaitTracker wait_tracker;

	static constexpr WaitTracker::mask_t WAIT_RECEIVE_REQUEST = 1 << 0;
	static constexpr WaitTracker::mask_t WAIT_SEND_RESPONSE = 1 << 1;

	/* request */
	struct Request {
		/**
		 * The response body if #error_status is set.
		 */
		const char *error_message;

		HttpServerRequest *request = nullptr;

		CancellablePointer cancel_ptr;

		uint64_t bytes_received = 0;

		/**
		 * If this is set, the this library rejects the
		 * request with this HTTP status instead of letting
		 * the caller handle it.  The field #error_message
		 * specifies the response body.
		 */
		HttpStatus error_status{};

		enum : uint_least8_t {
			/** there is no request (yet); waiting for the request
			    line */
			START,

			/** parsing request headers; waiting for empty line */
			HEADERS,

			/** reading the request body */
			BODY,

			/** the request has been consumed, and we are going to send the response */
			END
		} read_state = START;

#ifndef NDEBUG
		enum class BodyState : uint_least8_t {
			START,
			NONE,
			EMPTY,
			READING,
			CLOSED,
		} body_state = BodyState::START;
#endif

		/**
		 * Ignore all further headers (and don't pay attention
		 * to malformed header lines); in this case,
		 * #error_status is usually set and the whole request
		 * will fail.
		 */
		bool ignore_headers = false;

		/**
		 * This flag is true if we are currently calling the HTTP
		 * request handler.  During this period,
		 * http_server_request_stream_read() does nothing, to prevent
		 * recursion.
		 */
		bool in_handler;

		/**
		 * Did the client send an "Upgrade" header?
		 */
		bool upgrade;

		/** did the client send an "Expect: 100-continue" header? */
		bool expect_100_continue;

		void Reset() noexcept {
			error_status = {};
			read_state = START;
#ifndef NDEBUG
			body_state = BodyState::START;
#endif
			ignore_headers = false;
			bytes_received = 0;
		}

		void SetError(HttpStatus _status, const char *_msg) noexcept {
			if (error_status != HttpStatus::UNDEFINED)
				/* use only the first error */
				return;

			error_status = _status;
			error_message = _msg;
		}

		bool ShouldEnableReadTimeout() const noexcept {
			/* "Upgrade" requests have no request body
			   timeout, because an arbitrary protocol may
			   be on the wire now */
			/* no timeout as long as the client is waiting
			   for "100 Continue" */
			return !upgrade && !expect_100_continue;
		}
	} request;

	/** the request body reader; this variable is only valid if
	    read_state==READ_BODY */
	RequestBodyReader *request_body_reader;

	/** the response; this struct is only valid if
	    read_state==READ_BODY||read_state==READ_END */
	struct Response {
		off_t length;

		uint64_t bytes_sent = 0;

		HttpStatus status;

		Net::Log::ContentType content_type = {};

		bool want_write;

		/**
		 * Are we currently waiting for all output buffers to be
		 * drained, before we can close the socket?
		 *
		 * @see BufferedSocketHandler::drained
		 * @see http_server_socket_drained()
		 */
		bool pending_drained = false;
	} response;

#ifdef HAVE_URING
	class UringSend;

	void StartUringSend(Uring::Queue &queue, GrowingBuffer &&src);
	void CancelUringSend() noexcept;
	void OnUringSendDone() noexcept;
	void OnUringSendError(int error) noexcept;

	UringSend *uring_send = nullptr;

	class UringSplice final : Uring::Operation {
		HttpServerConnection &parent;
		Uring::Queue &queue;

		std::size_t max_length;
		bool then_eof;

	public:
		UringSplice(HttpServerConnection &_parent,
			    Uring::Queue &_queue) noexcept
			:parent(_parent), queue(_queue) {}

		~UringSplice() noexcept;

		using Uring::Operation::IsUringPending;

		void Start(FileDescriptor src, off_t offset, std::size_t _max_length, bool _then_eof);

	private:
		void OnUringCompletion(int res) noexcept override;
	};

	void OnUringSpliceCompletion(int res, std::size_t max_length, bool then_eof) noexcept;

	std::optional<UringSplice> uring_splice;
	bool uring_splice_then_eof;
#endif

	bool HaveUringSend() const noexcept {
#ifdef HAVE_URING
		return uring_send != nullptr;
#else
		return false;
#endif
	}

	enum http_server_score score = HTTP_SERVER_NEW;

	bool date_header;

	/* connection settings */
	bool keep_alive;

	HttpServerConnection(struct pool &_pool,
			     UniquePoolPtr<FilteredSocket> &&_socket,
			     SocketAddress _local_address,
			     SocketAddress _remote_address,
			     bool _date_header,
			     SlicePool &_request_slice_pool,
			     HttpServerConnectionHandler &_handler,
			     HttpServerRequestHandler &_request_handler) noexcept;
	~HttpServerConnection() noexcept;

	void Delete() noexcept;

	auto &GetEventLoop() const noexcept {
		return idle_timer.GetEventLoop();
	}

	[[gnu::pure]]
	bool IsValid() const noexcept {
		return socket->IsValid() && socket->IsConnected();
	}

	void IdleTimeoutCallback() noexcept;
	void OnReadTimeout() noexcept;

	void Log(HttpServerRequest &r) noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	bool ParseRequestLine(std::string_view line) noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	bool HeadersFinished() noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	bool HandleLine(std::string_view line) noexcept;

	BufferedResult FeedHeaders(std::string_view b) noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	bool SubmitRequest() noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	BufferedResult Feed(std::span<const std::byte> b) noexcept;

	/**
	 * Send data from the input buffer to the request body istream
	 * handler.
	 */
	BufferedResult FeedRequestBody(std::span<const std::byte> src) noexcept;

	/**
	 * Attempt a "direct" transfer of the request body.  Caller must
	 * hold an additional pool reference.
	 */
	DirectResult TryRequestBodyDirect(SocketDescriptor fd, FdType fd_type) noexcept;

	/**
	 * The request body is not needed anymore.  This method discards
	 * it.  If it is not possible to discard it properly, this method
	 * disables keep-alive so the connection will be closed as soon as
	 * the response has been sent, forcibly disposing the request
	 * body.
	 */
	void DiscardRequestBody() noexcept;

	void ReadRequestBody() noexcept;

	/**
	 * @return false if the connection has been closed
	 */
	bool MaybeSend100Continue() noexcept;

	void SetResponseIstream(UnusedIstreamPtr r) noexcept;

	/**
	 * To be called after the response istream has seen end-of-file,
	 * and has been destroyed.
	 *
	 * @return false if the connection has been closed
	 */
	bool ResponseIstreamFinished() noexcept;

	void SubmitResponse(HttpStatus status,
			    HttpHeaders &&headers,
			    UnusedIstreamPtr body);

	void ScheduleReadTimeoutTimer() noexcept {
		assert(request.read_state == Request::BODY);

		if (request.ShouldEnableReadTimeout()) {
			read_timer.Schedule(read_timeout);
			wait_tracker.Set(GetEventLoop(), WAIT_RECEIVE_REQUEST);
		}
	}

	void CancelReadTimeoutTimer() noexcept {
		read_timer.Cancel();
		wait_tracker.Clear(GetEventLoop(), WAIT_RECEIVE_REQUEST);
	}

	void DeferWrite() noexcept {
		response.want_write = true;
		socket->DeferWrite();
	}

	void ScheduleWrite() noexcept {
		response.want_write = true;
		socket->ScheduleWrite();
		wait_tracker.Set(GetEventLoop(), WAIT_SEND_RESPONSE);
	}

	/**
	 * @return false if the connection has been closed
	 */
	bool TryWrite() noexcept;

	/**
	 * Throws on error.
	 */
	BucketResult TryWriteBuckets2();

	BucketResult TryWriteBuckets() noexcept;

	HttpServerRequest *NewRequest(HttpMethod method,
				      std::string_view uri) noexcept;

	void CloseRequest() noexcept;

	/**
	 * The last response on this connection is finished, and it should
	 * be closed.
	 */
	void Done() noexcept;

	/**
	 * The peer has closed the socket.
	 */
	void Cancel() noexcept;

	/**
	 * A fatal error has occurred, and the connection should be closed
	 * immediately, without sending any further information to the
	 * client.  This invokes
	 * HttpServerConnectionHandler::HttpConnectionError(), but not
	 * HttpServerConnectionHandler::HttpConnectionClosed().
	 */
	void Error(std::exception_ptr e) noexcept;

	void Error(const char *msg) noexcept;

	void SocketErrorErrno(const char *msg) noexcept;

	template<typename T>
	void SocketError(T &&t) noexcept {
		try {
			ThrowException(std::forward<T>(t));
		} catch (...) {
			Error(std::make_exception_ptr(HttpServerSocketError()));
		}
	}

	void SocketError(const char *msg) noexcept {
		SocketError(std::runtime_error(msg));
	}

	void ProtocolError(const char *msg) noexcept {
		Error(std::make_exception_ptr(SocketProtocolError(msg)));
	}

	/* virtual methods from class BufferedSocketHandler */
	BufferedResult OnBufferedData() override;
	DirectResult OnBufferedDirect(SocketDescriptor fd, FdType fd_type) override;
	bool OnBufferedHangup() noexcept override;
	bool OnBufferedClosed() noexcept override;
	bool OnBufferedWrite() override;
	bool OnBufferedDrained() noexcept override;
	void OnBufferedError(std::exception_ptr e) noexcept override;

	/* virtual methods from class IstreamHandler */
	IstreamReadyResult OnIstreamReady() noexcept override;
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	IstreamDirectResult OnDirect(FdType type, FileDescriptor fd,
				     off_t offset, std::size_t max_length,
				     bool then_eof) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};
