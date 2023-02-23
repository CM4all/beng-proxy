// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Ptr.hxx"
#include "Widget.hxx"
#include "Class.hxx"
#include "pool/pool.hxx"

const WidgetClass root_widget_class{};

WidgetPtr
MakeWidget(struct pool &pool, const WidgetClass *cls) noexcept
{
	return WidgetPtr(NewFromPool<Widget>(pool, pool, cls));
}

WidgetPtr
MakeRootWidget(struct pool &pool, const char *id) noexcept
{
	return WidgetPtr(NewFromPool<Widget>(pool, Widget::RootTag{}, pool, id));
}
