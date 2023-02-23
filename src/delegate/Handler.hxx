// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

class UniqueFileDescriptor;

class DelegateHandler {
public:
	virtual void OnDelegateSuccess(UniqueFileDescriptor fd) = 0;
	virtual void OnDelegateError(std::exception_ptr ep) = 0;
};
