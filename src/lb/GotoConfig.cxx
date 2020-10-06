/*
 * Copyright 2007-2019 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "GotoConfig.hxx"

#ifdef HAVE_AVAHI

bool
LbBranchConfig::HasZeroConf() const noexcept
{
	if (fallback.HasZeroConf())
		return true;

	for (const auto &i : conditions)
		if (i.HasZeroConf())
			return true;

	return false;
}

#endif

LbProtocol
LbGotoConfig::GetProtocol() const noexcept
{
	assert(IsDefined());

	if (response.IsDefined() || lua != nullptr || translation != nullptr)
		return LbProtocol::HTTP;

	return cluster != nullptr
		? cluster->protocol
		: branch->GetProtocol();
}

const char *
LbGotoConfig::GetName() const noexcept
{
	assert(IsDefined());

	if (lua != nullptr)
		return lua->name.c_str();

	if (translation != nullptr)
		return translation->name.c_str();

	return cluster != nullptr
		? cluster->name.c_str()
		: branch->name.c_str();
}

#ifdef HAVE_AVAHI

bool
LbGotoConfig::HasZeroConf() const noexcept
{
	return (cluster != nullptr && cluster->HasZeroConf()) ||
		(branch != nullptr && branch->HasZeroConf());
}

#endif
