// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/server/Handler.hxx"

struct BpInstance;

class BpPrometheusExporter final : public HttpServerRequestHandler {
	BpInstance &instance;

public:
	explicit BpPrometheusExporter(BpInstance &_instance) noexcept
		:instance(_instance) {}

	/* virtual methods from class HttpServerRequestHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
};
