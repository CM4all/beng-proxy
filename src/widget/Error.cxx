// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Error.hxx"
#include "Widget.hxx"
#include "lib/fmt/ToBuffer.hxx"
#include "util/StringBuffer.hxx"

static StringBuffer<256>
FormatWidgetError(const Widget &widget, const char *msg)
{
	return FmtBuffer<256>("Error from widget '{}': {}",
			      widget.GetLogName(), msg);
}

WidgetError::WidgetError(const Widget &widget,
			 WidgetErrorCode _code, const char *_msg)
	:std::runtime_error(FormatWidgetError(widget, _msg).c_str()), code(_code)
{
}
