// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/**
 * OpenSSL global initialization.
 */
void
ssl_global_init();

void
ssl_global_deinit() noexcept;

struct ScopeSslGlobalInit {
	ScopeSslGlobalInit() {
		ssl_global_init();
	}

	~ScopeSslGlobalInit() noexcept {
		ssl_global_deinit();
	}

	ScopeSslGlobalInit(const ScopeSslGlobalInit &) = delete;
	ScopeSslGlobalInit &operator=(const ScopeSslGlobalInit &) = delete;
};
