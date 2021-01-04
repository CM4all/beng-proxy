/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Serialize objects portably into a buffer.
 */

#ifndef BENG_PROXY_SERIALIZE_HXX
#define BENG_PROXY_SERIALIZE_HXX

#include <stdint.h>

struct pool;
class GrowingBuffer;
class StringMap;
template<typename T> struct ConstBuffer;

/**
 * Exception which is thrown by the deserialize functions below, for
 * example if the buffer is too small.
 */
class DeserializeError {};

void
serialize_uint16(GrowingBuffer &gb, uint16_t value);

void
serialize_uint32(GrowingBuffer &gb, uint32_t value);

void
serialize_uint64(GrowingBuffer &gb, uint64_t value);

void
serialize_string(GrowingBuffer &gb, const char *value);

void
serialize_string_null(GrowingBuffer &gb, const char *value);

void
serialize_strmap(GrowingBuffer &gb, const StringMap &map);

void
serialize_strmap(GrowingBuffer &gb, const StringMap *map);

uint16_t
deserialize_uint16(ConstBuffer<void> &input);

uint32_t
deserialize_uint32(ConstBuffer<void> &input);

uint64_t
deserialize_uint64(ConstBuffer<void> &input);

const char *
deserialize_string(ConstBuffer<void> &input);

const char *
deserialize_string_null(ConstBuffer<void> &input);

void
deserialize_strmap(ConstBuffer<void> &input, StringMap &dest);

#endif
