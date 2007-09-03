/*
 * Embed a processed HTML document
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_EMBED_H
#define __BENG_EMBED_H

#include "istream.h"

struct widget;

istream_t
embed_new(pool_t pool, const char *url, const struct widget *widget);

#endif
