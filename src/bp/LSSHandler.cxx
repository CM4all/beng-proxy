// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "LSSHandler.hxx"
#include "spawn/ExitListener.hxx"
#include "spawn/Interface.hxx"
#include "spawn/Prepared.hxx"
#include "spawn/ProcessHandle.hxx"
#include "translation/Response.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "net/SocketDescriptor.hxx"
#include "io/FdHolder.hxx"
#include "util/DisposablePointer.hxx"

class BpListenStreamStockHandler::Process final
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

DisposablePointer
BpListenStreamStockHandler::Handle(const char *socket_path,
				   SocketDescriptor socket,
				   const TranslateResponse &response,
				   ListenStreamReadyHandler &handler)
{
	auto process = DoSpawn(spawn_service, socket_path, socket, response);
	return ToDeletePointer(new Process(handler, std::move(process)));
}
