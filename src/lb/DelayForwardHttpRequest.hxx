// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Chrono.hxx"

struct LbHttpConnection;
struct IncomingHttpRequest;
class LbCluster;
class CancellablePointer;

void
DelayForwardHttpRequest(LbHttpConnection &connection,
			IncomingHttpRequest &request,
			LbCluster &cluster,
			Event::Duration delay,
			CancellablePointer &cancel_ptr) noexcept;
