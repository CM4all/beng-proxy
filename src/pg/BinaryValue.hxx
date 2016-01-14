/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_BINARY_VALUE_HXX
#define PG_BINARY_VALUE_HXX

#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <cstddef>

struct PgBinaryValue : ConstBuffer<void> {
    PgBinaryValue() = default;

    constexpr PgBinaryValue(ConstBuffer<void> _buffer)
        :ConstBuffer<void>(_buffer) {}

    constexpr PgBinaryValue(const void *_value, size_t _size)
        :ConstBuffer<void>(_value, _size) {}

    gcc_pure
    bool ToBool() const {
        return size == 1 && data != nullptr && *(const bool *)data;
    }
};

#endif
