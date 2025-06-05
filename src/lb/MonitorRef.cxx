// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MonitorRef.hxx"
#include "MonitorStock.hxx"
#include "MonitorController.hxx"

#include <assert.h>

LbMonitorRef::LbMonitorRef(LbMonitorStock &_stock,
			   LbMonitorController &_controller) noexcept
	:stock(_stock), controller(&_controller)
{
	controller->Ref();
}

void
LbMonitorRef::Release() noexcept
{
	assert(controller != nullptr);

	if (controller->Unref())
		stock.Remove(*controller);
}
