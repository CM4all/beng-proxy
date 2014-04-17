/*
 * Provide entropy for the GLib PRNG.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RANDOM_H
#define BENG_PROXY_RANDOM_H

#include <glib.h>

#ifdef __cplusplus
extern "C" {
#endif

void
obtain_entropy(GRand *r);

#ifdef __cplusplus
}
#endif

#endif
