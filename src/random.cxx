/*
 * PRNG for session ids.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "random.hxx"
#include "system/urandom.hxx"

#include <daemon/log.h>

#include <random>

typedef std::mt19937 Prng;
static Prng prng;

template<typename T>
static size_t
obtain_entropy(T *dest, size_t max)
{
    size_t nbytes = UrandomRead(dest, max * sizeof(dest[0]));
    return nbytes / sizeof(dest[0]);
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
