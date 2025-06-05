// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

template<typename T> struct ConstBuffer;

void
Acme(ConstBuffer<const char *> args);
