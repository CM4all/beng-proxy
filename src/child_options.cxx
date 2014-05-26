/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "child_options.hxx"
#include "pool.h"
#include "djbhash.h"

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

child_options::child_options(struct pool *pool,
                             const struct child_options &src)
    :stderr_path(p_strdup_checked(pool, src.stderr_path)),
     rlimits(src.rlimits),
     ns(pool, src.ns),
     jail(pool, src.jail)
{
}

void
child_options::CopyFrom(struct pool *pool, const struct child_options *src)
{
    stderr_path = p_strdup_checked(pool, src->stderr_path);

    rlimit_options_copy(&rlimits, &src->rlimits);
    namespace_options_copy(pool, &ns, &src->ns);
    jail_params_copy(pool, &jail, &src->jail);
}

char *
child_options::MakeId(char *p) const
{
    if (stderr_path != nullptr)
        p += sprintf(p, ";e%08x", djb_hash_string(stderr_path));

    p = rlimit_options_id(&rlimits, p);
    p = namespace_options_id(&ns, p);
    p = jail_params_id(&jail, p);
    return p;
}

int
child_options::OpenStderrPath() const
{
    assert(stderr_path != nullptr);

    return open(stderr_path, O_CREAT|O_WRONLY|O_APPEND|O_CLOEXEC|O_NOCTTY,
                0666);
}

void
child_options::SetupStderr(bool stdout) const
{
    if (stderr_path == nullptr)
        return;

    int fd = OpenStderrPath();
    if (fd < 0) {
        fprintf(stderr, "open('%s') failed: %s\n",
                stderr_path, strerror(errno));
        _exit(2);
    }

    if (fd != 2)
        dup2(fd, 2);

    if (stdout && fd != 1)
        dup2(fd, 1);

    close(fd);
}
