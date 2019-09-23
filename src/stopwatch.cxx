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
#include "net/SocketDescriptor.hxx"
#include "net/StaticSocketAddress.hxx"
#include "net/ToString.hxx"
#include "io/Logger.hxx"
#include "util/LeakDetector.hxx"
#include "util/StringBuilder.hxx"

#include <boost/container/static_vector.hpp>
#include <boost/intrusive/slist.hpp>

#include <chrono>
#include <string>

#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>

enum {
    STOPWATCH_VERBOSE = 3,
};

struct StopwatchEvent {
    std::string name;

    std::chrono::steady_clock::time_point time;

    template<typename N>
    explicit StopwatchEvent(N &&_name) noexcept
        :name(std::forward<N>(_name)), time(std::chrono::steady_clock::now()) {}
};

class Stopwatch final : LeakDetector,
                        public boost::intrusive::slist_base_hook<>
{
    const std::string name;

    boost::intrusive::slist<Stopwatch,
                            boost::intrusive::constant_time_size<false>,
                            boost::intrusive::cache_last<true>> children;

    boost::container::static_vector<StopwatchEvent, 16> events;

    /**
     * Our own resource usage, measured when the stopwatch was
     * started.
     */
    struct rusage self;

    const bool dump;

public:
    template<typename N>
    Stopwatch(Stopwatch *parent,
              N &&_name) noexcept
        :name(std::forward<N>(_name)),
         dump(parent == nullptr)
    {
        events.emplace_back(name);

        getrusage(RUSAGE_SELF, &self);
    }

    template<typename N>
    Stopwatch(N &&_name) noexcept
        :Stopwatch(nullptr, std::forward<N>(_name)) {}

    template<typename N>
    Stopwatch(Stopwatch &parent, N &&_name) noexcept
        :Stopwatch(&parent, std::forward<N>(_name))
    {
        parent.children.push_back(*this);
    }

    ~Stopwatch() noexcept {
        assert(!is_linked());

        if (dump)
            Dump(0);

        assert(children.empty());
    }

    void RecordEvent(const char *name) noexcept;

    void Dump(size_t indent) noexcept;
};

static bool stopwatch_enabled;

void
stopwatch_enable() noexcept
{
    assert(!stopwatch_enabled);

    stopwatch_enabled = true;
}

bool
stopwatch_is_enabled() noexcept
{
    return stopwatch_enabled && CheckLogLevel(STOPWATCH_VERBOSE);
}

static std::string
MakeStopwatchName(std::string name, const char *suffix) noexcept
{
    if (suffix != nullptr)
        name += suffix;

    constexpr size_t MAX_NAME = 96;
    if (name.length() > MAX_NAME)
        name.resize(MAX_NAME);

    return name;
}

static Stopwatch *
stopwatch_new(const char *name, const char *suffix) noexcept
{
    if (!stopwatch_is_enabled())
        return nullptr;

    return new Stopwatch(MakeStopwatchName(name, suffix));
}

static Stopwatch *
stopwatch_new(SocketAddress address, const char *suffix) noexcept
{
    char buffer[1024];

    if (!stopwatch_is_enabled())
        return nullptr;

    const char *name = ToString(buffer, sizeof(buffer), address, "unknown");
    return stopwatch_new(name, suffix);
}

static Stopwatch *
stopwatch_new(SocketDescriptor fd, const char *suffix) noexcept
{
    if (!stopwatch_is_enabled())
        return nullptr;

    const auto address = fd.GetPeerAddress();
    return address.IsDefined()
        ? stopwatch_new(address, suffix)
        : stopwatch_new("unknown", suffix);
}

StopwatchPtr::StopwatchPtr(const char *name, const char *suffix) noexcept
    :stopwatch(stopwatch_new(name, suffix)) {}

StopwatchPtr::StopwatchPtr(SocketAddress address, const char *suffix) noexcept
    :stopwatch(stopwatch_new(address, suffix)) {}

StopwatchPtr::StopwatchPtr(SocketDescriptor fd, const char *suffix) noexcept
    :stopwatch(stopwatch_new(fd, suffix)) {}

StopwatchPtr::StopwatchPtr(Stopwatch *parent, const char *name,
                           const char *suffix) noexcept
{
    if (parent != nullptr)
        stopwatch = new Stopwatch(*parent,
                                  MakeStopwatchName(name, suffix));
}

void
RootStopwatchPtr::Destruct(Stopwatch *stopwatch) noexcept
{
    if (!stopwatch->is_linked())
        delete stopwatch;
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
ToLongMs(std::chrono::steady_clock::duration d) noexcept
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}

static long
timeval_diff_ms(const struct timeval *a, const struct timeval *b) noexcept
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
Stopwatch::Dump(size_t indent) noexcept
try {
    assert(!events.empty());

    char message[1024];
    StringBuilder<> b(message, sizeof(message));

    b.CheckAppend(indent);
    std::fill_n(b.GetTail(), indent, ' ');
    b.Extend(indent);

    for (const auto &i : events)
        AppendFormat(b, " %s=%ldms",
                     i.name.c_str(),
                     ToLongMs(i.time - events.front().time));

    struct rusage new_self;
    getrusage(RUSAGE_SELF, &new_self);
    AppendFormat(b, " (beng-proxy=%ld+%ldms)",
                 timeval_diff_ms(&new_self.ru_utime, &self.ru_utime),
                 timeval_diff_ms(&new_self.ru_stime, &self.ru_stime));

    LogConcat(STOPWATCH_VERBOSE, "stopwatch", message);

    indent += 2;
    children.clear_and_dispose([indent](Stopwatch *child){
        child->Dump(indent);
        child->~Stopwatch();
    });
} catch (StringBuilder<>::Overflow) {
}
