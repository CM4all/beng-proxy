/*
 * Emulation layer for Google gadgets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GOOGLE_GADGET_H
#define __BENG_GOOGLE_GADGET_H

#include "istream.h"

struct processor_env;
struct widget;

istream_t
embed_google_gadget(pool_t pool, struct processor_env *env,
                    struct widget *widget);

#endif
