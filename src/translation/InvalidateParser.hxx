/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRANSLATION_INVALIDATE_PARSER_HXX
#define TRANSLATION_INVALIDATE_PARSER_HXX

#include "translation/Protocol.hxx"

#include <stddef.h>

struct pool;
struct TranslateRequest;

unsigned
decode_translation_packets(struct pool *pool, TranslateRequest *request,
                           TranslationCommand *cmds, unsigned max_cmds,
                           const void *data, size_t length,
                           const char **site_r);

#endif
