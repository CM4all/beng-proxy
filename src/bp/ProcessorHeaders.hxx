// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class StringMap;

/**
 * Returns the processed response headers.
 */
StringMap
processor_header_forward(struct pool &pool, const StringMap &src);
