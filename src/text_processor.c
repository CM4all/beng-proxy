/*
 * Process URLs in a CSS stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "text_processor.h"
#include "strmap.h"
#include "istream.h"
#include "widget.h"
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

static void
headers_copy2(struct strmap *in, struct strmap *out,
              const char *const* keys)
{
    const char *value;

    for (; *keys != NULL; ++keys) {
        value = strmap_get(in, *keys);
        if (value != NULL)
            strmap_set(out, *keys, value);
    }
}

struct strmap *
text_processor_header_forward(struct pool *pool, struct strmap *headers)
{
    if (headers == NULL)
        return NULL;

    static const char *const copy_headers[] = {
        "content-language",
        "content-type",
        "content-disposition",
        "location",
        NULL,
    };

    struct strmap *headers2 = strmap_new(pool, 8);
    headers_copy2(headers, headers2, copy_headers);
    return headers2;
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
    istream_subst_add(istream, "&c:path;", widget_path(widget));
    istream_subst_add(istream, "&c:prefix;", widget_prefix(widget));
    istream_subst_add(istream, "&c:uri;", env->absolute_uri);
    istream_subst_add(istream, "&c:base;",
                      base_uri(env->pool, env->uri));
    istream_subst_add(istream, "&c:frame;",
                      strmap_get(env->args, "frame"));
    istream_subst_add(istream, "&c:view;", widget_get_view_name(widget));
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
