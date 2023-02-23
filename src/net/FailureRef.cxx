// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FailureRef.hxx"

FailureRef::FailureRef(ReferencedFailureInfo &_info) noexcept
	:info(_info)
{
	info.Ref();
}

FailureRef::~FailureRef() noexcept
{
	info.Unref();
}
