/*
 * Statistics collector.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "stopwatch.h"

#include <daemon/log.h>
#include <socket/address.h>

#include <glib.h>

#include <time.h>
#include <assert.h>
#include <sys/socket.h>
#include <string.h>
#include <sys/resource.h>

enum {
    STOPWATCH_VERBOSE = 3,
    MAX_EVENTS = 16,
};

struct stopwatch_event {
    const char *name;

    struct timespec time;
};

struct stopwatch {
    pool_t pool;

    struct pool_notify pool_notify;

    const char *name;

    unsigned num_events;
    struct stopwatch_event events[MAX_EVENTS];

    /**
     * Our own resource usage, measured when the stopwatch was
     * started.
     */
    struct rusage self;
};

static bool stopwatch_enabled;

void
stopwatch_enable(void)
{
    assert(!stopwatch_enabled);

    stopwatch_enabled = true;
}

static void
stopwatch_event_init(struct stopwatch_event *event, const char *name)
{
    assert(event != NULL);

    event->name = name;
    clock_gettime(CLOCK_MONOTONIC, &event->time);
}

struct stopwatch *
stopwatch_new(pool_t pool, const char *name)
{
    struct stopwatch *stopwatch;

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return NULL;

    stopwatch = p_malloc(pool, sizeof(*stopwatch));
    stopwatch->pool = pool;
    pool_notify(pool, &stopwatch->pool_notify);

    stopwatch->name = p_strdup(pool, name);

    stopwatch_event_init(&stopwatch->events[0], stopwatch->name);
    stopwatch->num_events = 1;

    getrusage(RUSAGE_SELF, &stopwatch->self);

    return stopwatch;
}

struct stopwatch *
stopwatch_sockaddr_new(pool_t pool, const struct sockaddr *address,
                       size_t address_length, const char *suffix)
{
    char buffer[1024];

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return NULL;

    if (!socket_address_to_string(buffer, sizeof(buffer),
                                  address, address_length))
        strcpy(buffer, "unknown");

    return stopwatch_new(pool, p_strcat(pool, buffer,
                                        suffix != NULL ? " " : NULL, suffix,
                                        NULL));
}

struct stopwatch *
stopwatch_fd_new(pool_t pool, int fd, const char *suffix)
{
    struct sockaddr_storage address;
    socklen_t address_length = sizeof(address);

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return NULL;

    return getpeername(fd, (struct sockaddr *)&address, &address_length) >= 0
        ? stopwatch_sockaddr_new(pool, (struct sockaddr *)&address,
                                 address_length, suffix)
        : stopwatch_new(pool, suffix);
}

void
stopwatch_event(struct stopwatch *stopwatch, const char *name)
{
    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return;

    assert(stopwatch != NULL);
    assert(name != NULL);

    if (stopwatch->num_events >= MAX_EVENTS)
        /* array is full, do not record any more events */
        return;

    stopwatch_event_init(&stopwatch->events[stopwatch->num_events++],
                         name);
}

static long
timespec_diff_ms(const struct timespec *a, const struct timespec *b)
{
    return (a->tv_sec - b->tv_sec) * 1000 +
        (a->tv_nsec - b->tv_nsec) / 1000000;
}

static long
timeval_diff_ms(const struct timeval *a, const struct timeval *b)
{
    return (a->tv_sec - b->tv_sec) * 1000 +
        (a->tv_usec - b->tv_usec) / 1000;
}

void
stopwatch_dump(const struct stopwatch *stopwatch)
{
    GString *message;
    struct rusage self;

    if (!stopwatch_enabled || daemon_log_config.verbose < STOPWATCH_VERBOSE)
        return;

    assert(stopwatch != NULL);
    assert(stopwatch->num_events > 0);
    assert(stopwatch->num_events <= MAX_EVENTS);

    if (stopwatch->num_events < 2)
        /* nothing was recorded (except for the initial event) */
        return;

    message = g_string_sized_new(1024);
    g_string_printf(message, "stopwatch[%s]:", stopwatch->name);

    for (unsigned i = 1; i < stopwatch->num_events; ++i)
        g_string_append_printf(message, " %s=%ldms",
                               stopwatch->events[i].name,
                               timespec_diff_ms(&stopwatch->events[i].time,
                                                &stopwatch->events[0].time));

    getrusage(RUSAGE_SELF, &self);
    g_string_append_printf(message, " (beng-proxy=%ld+%ldms)",
                           timeval_diff_ms(&self.ru_utime,
                                           &stopwatch->self.ru_utime),
                           timeval_diff_ms(&self.ru_stime,
                                           &stopwatch->self.ru_stime));

    daemon_log(STOPWATCH_VERBOSE, "%s\n", message->str);

    g_string_free(message, true);
}
