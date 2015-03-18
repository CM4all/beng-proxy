/*
 * PRNG for session ids.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "random.hxx"
#include "fd_util.h"

#include <daemon/log.h>

#include <random>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>

typedef std::mt19937 Prng;
static Prng prng;

template<typename T>
static unsigned
read_some_entropy(const char *path, T *dest, unsigned max)
{
    int fd = open_cloexec(path, O_RDONLY, 0);
    if (fd < 0) {
        daemon_log(2, "Failed to open %s: %s\n", path, strerror(errno));
        return 0;
    }

    ssize_t nbytes = read(fd, dest, max * sizeof(dest[0]));
    if (nbytes < 0) {
        daemon_log(2, "Failed to read from %s: %s\n",
                   path, strerror(errno));
        close(fd);
        return 0;
    }

    close(fd);
    return nbytes / sizeof(dest[0]);
}

template<typename T>
static size_t
obtain_entropy(T *p, size_t size)
{
    return read_some_entropy("/dev/urandom", p, size);
}

void
random_seed()
{
    uint32_t seed[Prng::state_size];
    auto n = obtain_entropy(seed, Prng::state_size);
    if (n == 0)
        return;

    std::seed_seq ss(seed, seed + n);
    prng.seed(ss);
}

uint32_t
random_uint32()
{
    return prng();
}
