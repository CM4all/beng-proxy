/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_id.hxx"
#include "random.hxx"
#include "format.h"

#include <assert.h>
#include <stdlib.h>

void
SessionId::Generate()
{
    for (auto &i : data)
        i = random_uint32();
}

static uint32_t
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

    uint32_t old_hash = GetClusterHash();
    uint32_t new_hash = ToClusterNode(old_hash, cluster_size, cluster_node);
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
