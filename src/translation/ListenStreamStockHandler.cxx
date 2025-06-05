// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

	const TranslateRequest request;

	const char *const socket_path;

	const SocketDescriptor socket;

	ListenStreamReadyHandler &handler;
	CancellablePointer &caller_cancel_ptr;

	PoolPtr translation_pool;

	CancellablePointer translation_cancel_ptr;

public:
	Request(TranslationListenStreamStockHandler &_parent,
		std::string_view key,
		const char *_socket_path,
		SocketDescriptor _socket,
		ListenStreamReadyHandler &_handler,
		CancellablePointer &_caller_cancel_ptr) noexcept
		:parent(_parent),
		 request{.mount_listen_stream = AsBytes(key)},
		 socket_path(_socket_path), socket(_socket),
		 handler(_handler), caller_cancel_ptr(_caller_cancel_ptr) {}

	void Start(TranslationService &_translation_service) noexcept {
		caller_cancel_ptr = *this;

		translation_pool = pool_new_libc(nullptr, "TranslationListenStreamStockHandler::Request::Translation");

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

	parent.Handle(socket_path, socket, std::move(response),
		      handler, caller_cancel_ptr);
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
	auto *request = new Request(*this, key, socket_path, socket, handler, cancel_ptr);
	request->Start(translation_service);
}
