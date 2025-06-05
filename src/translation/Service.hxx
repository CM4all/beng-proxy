// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct TranslateRequest;
class AllocatorPtr;
class TranslateHandler;
class StopwatchPtr;
class CancellablePointer;

class TranslationService {
public:
	virtual ~TranslationService() noexcept = default;

	virtual void SendRequest(AllocatorPtr alloc,
				 const TranslateRequest &request,
				 const StopwatchPtr &parent_stopwatch,
				 TranslateHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept = 0;
};
