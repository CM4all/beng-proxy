// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "fcgi/Protocol.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "strmap.hxx"

#include <cstdint>
#include <string_view>

#include <sys/types.h>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
struct pool;
class StringMap;
struct fcgi_record_header;

struct FcgiRequest {
	uint16_t id;

	HttpMethod method;
	const char *uri;
	StringMap headers;

	off_t length;
};

class FcgiServer {
	UniqueSocketDescriptor socket;

public:
	[[nodiscard]]
	explicit FcgiServer(UniqueSocketDescriptor &&_socket) noexcept
		:socket(std::move(_socket)) {}

	[[nodiscard]]
	struct fcgi_record_header ReadHeader();

	[[nodiscard]]
	std::pair<struct fcgi_begin_request, uint_least16_t> ReadBeginRequest();

	std::byte ReadByte(std::size_t &remaining);
	std::size_t ReadLength(std::size_t &remaining);

	void ReadParams(struct pool &pool, FcgiRequest &request);

	[[nodiscard]]
	FcgiRequest ReadRequest(struct pool &pool);

	void DiscardRequestBody(const FcgiRequest &r);

	[[nodiscard]]
	std::size_t ReadRaw(std::span<std::byte> dest);

	[[nodiscard]]
	std::size_t ReadAllRaw(std::span<std::byte> dest);

	void ReadFullRaw(std::span<std::byte> dest);

	void DiscardRaw(std::size_t size);

	[[nodiscard]]
	std::size_t WriteRaw(std::span<const std::byte> src);

	void WriteFullRaw(std::span<const std::byte> src);

	void WriteHeader(const struct fcgi_record_header &src) {
		WriteFullRaw(std::as_bytes(std::span{&src, 1}));
	}

	void WriteResponseHeaders(const FcgiRequest &r, HttpStatus status,
				  const StringMap &headers);
	void WriteStdout(const FcgiRequest &r, std::string_view src);

	void WriteStderr(const FcgiRequest &r, std::string_view src);

	void MirrorRaw(std::size_t size);

	void EndResponse(const FcgiRequest &r);

	void Shutdown() noexcept {
		socket.Shutdown();
	}
};
