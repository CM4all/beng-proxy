// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Widget.hxx"
#include "Class.hxx"

Widget::Widget(RootTag, struct pool &_pool, const char *_id) noexcept
	:Widget(_pool, &root_widget_class)
{
	id = _id;
	id_path = "";
	prefix = "C_";
}
