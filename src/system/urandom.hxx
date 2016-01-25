/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_URANDOM_HXX
#define BENG_PROXY_URANDOM_HXX

#include <stddef.h>

size_t
UrandomRead(void *p, size_t size);

void
UrandomFill(void *p, size_t size);

#endif
