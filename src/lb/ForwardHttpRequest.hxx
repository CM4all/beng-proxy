// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct LbHttpConnection;
struct IncomingHttpRequest;
class LbCluster;
class CancellablePointer;

void
ForwardHttpRequest(LbHttpConnection &connection,
		   IncomingHttpRequest &request,
		   LbCluster &cluster,
		   CancellablePointer &cancel_ptr) noexcept;
