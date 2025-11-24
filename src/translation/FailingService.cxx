// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FailingService.hxx"
#include "Handler.hxx"
#include "AllocatorPtr.hxx"

#include <stdexcept>

void
FailingTranslationService::SendRequest([[maybe_unused]] AllocatorPtr alloc,
				       [[maybe_unused]] const TranslateRequest &request,
				       [[maybe_unused]] const StopwatchPtr &parent_stopwatch,
				       TranslateHandler &handler,
				       [[maybe_unused]] CancellablePointer &cancel_ptr) noexcept
{
	handler.OnTranslateError(std::make_exception_ptr(std::runtime_error{"unimplemented"}));
}
