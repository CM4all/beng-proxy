/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WIDGET_REF_HXX
#define BENG_PROXY_WIDGET_REF_HXX

#include "util/Compiler.h"

struct pool;

/** a reference to a widget inside a widget.  nullptr means the current
    (root) widget is being referenced */
struct widget_ref {
    const struct widget_ref *next;

    const char *id;
};

static constexpr char WIDGET_REF_SEPARATOR = ':';
#define WIDGET_REF_SEPARATOR_S ":"

gcc_pure gcc_malloc
const struct widget_ref *
widget_ref_parse(struct pool *pool, const char *p);

/**
 * Is the specified "inner" reference inside or the same as "outer"?
 */
gcc_pure
bool
widget_ref_includes(const struct widget_ref *outer,
                    const struct widget_ref *inner);

#endif
