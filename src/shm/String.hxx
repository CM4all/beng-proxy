/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef SHM_STRING_HXX
#define SHM_STRING_HXX

#include "util/StringView.hxx"

#include <new>
#include <utility>

struct dpool;

/**
 * A string allocated from shared memory.
 *
 * An instance is always in a well-defined state; it cannot be uninitialized.
 */
class DString {
    char *value = nullptr;

    explicit constexpr DString(char *_value):value(_value) {}

public:
    /**
     * Construct a "nulled" instance.
     */
    DString() = default;

    constexpr DString(std::nullptr_t) {}

    DString(struct dpool &pool, StringView src) throw(std::bad_alloc) {
        Set(pool, src);
    }

    DString(struct dpool &pool, const DString &src) throw(std::bad_alloc)
        :DString(pool, src.value) {}

    static DString Donate(char *_value) {
        return DString(_value);
    }

    DString(DString &&src):value(src.value) {
        src.value = nullptr;
    }

    DString &operator=(DString &&src) noexcept {
        std::swap(value, src.value);
        return *this;
    }

    constexpr operator bool() const noexcept {
        return value != nullptr;
    }

    constexpr operator const char *() const noexcept {
        return value;
    }

    constexpr const char *c_str() const noexcept {
        return value;
    }

    void Clear(struct dpool &pool) noexcept;

    /**
     * Assign a new value.  Throws std::bad_alloc if memory allocation
     * fails.
     */
    void Set(struct dpool &pool, StringView _value) throw(std::bad_alloc);

    /**
     * Assign a new value.  Returns false if memory allocation fails.
     */
    bool SetNoExcept(struct dpool &pool, StringView _value) noexcept;
};

#endif
