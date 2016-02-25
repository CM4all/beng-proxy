/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_EVENT_TIMEOUT_HXX
#define BENG_PROXY_EVENT_TIMEOUT_HXX

#include <sys/time.h>

/**
 * Generator for shared timeval constants.
 */
template<time_t s, suseconds_t us=0>
struct EventDuration {
    static constexpr struct timeval value = { s, us };
};

template<time_t s, suseconds_t us>
constexpr struct timeval EventDuration<s, us>::value;

#endif
