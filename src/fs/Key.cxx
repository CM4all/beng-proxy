// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

void
MakeFilteredSocketStockKey(StringBuilder &b, std::string_view name,
			   SocketAddress bind_address, SocketAddress address,
			   const SocketFilterParams *filter_params)
{
	if (!bind_address.IsNull()) {
		AppendSocketAddress(b, bind_address);
		b.Append('>');
	}

	if (!name.empty())
		b.Append(name);
	else
		AppendSocketAddress(b, address);

	if (filter_params != nullptr) {
		b.Append('|');
		filter_params->AppendFilterId(b);
	}
}
