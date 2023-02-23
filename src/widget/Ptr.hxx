// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/DestructDeleter.hxx"

#include <memory>

class Widget;
struct WidgetClass;

using WidgetPtr = std::unique_ptr<Widget, DestructDeleter>;

WidgetPtr
MakeWidget(struct pool &pool, const WidgetClass *cls) noexcept;

WidgetPtr
MakeRootWidget(struct pool &pool, const char *id) noexcept;
