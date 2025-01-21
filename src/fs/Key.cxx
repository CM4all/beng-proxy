// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Key.hxx"
#include "Params.hxx"
#include "net/SocketAddress.hxx"
#include "net/FormatAddress.hxx"
#include "util/StringBuilder.hxx"

#include <assert.h>
#include <string.h>

static void
AppendSocketAddress(StringBuilder &b, SocketAddress address)
{
	assert(!address.IsNull());

	auto w = b.Write();
	if (ToString(w, address))
		b.Extend(strlen(w.data()));
}

static void
MakeKey(StringBuilder &b, SocketAddress bind_address,
	SocketAddress address) noexcept
{
	if (!bind_address.IsNull()) {
		AppendSocketAddress(b, bind_address);
		b.Append('>');
	}

	AppendSocketAddress(b, address);
}

void
MakeFilteredSocketStockKey(StringBuilder &b, std::string_view name,
			   SocketAddress bind_address, SocketAddress address,
			   const SocketFilterParams *filter_params)
{
	if (!name.empty())
		b.Append(name);
	else
		MakeKey(b, bind_address, address);

	if (filter_params != nullptr) {
		b.Append('|');

		const char *id = filter_params->GetFilterId();
		if (id != nullptr)
			b.Append(id);
	}
}
