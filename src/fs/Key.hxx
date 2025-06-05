// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class StringBuilder;
class SocketAddress;
class SocketFilterParams;

/**
 * Can throw StringBuilder::Overfow.
 */
void
MakeFilteredSocketStockKey(StringBuilder &b, std::string_view name,
			   SocketAddress bind_address, SocketAddress address,
			   const SocketFilterParams *filter_params);
