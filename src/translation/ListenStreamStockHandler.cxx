// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ListenStreamStockHandler.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "translation/Service.hxx"
#include "pool/Ptr.hxx"
#include "pool/pool.hxx"
#include "net/SocketDescriptor.hxx"
#include "util/Cancellable.hxx"
#include "util/DisposablePointer.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "stopwatch.hxx"

class TranslationListenStreamStockHandler::Request final
	: TranslateHandler, Cancellable
{
	TranslationListenStreamStockHandler &parent;

	const char *const socket_path;

	const SocketDescriptor socket;

	ListenStreamReadyHandler &handler;

	PoolPtr translation_pool;

	CancellablePointer translation_cancel_ptr;

public:
	Request(TranslationListenStreamStockHandler &_parent,
		const char *_socket_path,
		SocketDescriptor _socket,
		ListenStreamReadyHandler &_handler) noexcept
		:parent(_parent),
		 socket_path(_socket_path), socket(_socket),
		 handler(_handler) {}

	void Start(TranslationService &_translation_service,
		   std::string_view key,
		   CancellablePointer &caller_cancel_ptr) noexcept {
		caller_cancel_ptr = *this;

		translation_pool = pool_new_libc(nullptr, "TranslationListenStreamStockHandler::Request::Translation");

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

void
TranslationListenStreamStockHandler::Request::OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept
try {
	assert(translation_cancel_ptr);
	assert(translation_pool);

	const std::string_view tags = response->child_options.tag;
	auto process = parent.Handle(socket_path, socket, *response, handler);

	response = {};

	handler.OnListenStreamSuccess(std::move(process), tags);

	delete this;
} catch (...) {
	response.reset();

	auto &_handler = handler;
	delete this;
	_handler.OnListenStreamError(std::current_exception());
}

void
TranslationListenStreamStockHandler::Request::OnTranslateError(std::exception_ptr _error) noexcept
{
	assert(translation_cancel_ptr);
	assert(translation_pool);

	auto &_handler = handler;
	delete this;
	_handler.OnListenStreamError(std::move(_error));
}

void
TranslationListenStreamStockHandler::OnListenStreamReady(std::string_view key,
							 const char *socket_path,
							 SocketDescriptor socket,
							 ListenStreamReadyHandler &handler,
							 CancellablePointer &cancel_ptr) noexcept
{
	auto *request = new Request(*this, socket_path, socket, handler);
	request->Start(translation_service, key, cancel_ptr);
}
