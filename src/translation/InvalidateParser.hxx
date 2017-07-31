/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef TRANSLATION_INVALIDATE_PARSER_HXX
#define TRANSLATION_INVALIDATE_PARSER_HXX

#include "Request.hxx"
#include "translation/Protocol.hxx"
#include "util/TrivialArray.hxx"

#include <stddef.h>

struct pool;

struct TranslationInvalidateRequest : TranslateRequest {
    const char *site;

    TrivialArray<TranslationCommand, 32> commands;

    void Clear() {
        TranslateRequest::Clear();
        site = nullptr;
        commands.clear();
    }
};

/**
 * Throws on error.
 */
TranslationInvalidateRequest
ParseTranslationInvalidateRequest(struct pool &pool,
                                  const void *data, size_t length);

#endif
