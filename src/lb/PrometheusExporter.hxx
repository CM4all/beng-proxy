// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/server/Handler.hxx"

struct LbPrometheusExporterConfig;
struct LbInstance;

class LbPrometheusExporter final : public HttpServerRequestHandler {
	const LbPrometheusExporterConfig &config;

	LbInstance *instance = nullptr;

	class AppendRequest;

public:
	explicit LbPrometheusExporter(const LbPrometheusExporterConfig &_config) noexcept
		:config(_config) {}

	void SetInstance(LbInstance &_instance) noexcept {
		instance = &_instance;
	}

	/* virtual methods from class HttpServerRequestHandler */
	void HandleHttpRequest(IncomingHttpRequest &request,
			       const StopwatchPtr &parent_stopwatch,
			       CancellablePointer &cancel_ptr) noexcept override;
};
