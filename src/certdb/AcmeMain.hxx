// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

template<typename T> struct ConstBuffer;

void
Acme(ConstBuffer<const char *> args);
