// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

class Option {
	nghttp2_option *option;

public:
	Option() noexcept {
		nghttp2_option_new(&option);
	}

	~Option() noexcept {
		nghttp2_option_del(option);
	}

	Option(const Option &) = delete;
	Option &operator=(const Option &) = delete;

	auto *get() const noexcept {
		return option;
	}
};

} // namespace NgHttp2
