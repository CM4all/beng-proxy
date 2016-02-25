/*
 * Global variables which are not worth passing around.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_global.hxx"

struct tcache *global_translate_cache;

HttpCache *global_http_cache;

FilterCache *global_filter_cache;

Stock *global_pipe_stock;
