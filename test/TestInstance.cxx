// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "TestInstance.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "io/SpliceSupport.hxx"

AutoPoolCommit::~AutoPoolCommit() noexcept
{
	pool_commit();
}

TestInstance::TestInstance() noexcept
{
	direct_global_init();
	fb_pool_init();
}

TestInstance::~TestInstance() noexcept
{
	fb_pool_deinit();
}
