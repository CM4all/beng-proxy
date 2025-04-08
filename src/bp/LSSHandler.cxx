// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LSSHandler.hxx"
#include "Instance.hxx"
#include "Listener.hxx"
#include "LStats.hxx"
#include "access_log/ChildErrorLog.hxx"
#include "spawn/CompletionHandler.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ProcessHandle.hxx"
#include "translation/Response.hxx"
#include "pool/UniquePtr.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "util/DisposablePointer.hxx"

BpListenStreamStockHandler::BpListenStreamStockHandler(BpInstance &_instance,
						       Net::Log::Sink *_log_sink,
						       const ChildErrorLogOptions &_log_options) noexcept
	:TranslationListenStreamStockHandler(*_instance.translation_service),
	 instance(_instance),
	 log_sink(_log_sink), log_options(_log_options)
{
}

class BpListenStreamStockHandler::Process final
	: Cancellable, SpawnCompletionHandler, ExitListener
{
	ListenStreamReadyHandler &handler;

	std::unique_ptr<ChildProcessHandle> process;

	const std::string tags;

	ChildErrorLog log;

public:
	Process(ListenStreamReadyHandler &_handler,
		std::string_view _tags) noexcept
		:handler(_handler), tags(_tags)
	{
	}

	void Start(EventLoop &event_loop, SpawnService &service,
		   std::string_view name,
		   SocketDescriptor socket,
		   UniquePoolPtr<TranslateResponse> &&_response,
		   Net::Log::Sink *log_sink,
		   const ChildErrorLogOptions &log_options,
		   CancellablePointer &cancel_ptr);

	// virtual methods from class Cancellable
	void Cancel() noexcept override {
		delete this;
	}

	// virtual methods from class SpawnCompletionHandler
	void OnSpawnSuccess() noexcept override {
		handler.OnListenStreamSuccess(ToDeletePointer(this), tags);
	}

	void OnSpawnError(std::exception_ptr error) noexcept override {
		handler.OnListenStreamError(std::move(error));
		delete this;
	}

	// virtual methods from class ExitListener
	void OnChildProcessExit([[maybe_unused]] int status) noexcept override {
		handler.OnListenStreamExit();
	}
};

class BpListenStreamStockHandler::HttpListener final
{
	const BpListenerConfig config;

	BpListener listener;

public:
	HttpListener(BpInstance &instance,
		     SocketDescriptor socket,
		     const TranslateResponse &response) noexcept
		:config(MakeConfig(response)),
		 listener(instance,
			  response.stats_tag != nullptr ? instance.listener_stats[response.stats_tag] : instance.listener_stats[config.tag],
			  nullptr,
			  nullptr, // TODO?
			  instance.translation_service,
			  config,
			  socket.Duplicate())
	{
	}

private:
	[[gnu::pure]]
	static BpListenerConfig MakeConfig(const TranslateResponse &response) noexcept {
		BpListenerConfig config;

		if (response.listener_tag != nullptr)
			config.tag = response.listener_tag;

		config.access_logger = false; // TODO

		return config;
	}
};

inline void
BpListenStreamStockHandler::Process::Start(EventLoop &event_loop, SpawnService &service,
					   std::string_view name,
					   SocketDescriptor socket,
					   UniquePoolPtr<TranslateResponse> &&_response,
					   Net::Log::Sink *_log_sink,
					   const ChildErrorLogOptions &_log_options,
					   CancellablePointer &cancel_ptr)
{
	assert(!process);

	const auto &response = *_response;
	assert(response.execute != nullptr);

	PreparedChildProcess p;
	p.stdin_fd = socket.ToFileDescriptor();
	p.args.push_back(response.execute);

	for (const char *arg : response.args) {
		if (p.args.size() >= 4096)
			throw std::runtime_error("Too many APPEND packets from translation server");

		p.args.push_back(arg);
	}

	FdHolder close_fds;
	response.child_options.CopyTo(p, close_fds);

	if (_log_sink != nullptr && !p.stderr_fd.IsDefined() &&
	    p.stderr_path == nullptr)
		log.EnableClient(p, close_fds,
				 event_loop, _log_sink, _log_options,
				 response.child_options.stderr_pond);

	process = service.SpawnChildProcess(name, std::move(p));

	_response = {};

	cancel_ptr = *this;
	process->SetExitListener(*this);
	process->SetCompletionHandler(*this);
}

void
BpListenStreamStockHandler::Handle(const char *socket_path,
				   SocketDescriptor socket,
				   UniquePoolPtr<TranslateResponse> _response,
				   ListenStreamReadyHandler &handler,
				   CancellablePointer &cancel_ptr)
{
	const auto &response = *_response;

	if (response.status != HttpStatus{}) {
		if (response.message != nullptr)
			throw FmtRuntimeError("Status {} from translation server: {}",
					      static_cast<unsigned>(response.status),
					      response.message);

		throw FmtRuntimeError("Status {} from translation server",
				      static_cast<unsigned>(response.status));
	} else if (response.execute != nullptr) {
		auto *process2 = new Process(handler, response.child_options.tag);

		process2->Start(instance.event_loop, *instance.spawn_service,
				socket_path, socket,
				std::move(_response),
				log_sink, log_options,
				cancel_ptr);
	} else if (response.accept_http) {
		auto ptr = ToDeletePointer(new HttpListener(instance, socket, response));

		const std::string_view tags = response.child_options.tag;
		_response = {};

		handler.OnListenStreamSuccess(std::move(ptr), tags);
	} else
		throw std::runtime_error("No EXECUTE from translation server");
}
