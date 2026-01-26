// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "certdb/Config.hxx"
#include "ssl/NameCache.hxx"
#include "event/Loop.hxx"
#include "event/ShutdownListener.hxx"
#include "util/PrintException.hxx"

#include <fmt/core.h>

using std::string_view_literals::operator""sv;

class Instance final : CertNameCacheHandler {
	EventLoop event_loop;
	ShutdownListener shutdown_listener;
	CertNameCache cache;

public:
	explicit Instance(const CertDatabaseConfig &config)
		:shutdown_listener(event_loop, BIND_THIS_METHOD(OnShutdown)),
		 cache(event_loop, config, *this) {
		shutdown_listener.Enable();
		cache.Connect();
	}

	void Run() noexcept {
		event_loop.Run();
	}

private:
	void OnShutdown() noexcept {
		cache.Disconnect();
	}

	/* virtual methods from CertNameCacheHandler */
	void OnCertModified(std::string_view name,
			    bool deleted) noexcept override {
		fmt::print(stderr, "{}: {}\n"sv,
			   deleted ? "deleted"sv : "modified"sv,
			   name);
	}
};

int
main(int argc, char **argv)
try {
	if (argc != 2) {
		fmt::print(stderr, "Usage: {} CONNINFO\n", argv[0]);
		return EXIT_FAILURE;
	}

	CertDatabaseConfig config;
	config.connect = argv[1];

	Instance instance(config);
	instance.Run();

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
}
