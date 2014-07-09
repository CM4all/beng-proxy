/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "rlimit_options.hxx"
#include "util/djbhash.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

gcc_pure
static bool
rlimit_empty(const struct rlimit *r)
{
    return r->rlim_cur == RLIM_UNDEFINED && r->rlim_max == RLIM_UNDEFINED;
}

gcc_pure
static bool
rlimit_full(const struct rlimit *r)
{
    return r->rlim_cur != RLIM_UNDEFINED && r->rlim_max != RLIM_UNDEFINED;
}

gcc_pure
static bool
rlimit_options_empty(const struct rlimit_options *r)
{
    for (unsigned i = 0; i < RLIM_NLIMITS; ++i)
        if (!rlimit_empty(&r->values[i]))
            return false;

    return true;
}

gcc_pure
static unsigned
rlimit_options_hash(const struct rlimit_options *r)
{
    return djb_hash(r, sizeof(*r));
}

char *
rlimit_options_id(const struct rlimit_options *r, char *p)
{
    if (rlimit_options_empty(r))
        return p;

    *p++ = ';';
    *p++ = 'r';
    p += sprintf(p, "%08x", rlimit_options_hash(r));
    return p;
}

/**
 * Replace RLIM_UNDEFINED with current values.
 */
static const struct rlimit *
complete_rlimit(int resource, const struct rlimit *r, struct rlimit *buffer)
{
    if (rlimit_full(r))
        /* already complete */
        return r;

    if (getrlimit(resource, buffer) < 0) {
            fprintf(stderr, "getrlimit(%d) failed: %s\n",
                    resource, strerror(errno));
            _exit(2);
    }

    if (r->rlim_cur != RLIM_UNDEFINED)
        buffer->rlim_cur = r->rlim_cur;

    if (r->rlim_max != RLIM_UNDEFINED)
        buffer->rlim_max = r->rlim_max;

    return buffer;
}

static void
rlimit_apply(int resource, const struct rlimit *r)
{
    if (rlimit_empty(r))
        return;

    struct rlimit buffer;
    r = complete_rlimit(resource, r, &buffer);

    if (setrlimit(resource, r) < 0) {
        fprintf(stderr, "setrlimit(%d, %lu, %lu) failed: %s\n",
                resource, (unsigned long)r->rlim_cur,
                (unsigned long)r->rlim_max,
                strerror(errno));
        _exit(2);
    }
}

void
rlimit_options_apply(const struct rlimit_options *r)
{
    for (unsigned i = 0; i < RLIM_NLIMITS; ++i)
        rlimit_apply(i, &r->values[i]);
}

bool
rlimit_options_parse(struct rlimit_options *r, const char *s)
{
    enum {
        BOTH,
        SOFT,
        HARD,
    } which = BOTH;

    char ch;
    while ((ch = *s++) != 0) {
        unsigned resource;

        switch (ch) {
        case 'S':
            which = SOFT;
            continue;

        case 'H':
            which = HARD;
            continue;

        case 't':
            resource = RLIMIT_CPU;
            break;

        case 'f':
            resource = RLIMIT_FSIZE;
            break;

        case 'd':
            resource = RLIMIT_DATA;
            break;

        case 's':
            resource = RLIMIT_STACK;
            break;

        case 'c':
            resource = RLIMIT_CORE;
            break;

        case 'm':
            resource = RLIMIT_RSS;
            break;

        case 'u':
            resource = RLIMIT_NPROC;
            break;

        case 'n':
            resource = RLIMIT_NOFILE;
            break;

        case 'l':
            resource = RLIMIT_MEMLOCK;
            break;

        case 'v':
            resource = RLIMIT_AS;
            break;

            /* obsolete:
        case 'x':
            resource = RLIMIT_LOCKS;
            break;
            */

        case 'i':
            resource = RLIMIT_SIGPENDING;
            break;

        case 'q':
            resource = RLIMIT_MSGQUEUE;
            break;

        case 'e':
            resource = RLIMIT_NICE;
            break;

        case 'r':
            resource = RLIMIT_RTPRIO;
            break;

            /* not supported by bash's "ulimit" command
        case ?:
            resource = RLIMIT_RTTIME;
            break;
            */

        default:
            return false;
        }

        assert(resource < RLIM_NLIMITS);
        struct rlimit *const t = &r->values[resource];

        unsigned long value;

        if (*s == '!') {
            value = (unsigned long)RLIM_INFINITY;
            ++s;
        } else {
            char *endptr;
            value = strtoul(s, &endptr, 10);
            if (endptr == s)
                return false;

            s = endptr;

            switch (*s) {
            case 'G' : value <<= 10; /* fall through */
            case 'M' : value <<= 10;
            case 'K' : value <<= 10;
                ++s;
            }
        }

        switch (which) {
        case BOTH:
            t->rlim_cur = t->rlim_max = value;
            break;

        case SOFT:
            t->rlim_cur = value;
            break;

        case HARD:
            t->rlim_max = value;
            break;
        }
    }

    return true;
}
