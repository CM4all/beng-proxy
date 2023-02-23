// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AlpnMake.hxx"

constexpr auto alpn_http_1_1 = MakeAlpnString("http/1.1");
constexpr auto alpn_h2 = MakeAlpnString("h2");
constexpr auto alpn_http_any = ConcatAlpnStrings(alpn_h2, alpn_http_1_1);

constexpr auto alpn_acme_tls1 = MakeAlpnString("acme-tls/1");
