/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "text_processor.h"
#include "strmap.h"
#include "istream.h"
#include "widget.h"
#include "widget-class.h"
#include "penv.h"

#include <assert.h>

gcc_pure
static bool
text_processor_allowed_content_type(const char *content_type)
{
    assert(content_type != NULL);

    return strncmp(content_type, "text/", 5) == 0 ||
        strncmp(content_type, "application/json", 16) == 0 ||
        strncmp(content_type, "application/javascript", 22) == 0;
}

bool
text_processor_allowed(const struct strmap *headers)
{
    const char *content_type = strmap_get_checked(headers, "content-type");
    return content_type != NULL &&
        text_processor_allowed_content_type(content_type);
}

G_GNUC_PURE
static const char *
base_uri(struct pool *pool, const char *absolute_uri)
{
    const char *p;

    if (absolute_uri == NULL)
        return NULL;

    p = strchr(absolute_uri, ';');
    if (p == NULL) {
        p = strchr(absolute_uri, '?');
        if (p == NULL)
            return absolute_uri;
    }

    return p_strndup(pool, absolute_uri, p - absolute_uri);
}

static void
processor_subst_beng_widget(struct istream *istream,
                            const struct widget *widget,
                            const struct processor_env *env)
{
    istream_subst_add(istream, "&c:type;", widget->class_name);
    istream_subst_add(istream, "&c:class;",
                      widget_get_quoted_class_name(widget));
    istream_subst_add(istream, "&c:local;", widget->cls->local_uri);
    istream_subst_add(istream, "&c:id;", widget->id);
    istream_subst_add(istream, "&c:path;", widget_path(widget));
    istream_subst_add(istream, "&c:prefix;", widget_prefix(widget));
    istream_subst_add(istream, "&c:uri;", env->absolute_uri);
    istream_subst_add(istream, "&c:base;",
                      base_uri(env->pool, env->uri));
    istream_subst_add(istream, "&c:frame;",
                      strmap_get(env->args, "frame"));
    istream_subst_add(istream, "&c:view;", widget_get_view(widget)->name);
    istream_subst_add(istream, "&c:session;",
                      strmap_get(env->args, "session"));
}

struct istream *
text_processor(struct pool *pool, struct istream *istream,
               const struct widget *widget, const struct processor_env *env)
{
    assert(istream != NULL);
    assert(!istream_has_handler(istream));

    istream = istream_subst_new(pool, istream);
    processor_subst_beng_widget(istream, widget, env);
    return istream;
}
