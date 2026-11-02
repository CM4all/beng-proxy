// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "NopThreadSocketFilterFactory.hxx"

NopThreadSocketFilterFactory::~NopThreadSocketFilterFactory() noexcept
{
	thread_pool_stop();
	thread_pool_join();
	thread_pool_deinit();
}
