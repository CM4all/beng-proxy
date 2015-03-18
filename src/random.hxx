/*
 * Provide entropy for the GLib PRNG.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RANDOM_HXX
#define BENG_PROXY_RANDOM_HXX

#include <glib.h>

void
obtain_entropy(GRand *r);

#endif
