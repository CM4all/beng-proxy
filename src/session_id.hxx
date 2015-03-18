/*
 * The session id data structure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_ID_H
#define BENG_PROXY_SESSION_ID_H

#include <inline/compiler.h>

#include <stddef.h>
#include <stdint.h>

#ifdef SESSION_ID_SIZE
#include <array>
#include <string.h> /* for memcmp() */
#endif

struct SessionId {
#ifdef SESSION_ID_SIZE
    static constexpr size_t SESSION_ID_WORDS = (SESSION_ID_SIZE + 1) / 4;
    std::array<uint32_t, SESSION_ID_WORDS> data;
#else
    uint64_t value;

    SessionId() = default;

    explicit constexpr SessionId(uint64_t _value):value(_value) {}
#endif

    gcc_pure
    bool IsDefined() const {
#ifdef SESSION_ID_SIZE
        for (auto i : data)
            if (i != 0)
                return true;
        return false;
#else
        return value != 0;
#endif
    }

    void Clear() {
#ifdef SESSION_ID_SIZE
        std::fill(data.begin(), data.end(), 0);
#else
        value = 0;
#endif
    }

    void Generate();

    /**
     * Manipulate the modulo of GetClusterHash() so that it results in
     * the specified cluster node.
     */
    void SetClusterNode(unsigned cluster_size, unsigned cluster_node);

    gcc_pure
    bool operator==(const SessionId &other) const {
#ifdef SESSION_ID_SIZE
        return memcmp(this, &other, sizeof(other)) == 0;
#else
        return value == other.value;
#endif
    }

    gcc_pure
    size_t Hash() const {
#ifdef SESSION_ID_SIZE
        return data[0];
#else
        return value;
#endif
    }

    /**
     * Returns a hash that can be used to determine the cluster node
     * by calculating the modulo.
     */
    gcc_pure
    uint32_t GetClusterHash() const {
        return Hash();
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
