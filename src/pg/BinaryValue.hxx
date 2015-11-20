/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_BINARY_VALUE_HXX
#define PG_BINARY_VALUE_HXX

#include <inline/compiler.h>

#include <cstddef>

struct PgBinaryValue {
    const void *value;
    size_t size;

    PgBinaryValue() = default;
    constexpr PgBinaryValue(const void *_value, size_t _size)
        :value(_value), size(_size) {}

    gcc_pure
    bool ToBool() const {
        return size == 1 && value != nullptr && *(const bool *)value;
    }
};

#endif
