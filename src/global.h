/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GLOBAL_H
#define __BENG_GLOBAL_H

extern struct tcache *global_translate_cache;

extern struct http_cache *global_http_cache;

extern struct hstock *global_ajp_client_stock;

#endif
