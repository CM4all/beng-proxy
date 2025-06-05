// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <nghttp2/nghttp2.h>

#include <string_view>

namespace NgHttp2 {

/**
 * Construct a #nghttp2_nv from two string views.
 */
constexpr auto
MakeNv(std::string_view name, std::string_view value,
       uint8_t flags=NGHTTP2_NV_FLAG_NONE) noexcept
{
	nghttp2_nv nv{};
	nv.name = const_cast<uint8_t *>((const uint8_t *)(const void *)name.data());
	nv.value = const_cast<uint8_t *>((const uint8_t *)(const void *)value.data());
	nv.namelen = name.size();
	nv.valuelen = value.size();
	nv.flags = flags;
	return nv;
}

} // namespace NgHttp2
