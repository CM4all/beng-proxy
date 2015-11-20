/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_NOTIFY_HXX
#define PG_NOTIFY_HXX

#include <inline/compiler.h>

#include <postgresql/libpq-fe.h>

#include <algorithm>

/**
 * A thin C++ wrapper for a PGnotify pointer.
 */
class PgNotify {
    PGnotify *notify;

public:
    PgNotify():notify(nullptr) {}
    explicit PgNotify(PGnotify *_notify):notify(_notify) {}

    PgNotify(const PgNotify &other) = delete;
    PgNotify(PgNotify &&other):notify(other.notify) {
        other.notify = nullptr;
    }

    ~PgNotify() {
        if (notify != nullptr)
            PQfreemem(notify);
    }

    operator bool() const {
        return notify != nullptr;
    }

    PgNotify &operator=(const PgNotify &other) = delete;
    PgNotify &operator=(PgNotify &&other) {
        std::swap(notify, other.notify);
        return *this;
    }

    const PGnotify &operator*() const {
        return *notify;
    }

    const PGnotify *operator->() const {
        return notify;
    }
};

#endif
