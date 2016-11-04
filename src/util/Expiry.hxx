/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef EXPIRY_HXX
#define EXPIRY_HXX

#include <chrono>

/**
 * Helper library for handling expiry time stamps using the system's
 * monotonic clock.
 */
class Expiry {
    typedef std::chrono::steady_clock clock_type;
    typedef clock_type::time_point value_type;
    value_type value;

    constexpr Expiry(value_type _value):value(_value) {}

public:
    Expiry() = default;

    static Expiry Now() {
        return clock_type::now();
    }

    static Expiry AlreadyExpired() {
        return value_type::min();
    }

    static Expiry Never() {
        return value_type::max();
    }

    static constexpr Expiry Touched(Expiry now,
                                    std::chrono::seconds duration) {
        return now.value + duration;
    }

    static Expiry Touched(std::chrono::seconds duration) {
        return Touched(Now(), duration);
    }

    void Touch(Expiry now, std::chrono::seconds duration) {
        value = now.value + duration;
    }

    void Touch(std::chrono::seconds duration) {
        Touch(Now(), duration);
    }

    constexpr bool IsExpired(Expiry now) const {
        return now >= *this;
    }

    bool IsExpired() const {
        return IsExpired(Now());
    }

    constexpr bool operator==(Expiry other) const {
        return value == other.value;
    }

    constexpr bool operator>=(Expiry other) const {
        return value >= other.value;
    }
};

#endif
