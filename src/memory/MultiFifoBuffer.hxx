// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "memory/BufferQueue.hxx"
#include "DefaultFifoBuffer.hxx"

class IstreamBucketList;

class MultiFifoBuffer : public BufferQueue {
public:
	void FillBucketList(IstreamBucketList &list) const noexcept;
};
