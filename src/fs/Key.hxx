// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class StringBuilder;
class SocketAddress;
class SocketFilterParams;

/**
 * Can throw StringBuilder::Overfow.
 */
void
MakeFilteredSocketStockKey(StringBuilder &b, const char *name,
			   SocketAddress bind_address, SocketAddress address,
			   const SocketFilterParams *filter_params);
