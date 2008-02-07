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

    struct async_operation delayed_operation;
    istream_t delayed, subst;

    struct async_operation_ref async;

    struct parser *parser;

    struct {
        enum {
            TAG_NONE,
            TAG_LOCALE,
            TAG_CONTENT,
        } tag;

        enum {
            TYPE_NONE,
            TYPE_URL,
            TYPE_HTML,
            TYPE_HTML_INLINE,
        } type;

        unsigned sending_content:1, in_parser:1;

        const char *url;
    } from_parser;

    unsigned has_locale:1, waiting_for_locale:1;

    struct {
        struct parser *parser;
        unsigned in_msg_tag;
        const char *key, *value;
    } msg;

    struct istream output;
};

void
google_gadget_msg_eof(struct google_gadget *gg);

void
google_gadget_msg_abort(struct google_gadget *gg);

void
google_gadget_msg_load(struct google_gadget *gg, const char *url);

void
google_gadget_msg_close(struct google_gadget *gg);

#endif
