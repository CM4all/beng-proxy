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
#include "io/UniqueFileDescriptor.hxx"
#include "util/LeakDetector.hxx"
#include "util/StringBuilder.hxx"

#include <boost/container/static_vector.hpp>
#include <boost/intrusive/slist.hpp>

#include <chrono>
#include <list>
#include <string>

#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

#if 0
#include <sys/resource.h>
#endif

static UniqueFileDescriptor stopwatch_fd;

struct StopwatchEvent {
    std::string name;

    std::chrono::steady_clock::time_point time =
        std::chrono::steady_clock::now();

    template<typename N>
    explicit StopwatchEvent(N &&_name) noexcept
        :name(std::forward<N>(_name)) {}
};

class Stopwatch final : LeakDetector,
                        public boost::intrusive::slist_base_hook<>
{
    const std::string name;

    const std::chrono::steady_clock::time_point time =
        std::chrono::steady_clock::now();

    std::list<std::shared_ptr<Stopwatch>> children;

    boost::container::static_vector<StopwatchEvent, 16> events;

#if 0
    /**
     * Our own resource usage, measured when the stopwatch was
     * started.
     */
    struct rusage self;
#endif

    const bool dump;

public:
    template<typename N>
    Stopwatch(N &&_name, bool _dump) noexcept
        :name(std::forward<N>(_name)),
         dump(_dump)
    {
#if 0
        getrusage(RUSAGE_SELF, &self);
#endif
    }

    ~Stopwatch() noexcept {
        assert(!is_linked());

        if (dump)
            Dump(0);
    }

    template<typename C>
    void AddChild(C &&child) noexcept {
        children.emplace_back(std::forward<C>(child));
    }

    void RecordEvent(const char *name) noexcept;

    void Dump(size_t indent) noexcept;
};

void
stopwatch_enable(UniqueFileDescriptor fd) noexcept
{
    assert(fd.IsDefined());

    stopwatch_fd = std::move(fd);
}

bool
stopwatch_is_enabled() noexcept
{
    return stopwatch_fd.IsDefined();
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

static std::shared_ptr<Stopwatch>
stopwatch_new(const char *name, const char *suffix) noexcept
{
    if (!stopwatch_is_enabled())
        return nullptr;

    return std::make_shared<Stopwatch>(MakeStopwatchName(name, suffix), true);
}

StopwatchPtr::StopwatchPtr(const char *name, const char *suffix) noexcept
    :stopwatch(stopwatch_new(name, suffix)) {}

StopwatchPtr::StopwatchPtr(Stopwatch *parent, const char *name,
                           const char *suffix) noexcept
{
    if (parent != nullptr) {
        stopwatch = std::make_shared<Stopwatch>(MakeStopwatchName(name, suffix),
                                                false);
        parent->AddChild(stopwatch);
    }
}

StopwatchPtr::~StopwatchPtr() = default;

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

#if 0

static long
timeval_diff_ms(const struct timeval *a, const struct timeval *b) noexcept
{
    return (a->tv_sec - b->tv_sec) * 1000 +
        (a->tv_usec - b->tv_usec) / 1000;
}

#endif

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
    if (!stopwatch_fd.IsDefined())
        return;

    char message[1024];
    StringBuilder<> b(message, sizeof(message));

    b.CheckAppend(indent);
    std::fill_n(b.GetTail(), indent, ' ');
    b.Extend(indent);

    b.Append(name.c_str());

    for (const auto &i : events)
        AppendFormat(b, " %s=%ldms",
                     i.name.c_str(),
                     ToLongMs(i.time - time));

#if 0
    struct rusage new_self;
    getrusage(RUSAGE_SELF, &new_self);
    AppendFormat(b, " (beng-proxy=%ld+%ldms)",
                 timeval_diff_ms(&new_self.ru_utime, &self.ru_utime),
                 timeval_diff_ms(&new_self.ru_stime, &self.ru_stime));
#endif

    b.Append('\n');

    if (stopwatch_fd.Write(message, strlen(message)) < 0) {
        stopwatch_fd.Close();
        return;
    }

    indent += 2;

    for (const auto &child : children)
        child->Dump(indent);
} catch (StringBuilder<>::Overflow) {
}
