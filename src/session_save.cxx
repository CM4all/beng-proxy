/*
 * Saving all sessions into a file.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_save.hxx"
#include "session_write.hxx"
#include "session_read.hxx"
#include "session_file.h"
#include "session_manager.hxx"
#include "session.hxx"
#include "system/clock.h"
#include "shm/dpool.hxx"

#include <inline/compiler.h>
#include <daemon/log.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

static const char *session_save_path;

static bool
session_save_callback(const Session *session, void *ctx)
{
    FILE *file = (FILE *)ctx;
    return session_write_magic(file, MAGIC_SESSION) &&
        session_write(file, session);
}

static bool
session_manager_save(FILE *file)
{
    return session_write_file_header(file) &&
        session_manager_visit(session_save_callback, file) &&
        session_write_file_tail(file);
}

static bool
session_manager_load(FILE *file)
{
    if (!session_read_file_header(file))
        return false;

    const unsigned now = now_s();

    unsigned num_added = 0, num_expired = 0;
    while (true) {
        uint32_t magic = session_read_magic(file);
        if (magic == MAGIC_END_OF_LIST)
            break;
        else if (magic != MAGIC_SESSION)
            return false;

        struct dpool *pool = session_manager_new_dpool();
        if (pool == nullptr)
            return false;

        Session *session;
        try {
            session = session_read(file, pool);
            if (session == nullptr) {
                dpool_destroy(pool);
                return false;
            }
        } catch (std::bad_alloc) {
            dpool_destroy(pool);
            return false;
        }

        if (now >= (unsigned)session->expires) {
            /* this session is already expired, discard it
               immediately */
            session_destroy(session);
            ++num_expired;
            continue;
        }

        session_manager_add(*session);
        ++num_added;
    }

    daemon_log(4, "loaded %u sessions, discarded %u expired sessions\n",
               num_added, num_expired);
    return true;
}

void
session_save()
{
    daemon_log(5, "saving sessions to %s\n", session_save_path);

    size_t length = strlen(session_save_path);
    char path[length + 5];
    memcpy(path, session_save_path, length);
    memcpy(path + length, ".tmp", 5);

    if (unlink(path) < 0 && errno != ENOENT) {
        daemon_log(2, "Failed to delete %s: %s\n", path, strerror(errno));
        return;
    }

    FILE *file = fopen(path, "wb");
    if (file == nullptr) {
        daemon_log(2, "Failed to create %s: %s\n", path, strerror(errno));
        return;
    }

    if (!session_manager_save(file)) {
        daemon_log(2, "Failed to save sessions\n");
        fclose(file);
        unlink(path);
        return;
    }

    fclose(file);

    if (rename(path, session_save_path) < 0) {
        daemon_log(2, "Failed to rename %s to %s: %s\n",
                   path, session_save_path, strerror(errno));
        unlink(path);
   }
}

void
session_save_init(const char *path)
{
    assert(session_save_path == nullptr);

    if (path == nullptr)
        return;

    session_save_path = path;

    FILE *file = fopen(session_save_path, "rb");
    if (file != nullptr) {
        session_manager_load(file);
        fclose(file);
    }
}

void
session_save_deinit()
{
    if (session_save_path == nullptr)
        return;

    session_save();
}
