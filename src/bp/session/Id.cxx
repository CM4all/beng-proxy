/*
 * Copyright 2007-2017 Content Management AG
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

#include "Id.hxx"
#include "random.hxx"
#include "util/HexFormat.h"

#include <assert.h>
#include <stdlib.h>

void
SessionId::Generate()
{
    for (auto &i : data)
        i = random_uint32();
}

static auto
ToClusterNode(uint32_t id, unsigned cluster_size, unsigned cluster_node)
{
    uint32_t remainder = id % (uint32_t)cluster_size;
    assert(remainder < cluster_size);

    id -= remainder;
    id += cluster_node;
    return id;
}

void
SessionId::SetClusterNode(unsigned cluster_size, unsigned cluster_node)
{
    assert(cluster_size > 0);
    assert(cluster_node < cluster_size);

    const auto old_hash = GetClusterHash();
    const auto new_hash = ToClusterNode(old_hash, cluster_size, cluster_node);
    data.back() = new_hash;
}

bool
SessionId::Parse(const char *p)
{
    if (strlen(p) != SESSION_ID_WORDS * 8)
        return false;

    char segment[9];
    segment[8] = 0;
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i) {
        memcpy(segment, p + i * 8, 8);
        char *endptr;
        data[i] = strtoul(segment, &endptr, 16);
        if (endptr != segment + 8)
            return false;
    }

    return true;
}

const char *
SessionId::Format(struct session_id_string &string) const
{
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        format_uint32_hex_fixed(string.buffer + i * 8, data[i]);
    string.buffer[sizeof(string.buffer) - 1] = 0;
    return string.buffer;
}
