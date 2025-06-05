// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

class AllocatorPtr;
class StringMap;
struct TranslateResponse;
class GrowingBuffer;

void
add_translation_vary_header(AllocatorPtr alloc, StringMap &headers,
			    const TranslateResponse &response);

void
write_translation_vary_header(GrowingBuffer &headers,
			      const TranslateResponse &response);
