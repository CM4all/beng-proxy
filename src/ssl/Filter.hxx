/*
 * Copyright 2007-2021 CM4all GmbH
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

#pragma once

#include "ssl/Unique.hxx"

class SslFactory;
class SslFilter;
template<typename T> struct ConstBuffer;
class SocketFilter;
class ThreadSocketFilterHandler;

/**
 * Create a new SSL filter.
 */
SslFilter *
ssl_filter_new(UniqueSSL &&ssl) noexcept;

/**
 * Create a new SSL filter.
 *
 * Throws std::runtime_error on error.
 *
 * @param encrypted_fd the encrypted side of the filter
 * @param plain_fd the plain-text side of the filter (socketpair
 * to local service)
 */
SslFilter *
ssl_filter_new(SslFactory &factory);

ThreadSocketFilterHandler &
ssl_filter_get_handler(SslFilter &ssl) noexcept;

/**
 * Attempt to cast a #SocketFilter pointer to a #SslFilter.  If the
 * given #SocketFilter is a different type (or is nullptr), this
 * function returns nullptr.
 */
[[gnu::pure]]
const SslFilter *
ssl_filter_cast_from(const SocketFilter *socket_filter) noexcept;

[[gnu::pure]]
ConstBuffer<unsigned char>
ssl_filter_get_alpn_selected(const SslFilter &ssl) noexcept;

[[gnu::pure]]
const char *
ssl_filter_get_peer_subject(const SslFilter &ssl) noexcept;

[[gnu::pure]]
const char *
ssl_filter_get_peer_issuer_subject(const SslFilter &ssl) noexcept;
