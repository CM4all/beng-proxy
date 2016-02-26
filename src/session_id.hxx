/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_ID_H
#define BENG_PROXY_SESSION_ID_H

#include <inline/compiler.h>

#include <array>

#include <stddef.h>
#include <stdint.h>
#include <string.h> /* for memcmp() */

class SessionId {
    static constexpr size_t SESSION_ID_WORDS = 4;
    std::array<uint32_t, SESSION_ID_WORDS> data;

public:
    gcc_pure
    bool IsDefined() const {
        for (auto i : data)
            if (i != 0)
                return true;
        return false;
    }

    void Clear() {
        std::fill(data.begin(), data.end(), 0);
    }

    void Generate();

    /**
     * Manipulate the modulo of GetClusterHash() so that it results in
     * the specified cluster node.
     */
    void SetClusterNode(unsigned cluster_size, unsigned cluster_node);

    gcc_pure
    bool operator==(const SessionId &other) const {
        return memcmp(this, &other, sizeof(other)) == 0;
    }

    gcc_pure
    size_t Hash() const {
        return data[0];
    }

    /**
     * Returns a hash that can be used to determine the cluster node
     * by calculating the modulo.
     */
    gcc_pure
    uint32_t GetClusterHash() const {
        return data.back();
   }

    /**
     * Parse a session id from a string.
     *
     * @return true on success, false on error
     */
    bool Parse(const char *p);

    const char *Format(struct session_id_string &buffer) const;
};

/**
 * Buffer for the function session_id_format().
 */
struct session_id_string {
    /**
     * Two hex characters per byte, plus the terminating zero.
     */
    char buffer[sizeof(SessionId) * 2 + 1];
};

#endif
