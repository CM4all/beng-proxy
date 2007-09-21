/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FRAME_H
#define __BENG_FRAME_H

#include "istream.h"

struct widget;
struct processor_env;

istream_t
frame_widget_callback(pool_t pool, const struct processor_env *env,
                      struct widget *widget);

#endif
