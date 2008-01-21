/*
 * Emulation layer for Google gadgets.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_GOOGLE_GADGET_INTERNALH
#define __BENG_GOOGLE_GADGET_INTERNALH

#include "google-gadget.h"
#include "async.h"

struct google_gadget {
    pool_t pool;
    struct processor_env *env;
    struct widget *widget;

    istream_t delayed;

    struct async_operation_ref async;

    struct parser *parser;

    struct {
        enum {
            TAG_NONE,
            TAG_CONTENT,
        } tag;

        enum {
            TYPE_NONE,
            TYPE_URL,
            TYPE_HTML,
        } type;

        unsigned sending_content:1;

        const char *url;
    } from_parser;

    struct istream output;
};

#endif
