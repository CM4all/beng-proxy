/*
 * Statistics collector.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stopwatch.hxx"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "util/StaticArray.hxx"
#include "util/WritableBuffer.hxx"

#include <daemon/log.h>
#include <socket/address.h>

#include <chrono>

#include <time.h>
#include <assert.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/resource.h>

enum {
    STOPWATCH_VERBOSE = 3,
};

struct StopwatchEvent {
    const char *name;

    std::chrono::steady_clock::time_point time;

    void Init(const char *_name) {
        name = _name;
        time = std::chrono::steady_clock::now();
    }
};

struct Stopwatch {
    struct pool *pool;

#ifndef NDEBUG
    struct pool_notify_state pool_notify;
#endif

    const char *const name;

    StaticArray<StopwatchEvent, 16> events;

    /**
     * Our own resource usage, measured when the stopwatch was
     * started.
     */
    struct rusage self;

    Stopwatch(struct pool &_pool, const char *_name)
        :pool(&_pool), name(_name) {
        ::pool_notify(pool, &pool_notify);

        events.append().Init(name);

        getrusage(RUSAGE_SELF, &self);
    }
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
    return stopwatch_enabled && daemon_log_config.verbose >= STOPWATCH_VERBOSE;
}

Stopwatch *
stopwatch_new(struct pool *pool, const char *name, const char *suffix)
{
    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return nullptr;

    if (suffix == nullptr)
        name = p_strdup(pool, name);
    else
        name = p_strcat(pool, name, suffix, nullptr);

    constexpr size_t MAX_NAME = 96;
    if (strlen(name) > MAX_NAME)
        name = p_strndup(pool, name, MAX_NAME);

    return NewFromPool<Stopwatch>(*pool, *pool, name);
}

Stopwatch *
stopwatch_new(struct pool *pool, SocketAddress address, const char *suffix)
{
    char buffer[1024];

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return nullptr;

    const char *name = socket_address_to_string(buffer, sizeof(buffer),
                                                address.GetAddress(),
                                                address.GetSize())
        ? buffer
        : "unknown";

    return stopwatch_new(pool, name, suffix);
}

Stopwatch *
stopwatch_fd_new(struct pool *pool, int fd, const char *suffix)
{
    struct sockaddr_storage address;
    socklen_t address_length = sizeof(address);

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return nullptr;

    return getpeername(fd, (struct sockaddr *)&address, &address_length) >= 0
        ? stopwatch_new(pool, SocketAddress((const struct sockaddr *)&address,
                                            address_length),
                        suffix)
        : stopwatch_new(pool, "unknown", suffix);
}

void
stopwatch_event(Stopwatch *stopwatch, const char *name)
{
    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return;

    assert(stopwatch != nullptr);
    assert(name != nullptr);

    if (stopwatch->events.full())
        /* array is full, do not record any more events */
        return;

    stopwatch->events.append().Init(name);
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
static void
AppendFormat(WritableBuffer<char> &buffer, const char *fmt, Args&&... args)
{
    int r = snprintf(buffer.data, buffer.size, fmt, args...);
    if (r > 0)
        buffer.skip_front(std::min(size_t(r), buffer.size - 1));
}

void
stopwatch_dump(const Stopwatch *stopwatch)
{
    struct rusage self;

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return;

    assert(stopwatch != nullptr);
    assert(!stopwatch->events.empty());

    if (stopwatch->events.size() < 2)
        /* nothing was recorded (except for the initial event) */
        return;

    char message[1024];

    WritableBuffer<char> b(message, sizeof(message));
    AppendFormat(b, "stopwatch[%s]:", stopwatch->name);

    for (const auto &i : stopwatch->events)
        AppendFormat(b, " %s=%ldms",
                     i.name,
                     ToLongMs(i.time - stopwatch->events.front().time));

    getrusage(RUSAGE_SELF, &self);
    AppendFormat(b, " (beng-proxy=%ld+%ldms)",
                 timeval_diff_ms(&self.ru_utime,
                                 &stopwatch->self.ru_utime),
                 timeval_diff_ms(&self.ru_stime,
                                 &stopwatch->self.ru_stime));

    daemon_log(STOPWATCH_VERBOSE, "%s\n", message);
}
