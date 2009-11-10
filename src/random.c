/*
 * Provide entropy for the GLib PRNG.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "random.h"
#include "fd_util.h"
#include "socket-util.h"

#include <daemon/log.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static unsigned
read_some_entropy(const char *path, guint32 *dest, unsigned max)
{
    int fd = open_cloexec(path, O_RDONLY, 0);
    if (fd < 0) {
        daemon_log(2, "Failed to open %s: %s\n", path, strerror(errno));
        return 0;
    }

    socket_set_nonblock(fd, true);

    ssize_t nbytes = read(fd, dest, max * sizeof(dest[0]));
    if (nbytes < 0) {
        if (errno != EAGAIN)
            daemon_log(2, "Failed to read from %s: %s\n",
                       path, strerror(errno));
        close(fd);
        return 0;
    }

    close(fd);
    return nbytes / sizeof(dest[0]);
}

void
obtain_entropy(GRand *r)
{
    guint32 seed[64];

    /* read from /dev/random for strong entropy */

    unsigned n = read_some_entropy("/dev/random", seed, G_N_ELEMENTS(seed));

    /* fill the rest up with /dev/urandom */

    if (n < G_N_ELEMENTS(seed))
        n += read_some_entropy("/dev/urandom", seed, G_N_ELEMENTS(seed) - n);

    if (n < 8) {
        daemon_log(2, "Not enough entropy found\n");
        return;
    }

    g_rand_set_seed_array(r, seed, n);
}
