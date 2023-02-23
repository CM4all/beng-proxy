// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Widget.hxx"
#include "Class.hxx"
#include "util/LimitedConcurrencyQueue.hxx"

Widget::Widget(struct pool &_pool,
	       const WidgetClass *_cls) noexcept
	:PoolLeakDetector(_pool), pool(_pool), cls(_cls)
{
	if (_cls != nullptr)
		from_template.view = from_request.view = &_cls->views;
}

Widget::~Widget() noexcept
{
	DiscardForFocused();
	children.clear_and_dispose(Disposer{});
}
