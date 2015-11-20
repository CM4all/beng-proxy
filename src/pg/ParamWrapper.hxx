/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_PARAM_WRAPPER_HXX
#define PG_PARAM_WRAPPER_HXX

#include "BinaryValue.hxx"

#include <cstdio>
#include <cstddef>

template<typename T>
struct PgParamWrapper {
    PgParamWrapper(const T &t);
    const char *GetValue() const;

    /**
     * Is the buffer returned by GetValue() binary?  If so, the method
     * GetSize() must return the size of the value.
     */
    bool IsBinary() const;

    /**
     * Returns the size of the value in bytes.  Only applicable if
     * IsBinary() returns true and the value is non-nullptr.
     */
    size_t GetSize() const;
};

template<>
struct PgParamWrapper<PgBinaryValue> {
    PgBinaryValue value;

    constexpr PgParamWrapper(PgBinaryValue _value)
        :value(_value) {}

    constexpr const char *GetValue() const {
        return (const char *)value.value;
    }

    constexpr bool IsBinary() const {
        return true;
    }

    constexpr size_t GetSize() const {
        return value.size;
    }
};

template<>
struct PgParamWrapper<const char *> {
    const char *value;

    constexpr PgParamWrapper(const char *_value):value(_value) {}

    constexpr const char *GetValue() const {
        return value;
    }

    constexpr bool IsBinary() const {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<int> {
    char buffer[16];

    PgParamWrapper(int i) {
        sprintf(buffer, "%i", i);
    }

    const char *GetValue() const {
        return buffer;
    }

    bool IsBinary() const {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<unsigned> {
    char buffer[16];

    PgParamWrapper(unsigned i) {
        sprintf(buffer, "%u", i);
    }

    const char *GetValue() const {
        return buffer;
    }

    bool IsBinary() const {
        return false;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

template<>
struct PgParamWrapper<bool> {
    const char *value;

    constexpr PgParamWrapper(bool _value):value(_value ? "t" : "f") {}

    constexpr bool IsBinary() const {
        return false;
    }

    constexpr const char *GetValue() const {
        return value;
    }

    size_t GetSize() const {
        /* ignored for text columns */
        return 0;
    }
};

#endif
