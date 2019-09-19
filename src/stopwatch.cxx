/*
 * Copyright 2007-2019 Content Management AG
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

#include "stopwatch.hxx"
#include "AllocatorPtr.hxx"
#include "net/SocketDescriptor.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/ToString.hxx"
#include "io/Logger.hxx"
#include "util/LeakDetector.hxx"
#include "util/StringBuilder.hxx"

#include <boost/container/static_vector.hpp>

#include <chrono>

#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

enum {
    STOPWATCH_VERBOSE = 3,
};

struct StopwatchEvent {
    const char *name;

    std::chrono::steady_clock::time_point time;

    explicit StopwatchEvent(const char *_name) noexcept
        :name(_name), time(std::chrono::steady_clock::now()) {}
};

class Stopwatch final : LeakDetector {
    AllocatorPtr alloc;

    const char *const name;

    boost::container::static_vector<StopwatchEvent, 16> events;

    /**
     * Our own resource usage, measured when the stopwatch was
     * started.
     */
    struct rusage self;

public:
    Stopwatch(AllocatorPtr _alloc, const char *_name)
        :alloc(_alloc), name(_name) {
        events.emplace_back(name);

        getrusage(RUSAGE_SELF, &self);
    }

    ~Stopwatch() noexcept {
        Dump();
    }

    void RecordEvent(const char *name) noexcept;

    void Dump() const noexcept;
};

static bool stopwatch_enabled;

void
stopwatch_enable()
{
    assert(!stopwatch_enabled);

    stopwatch_enabled = true;
}

bool
stopwatch_is_enabled()
{
    return stopwatch_enabled && CheckLogLevel(STOPWATCH_VERBOSE);
}

static Stopwatch *
stopwatch_new(AllocatorPtr alloc, const char *name, const char *suffix)
{
    if (!stopwatch_is_enabled())
        return nullptr;

    if (suffix == nullptr)
        name = alloc.Dup(name);
    else
        name = alloc.Concat(name, suffix);

    constexpr size_t MAX_NAME = 96;
    if (strlen(name) > MAX_NAME)
        name = alloc.DupZ({name, MAX_NAME});

    return alloc.New<Stopwatch>(alloc, name);
}

static Stopwatch *
stopwatch_new(AllocatorPtr alloc, SocketAddress address, const char *suffix)
{
    char buffer[1024];

    if (!stopwatch_is_enabled())
        return nullptr;

    const char *name = ToString(buffer, sizeof(buffer), address, "unknown");
    return stopwatch_new(alloc, name, suffix);
}

static Stopwatch *
stopwatch_new(AllocatorPtr alloc, SocketDescriptor fd, const char *suffix)
{
    if (!stopwatch_is_enabled())
        return nullptr;

    const auto address = fd.GetPeerAddress();
    return address.IsDefined()
        ? stopwatch_new(alloc, address, suffix)
        : stopwatch_new(alloc, "unknown", suffix);
}

StopwatchPtr::StopwatchPtr(AllocatorPtr alloc,
                           const char *name, const char *suffix) noexcept
    :stopwatch(stopwatch_new(alloc, name, suffix)) {}

StopwatchPtr::StopwatchPtr(AllocatorPtr alloc,
                           SocketAddress address, const char *suffix) noexcept
    :stopwatch(stopwatch_new(alloc, address, suffix)) {}

StopwatchPtr::StopwatchPtr(AllocatorPtr alloc,
                           SocketDescriptor fd, const char *suffix) noexcept
    :stopwatch(stopwatch_new(alloc, fd, suffix)) {}

void
StopwatchPtr::Destruct(Stopwatch &stopwatch) noexcept
{
    stopwatch.~Stopwatch();
}

inline void
Stopwatch::RecordEvent(const char *event_name) noexcept
{
    if (events.size() >= events.capacity())
        /* array is full, do not record any more events */
        return;

    events.emplace_back(event_name);
}

void
StopwatchPtr::RecordEvent(const char *name) const noexcept
{
    if (stopwatch != nullptr)
        stopwatch->RecordEvent(name);
}

static constexpr long
ToLongMs(std::chrono::steady_clock::duration d)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

static long
timeval_diff_ms(const struct timeval *a, const struct timeval *b)
{
    return (a->tv_sec - b->tv_sec) * 1000 +
        (a->tv_usec - b->tv_usec) / 1000;
}

template<typename... Args>
static inline void
AppendFormat(StringBuilder<> &b, const char *fmt, Args&&... args)
{
    size_t size = b.GetRemainingSize();
    size_t n = snprintf(b.GetTail(), size, fmt, args...);
    if (n >= size - 1)
        throw StringBuilder<>::Overflow();
    b.Extend(n);
}

inline void
Stopwatch::Dump() const noexcept
try {
    assert(!events.empty());

    if (events.size() < 2)
        /* nothing was recorded (except for the initial event) */
        return;

    char domain[128];
    snprintf(domain, sizeof(domain), "stopwatch %s", name);

    char message[1024];
    StringBuilder<> b(message, sizeof(message));

    for (const auto &i : events)
        AppendFormat(b, " %s=%ldms",
                     i.name,
                     ToLongMs(i.time - events.front().time));

    struct rusage new_self;
    getrusage(RUSAGE_SELF, &new_self);
    AppendFormat(b, " (beng-proxy=%ld+%ldms)",
                 timeval_diff_ms(&new_self.ru_utime, &self.ru_utime),
                 timeval_diff_ms(&new_self.ru_stime, &self.ru_stime));

    LogConcat(STOPWATCH_VERBOSE, domain, message);
} catch (StringBuilder<>::Overflow) {
}
