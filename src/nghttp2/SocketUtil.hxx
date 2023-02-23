// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/net/BufferedSocket.hxx"

struct nghttp2_session;
class FilteredSocket;

namespace NgHttp2 {

BufferedResult
ReceiveFromSocketBuffer(nghttp2_session *session, FilteredSocket &socket);

ssize_t
SendToBuffer(FilteredSocket &socket, std::span<const std::byte> src) noexcept;

bool
OnSocketWrite(nghttp2_session *session, FilteredSocket &socket);

} // namespace NgHttp2
