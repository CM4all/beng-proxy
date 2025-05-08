// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class UnusedIstreamPtr;
class ThreadQueue;

UnusedIstreamPtr
NewGzipIstream(struct pool &pool, ThreadQueue &queue,
	       UnusedIstreamPtr input) noexcept;
