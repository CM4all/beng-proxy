// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Instance.hxx"
#include "Config.hxx"
#include "Listener.hxx"
#include "Control.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lb_features.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/Client.hxx"
#include "lib/avahi/Publisher.hxx"
#include "lib/avahi/Service.hxx"
#endif

void
LbInstance::InitAllListeners()
{
#ifdef HAVE_AVAHI
	std::forward_list<Avahi::Service> avahi_services;
#endif

	for (const auto &i : config.listeners) {
		try {
			listeners.emplace_front(*this, i);
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("Failed to set up listener '{}'",
							       i.name));
		}

#ifdef HAVE_AVAHI
		if (!i.zeroconf_service.empty()) {
			auto &listener = listeners.front();

			const char *const interface = i.GetZeroconfInterface();

			/* ask the kernel for the effective address
			   via getsockname(), because it may have
			   changed, e.g. if the kernel has selected a
			   port for us */
			const auto local_address = listener.GetLocalAddress();
			if (local_address.IsDefined())
				avahi_services.emplace_front(i.zeroconf_service.c_str(),
							     interface,
							     local_address,
							     i.v6only);
		}
#endif
	}

#ifdef HAVE_AVAHI
	if (!avahi_services.empty()) {
		assert(!avahi_publisher);

		Avahi::ErrorHandler &error_handler = *this;

		if (!avahi_client)
			avahi_client = std::make_unique<Avahi::Client>(GetEventLoop(),
								       error_handler);

		avahi_publisher = std::make_unique<Avahi::Publisher>(*avahi_client,
								     "beng-lb",
								     std::move(avahi_services),
								     error_handler);
	}
#endif
}

void
LbInstance::DeinitAllListeners() noexcept
{
	listeners.clear();
}

void
LbInstance::InitAllControls()
{
	for (const auto &i : config.controls) {
		controls.emplace_front(*this, i);
	}
}

void
LbInstance::DeinitAllControls() noexcept
{
	controls.clear();
}

void
LbInstance::EnableAllControls() noexcept
{
	for (auto &control : controls)
		control.Enable();
}
