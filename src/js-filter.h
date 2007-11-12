/*
 * This istream filter reads JavaScript code and performs some
 * transformations on it (to be implemented).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_JS_FILTER_H
#define __BENG_JS_FILTER_H

#include "istream.h"

istream_t
js_filter_new(pool_t pool, istream_t input);

#endif
