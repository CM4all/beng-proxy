// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ListenStreamStockHandler.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ProcessHandle.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Service.hxx"
#include "pool/Ptr.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "util/Cancellable.hxx"
#include "util/DisposablePointer.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

class SpawnListenStreamStockHandler::Process final
	: ExitListener
{
	ListenStreamReadyHandler &handler;

	std::unique_ptr<ChildProcessHandle> process;

public:
	Process(ListenStreamReadyHandler &_handler,
		std::unique_ptr<ChildProcessHandle> &&_process) noexcept
		:handler(_handler), process(std::move(_process))
	{
		process->SetExitListener(*this);
	}

	// virtual methods from class ExitListener
	void OnChildProcessExit([[maybe_unused]] int status) noexcept override {
		handler.OnListenStreamExit();
	}
};

class SpawnListenStreamStockHandler::Request final
	: TranslateHandler, Cancellable
{
	SpawnService &spawn_service;

	const char *const socket_path;

	const SocketDescriptor socket;

	ListenStreamReadyHandler &handler;

	PoolPtr translation_pool;

	CancellablePointer translation_cancel_ptr;

public:
	Request(SpawnService &_spawn_service,
		const char *_socket_path,
		SocketDescriptor _socket,
		ListenStreamReadyHandler &_handler) noexcept
		:spawn_service(_spawn_service),
		 socket_path(_socket_path), socket(_socket),
		 handler(_handler) {}

	void Start(TranslationService &_translation_service,
		   std::string_view key,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;

		translation_pool = pool_new_libc(nullptr, "SpawnListenStreamStockHandler::Request::Translation");

		const TranslateRequest request{
			.mount_listen_stream = AsBytes(key),
		};

		_translation_service.SendRequest(AllocatorPtr{translation_pool}, request,
						 {}, *this, translation_cancel_ptr);
	}

	// virtual methods from class TranslateHandler
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr _error) noexcept override;

	// virtual methods from class Cancellable
	void Cancel() noexcept override {
		assert(translation_cancel_ptr);

		translation_cancel_ptr.Cancel();
		delete this;
	}
};

static std::unique_ptr<ChildProcessHandle>
DoSpawn(SpawnService &service, const char *name,
	SocketDescriptor socket,
	const TranslateResponse &response)
{
	if (response.status != HttpStatus{}) {
		if (response.message != nullptr)
			throw FmtRuntimeError("Status {} from translation server: {}",
					      static_cast<unsigned>(response.status),
					      response.message);

		throw FmtRuntimeError("Status {} from translation server",
				      static_cast<unsigned>(response.status));
	}

	if (response.execute == nullptr)
		throw std::runtime_error("No EXECUTE from translation server");

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

	return service.SpawnChildProcess(name, std::move(p));
}


void
SpawnListenStreamStockHandler::Request::OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept
try {
	assert(translation_cancel_ptr);
	assert(translation_pool);

	const std::string_view tags = response->child_options.tag;

	auto process = DoSpawn(spawn_service, socket_path, socket,
							   *response);

	response = {};

	handler.OnListenStreamSuccess(ToDeletePointer(new Process(handler, std::move(process))),
				      tags);

	delete this;
} catch (...) {
	auto &_handler = handler;
	delete this;
	_handler.OnListenStreamError(std::current_exception());
}

void
SpawnListenStreamStockHandler::Request::OnTranslateError(std::exception_ptr _error) noexcept
{
	assert(translation_cancel_ptr);
	assert(translation_pool);

	auto &_handler = handler;
	delete this;
	_handler.OnListenStreamError(std::move(_error));
}

void
SpawnListenStreamStockHandler::OnListenStreamReady(std::string_view key,
						   const char *socket_path,
						   SocketDescriptor socket,
						   ListenStreamReadyHandler &handler,
						   CancellablePointer &cancel_ptr) noexcept
{
	auto *request = new Request(spawn_service, socket_path, socket, handler);
	request->Start(translation_service, key, cancel_ptr);
}
