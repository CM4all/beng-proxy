// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

class Widget;

/**
 * A handler class for looking up a widget in a container.
 */
class WidgetLookupHandler {
public:
	virtual void WidgetFound(Widget &widget) noexcept = 0;
	virtual void WidgetNotFound() noexcept = 0;
	virtual void WidgetLookupError(std::exception_ptr ep) noexcept = 0;
};
