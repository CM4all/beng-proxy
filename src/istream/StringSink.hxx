// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>
#include <string>

struct pool;
class UnusedIstreamPtr;
class CancellablePointer;
class StringSink;

class StringSinkHandler {
public:
	virtual void OnStringSinkSuccess(std::string &&value) noexcept = 0;
	virtual void OnStringSinkError(std::exception_ptr error) noexcept = 0;
};

StringSink &
NewStringSink(struct pool &pool, UnusedIstreamPtr input,
	      StringSinkHandler &handler, CancellablePointer &cancel_ptr);

void
ReadStringSink(StringSink &sink) noexcept;
