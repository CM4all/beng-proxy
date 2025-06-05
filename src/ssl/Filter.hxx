// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/openssl/UniqueSSL.hxx"

#include <memory>
#include <span>

class SslFilter;
class SocketFilter;
class ThreadSocketFilterHandler;

/**
 * Create a new SSL filter.
 */
std::unique_ptr<ThreadSocketFilterHandler>
ssl_filter_new(UniqueSSL &&ssl) noexcept;

/**
 * Cast a #ThreadSocketFilterHandler created by ssl_filter_new() to
 * #SslFilter.
 */
[[gnu::const]]
SslFilter &
ssl_filter_cast_from(ThreadSocketFilterHandler &tsfh) noexcept;

/**
 * Attempt to cast a #SocketFilter pointer to a #SslFilter.  If the
 * given #SocketFilter is a different type (or is nullptr), this
 * function returns nullptr.
 */
[[gnu::pure]]
const SslFilter *
ssl_filter_cast_from(const SocketFilter *socket_filter) noexcept;

[[gnu::pure]]
std::span<const unsigned char>
ssl_filter_get_alpn_selected(const SslFilter &ssl) noexcept;

[[gnu::pure]]
const char *
ssl_filter_get_peer_subject(const SslFilter &ssl) noexcept;

[[gnu::pure]]
const char *
ssl_filter_get_peer_issuer_subject(const SslFilter &ssl) noexcept;
