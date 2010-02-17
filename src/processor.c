/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "parser.h"
#include "args.h"
#include "widget.h"
#include "growing-buffer.h"
#include "tpool.h"
#include "embed.h"
#include "async.h"
#include "rewrite-uri.h"
#include "strref2.h"
#include "strref-pool.h"
#include "global.h"
#include "expansible-buffer.h"
#include "html-escape.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

enum uri_base {
    URI_BASE_TEMPLATE,
    URI_BASE_WIDGET,
    URI_BASE_CHILD,
    URI_BASE_PARENT,
};

struct uri_rewrite {
    enum uri_base base;
    enum uri_mode mode;
};

struct processor {
    pool_t pool, caller_pool;

    struct widget *container;
    struct processor_env *env;
    unsigned options;

    istream_t replace;

    struct parser *parser;
    bool had_input;

    enum {
        TAG_NONE,
        TAG_WIDGET,
        TAG_WIDGET_PATH_INFO,
        TAG_WIDGET_PARAM,
        TAG_WIDGET_HEADER,
        TAG_WIDGET_VIEW,
        TAG_A,
        TAG_FORM,
        TAG_IMG,
        TAG_SCRIPT,
        TAG_PARAM,
        TAG_REWRITE_URI,
    } tag;

    struct uri_rewrite uri_rewrite;

    /**
     * The default value for #uri_rewrite.
     */
    struct uri_rewrite default_uri_rewrite;

    /**
     * These values are used to buffer c:mode/c:base values in any
     * order, even after the actual URI attribute.
     */
    struct {
        bool pending;

        off_t uri_start, uri_end;
        struct expansible_buffer *value;

        /**
         * The positions of the c:mode/c:base attributes after the URI
         * attribute.  These have to be deleted *after* the URI
         * attribute has been rewritten.
         */
        struct {
            off_t start, end;
        } delete[2];
    } postponed_rewrite;

    struct {
        off_t start_offset;

        pool_t pool;
        struct widget *widget;

        struct {
            struct expansible_buffer *name;
            struct expansible_buffer *value;
        } param;

        struct expansible_buffer *params;
    } widget;

    struct async_operation async;

    struct http_response_handler_ref response_handler;
    struct async_operation_ref *async_ref;
};


static inline bool
processor_option_quiet(const struct processor *processor)
{
    return processor->replace == NULL;
}

static inline bool
processor_option_rewrite_url(const struct processor *processor)
{
    return (processor->options & PROCESSOR_REWRITE_URL) != 0;
}

static void
processor_replace_add(struct processor *processor, off_t start, off_t end,
                      istream_t istream)
{
    istream_replace_add(processor->replace, start, end, istream);
}


/*
 * async operation
 *
 */

static struct processor *
async_to_processor(struct async_operation *ao)
{
    return (struct processor*)(((char*)ao) - offsetof(struct processor, async));
}

static void
processor_async_abort(struct async_operation *ao)
{
    struct processor *processor = async_to_processor(ao);

    pool_unref(processor->caller_pool);

    if (processor->parser != NULL)
        parser_close(processor->parser);
}

static const struct async_operation_class processor_async_operation = {
    .abort = processor_async_abort,
};


/*
 * constructor
 *
 */

static const char *
base_uri(pool_t pool, const char *absolute_uri)
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
processor_subst_beng_widget(istream_t istream,
                            struct widget *widget,
                            const struct processor_env *env)
{
    istream_subst_add(istream, "&c:path;", widget_path(widget));
    istream_subst_add(istream, "&c:prefix;", widget_prefix(widget));
    istream_subst_add(istream, "&c:uri;", env->absolute_uri);
    istream_subst_add(istream, "&c:base;",
                      base_uri(env->pool, env->uri));
    istream_subst_add(istream, "&c:frame;",
                      strmap_get(env->args, "frame"));
    istream_subst_add(istream, "&c:session;",
                      strmap_get(env->args, "session"));
}

static void
processor_parser_init(struct processor *processor, istream_t input);

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

void
processor_new(pool_t caller_pool, http_status_t status,
              struct strmap *headers, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options,
              const struct http_response_handler *handler,
              void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    pool_t pool = pool_new_linear(caller_pool, "processor", 32768);
    struct processor *processor;

    assert(!http_status_is_empty(status));
    assert(istream != NULL);
    assert(!istream_has_handler(istream));
    assert(widget != NULL);

    if (widget->from_request.proxy_ref == NULL) {
        istream = istream_subst_new(pool, istream);
        processor_subst_beng_widget(istream, widget, env);
    }

    processor = p_malloc(pool, sizeof(*processor));
    processor->pool = pool;
    processor->caller_pool = caller_pool;

    processor->widget.pool = env->pool;

    processor->container = widget;
    processor->env = env;
    processor->options = options;
    processor->tag = TAG_NONE;

    processor->postponed_rewrite.pending = false;
    processor->postponed_rewrite.value = expansible_buffer_new(pool, 1024);

    processor->widget.widget = NULL;
    processor->widget.param.name = expansible_buffer_new(pool, 128);
    processor->widget.param.value = expansible_buffer_new(pool, 512);
    processor->widget.params = expansible_buffer_new(pool, 1024);

    if (widget->from_request.proxy_ref == NULL) {
        istream = istream_tee_new(pool, istream, true);
        processor->replace = istream_replace_new(pool, istream_tee_second(istream));
    } else {
        processor->replace = NULL;
    }

    processor_parser_init(processor, istream);
    pool_unref(pool);

    if (widget->from_request.proxy_ref == NULL) {
        struct strmap *headers2;

        if (processor_option_rewrite_url(processor)) {
            processor->default_uri_rewrite.base = URI_BASE_TEMPLATE;
            processor->default_uri_rewrite.mode = URI_MODE_DIRECT;
        }

        if (headers != NULL) {
            static const char *const copy_headers[] = {
                "content-language",
                "content-type",
                "content-disposition",
                "location",
                NULL,
            };

            headers2 = strmap_new(pool, 8);
            headers_copy2(headers, headers2, copy_headers);
        } else
            headers2 = NULL;

        http_response_handler_direct_response(handler, handler_ctx,
                                              status, headers2,
                                              processor->replace);
    } else {
        http_response_handler_set(&processor->response_handler,
                                  handler, handler_ctx);
        pool_ref(caller_pool);

        async_init(&processor->async, &processor_async_operation);
        async_ref_set(async_ref, &processor->async);
        processor->async_ref = async_ref;

        pool_ref(pool);
        do {
            processor->had_input = false;
            parser_read(processor->parser);
        } while (processor->had_input && processor->parser != NULL);
        pool_unref(pool);
    }
}

static void
processor_uri_rewrite_init(struct processor *processor)
{
    assert(!processor->postponed_rewrite.pending);

    processor->uri_rewrite = processor->default_uri_rewrite;
}

static void
processor_uri_rewrite_delete(struct processor *processor,
                             off_t start, off_t end)
{
    unsigned i = 0;

    if (!processor->postponed_rewrite.pending) {
        /* no URI attribute found yet: delete immediately */
        istream_replace_add(processor->replace, start, end, NULL);
        return;
    }

    /* find a free position in the "delete" array */

    while (processor->postponed_rewrite.delete[i].start > 0) {
        ++i;
        if (i >= 2)
            /* no more room in the array */
            return;
    }

    /* postpone the delete until the URI attribute has been replaced */

    processor->postponed_rewrite.delete[i].start = start;
    processor->postponed_rewrite.delete[i].end = end;
}

static void
transform_uri_attribute(struct processor *processor,
                        const struct parser_attr *attr,
                        enum uri_base base,
                        enum uri_mode mode);

static void
processor_uri_rewrite_attribute(struct processor *processor,
                                const struct parser_attr *attr)
{
    if (processor->postponed_rewrite.pending)
        /* cannot rewrite more than one attribute per element */
        return;

    /* postpone the URI rewrite until the tag is finished: save the
       attribute value position, save the original attribute value and
       set the "pending" flag */

    processor->postponed_rewrite.uri_start = attr->value_start;
    processor->postponed_rewrite.uri_end = attr->value_end;
    expansible_buffer_set_strref(processor->postponed_rewrite.value,
                                 &attr->value);

    processor->postponed_rewrite.delete[0].start = 0;
    processor->postponed_rewrite.delete[1].start = 0;
    processor->postponed_rewrite.pending = true;
}

static void
processor_uri_rewrite_commit(struct processor *processor)
{
    struct parser_attr uri_attribute = {
        .value_start = processor->postponed_rewrite.uri_start,
        .value_end = processor->postponed_rewrite.uri_end,
    };

    assert(processor->postponed_rewrite.pending);

    processor->postponed_rewrite.pending = false;

    /* rewrite the URI */

    expansible_buffer_read_strref(processor->postponed_rewrite.value,
                                  &uri_attribute.value);
    transform_uri_attribute(processor, &uri_attribute,
                            processor->uri_rewrite.base,
                            processor->uri_rewrite.mode);

    /* now delete all c:base/c:mode attributes which followed the
       URI */

    for (unsigned i = 0; i < 2; ++i)
        if (processor->postponed_rewrite.delete[i].start > 0)
            istream_replace_add(processor->replace,
                                processor->postponed_rewrite.delete[i].start,
                                processor->postponed_rewrite.delete[i].end,
                                NULL);
}


/*
 * parser callbacks
 *
 */

static bool
processor_processing_instruction(struct processor *processor,
                                 const struct strref *name)
{
    if (!processor_option_quiet(processor) &&
        processor_option_rewrite_url(processor) &&
        strref_cmp_literal(name, "cm4all-rewrite-uri") == 0) {
        processor->tag = TAG_REWRITE_URI;
        return true;
    }

    return false;
}

static bool
parser_element_start_in_widget(struct processor *processor,
                               enum parser_tag_type type,
                               const struct strref *_name)
{
    struct strref copy = *_name, *const name = &copy;

    if (type == TAG_PI)
        return processor_processing_instruction(processor, _name);

    if (strref_starts_with_n(name, "c:", 2))
        strref_skip(name, 2);

    if (strref_cmp_literal(name, "widget") == 0) {
        if (type == TAG_CLOSE)
            processor->tag = TAG_WIDGET;
    } else if (strref_cmp_literal(name, "path-info") == 0) {
        processor->tag = TAG_WIDGET_PATH_INFO;
    } else if (strref_cmp_literal(name, "param") == 0 ||
               strref_cmp_literal(name, "parameter") == 0) {
        processor->tag = TAG_WIDGET_PARAM;
        expansible_buffer_reset(processor->widget.param.name);
        expansible_buffer_reset(processor->widget.param.value);
    } else if (strref_cmp_literal(name, "header") == 0) {
        processor->tag = TAG_WIDGET_HEADER;
        expansible_buffer_reset(processor->widget.param.name);
        expansible_buffer_reset(processor->widget.param.value);
    } else if (strref_cmp_literal(name, "view") == 0) {
        processor->tag = TAG_WIDGET_VIEW;
    } else {
        processor->tag = TAG_NONE;
        return false;
    }

    return true;
}

static bool
processor_parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    struct processor *processor = ctx;

    processor->had_input = true;

    if (processor->tag == TAG_SCRIPT &&
        strref_lower_cmp_literal(&tag->name, "script") != 0)
        /* workaround for bugged scripts: ignore all closing tags
           except </SCRIPT> */
        return false;

    processor->tag = TAG_NONE;

    if (processor->widget.widget != NULL)
        return parser_element_start_in_widget(processor, tag->type, &tag->name);

    if (tag->type == TAG_PI)
        return processor_processing_instruction(processor, &tag->name);

    if (strref_cmp_literal(&tag->name, "c:widget") == 0) {
        if ((processor->options & PROCESSOR_CONTAINER) == 0 ||
            global_translate_cache == NULL)
            return false;

        if (tag->type == TAG_CLOSE) {
            assert(processor->widget.widget == NULL);
            return false;
        }

        processor->tag = TAG_WIDGET;
        processor->widget.widget = p_malloc(processor->widget.pool,
                                            sizeof(*processor->widget.widget));
        widget_init(processor->widget.widget, processor->widget.pool, NULL);
        expansible_buffer_reset(processor->widget.params);

        list_add(&processor->widget.widget->siblings,
                 &processor->container->children);
        processor->widget.widget->parent = processor->container;
    } else if (strref_lower_cmp_literal(&tag->name, "script") == 0) {
        processor->tag = TAG_SCRIPT;
        processor_uri_rewrite_init(processor);
    } else if (!processor_option_quiet(processor) &&
               processor_option_rewrite_url(processor)) {
        if (strref_lower_cmp_literal(&tag->name, "a") == 0 ||
            strref_lower_cmp_literal(&tag->name, "link") == 0) {
            processor->tag = TAG_A;
            processor_uri_rewrite_init(processor);
        } else if (strref_lower_cmp_literal(&tag->name, "link") == 0) {
            /* this isn't actually an anchor, but we are only interested in
               the HREF attribute */
            processor->tag = TAG_A;
            processor_uri_rewrite_init(processor);
        } else if (strref_lower_cmp_literal(&tag->name, "form") == 0) {
            processor->tag = TAG_FORM;
            processor_uri_rewrite_init(processor);
        } else if (strref_lower_cmp_literal(&tag->name, "img") == 0) {
            processor->tag = TAG_IMG;
            processor_uri_rewrite_init(processor);
        } else if (strref_lower_cmp_literal(&tag->name, "iframe") == 0 ||
                   strref_lower_cmp_literal(&tag->name, "embed") == 0 ||
                   strref_lower_cmp_literal(&tag->name, "video") == 0 ||
                   strref_lower_cmp_literal(&tag->name, "audio") == 0) {
            /* this isn't actually an IMG, but we are only interested
               in the SRC attribute */
            processor->tag = TAG_IMG;
            processor_uri_rewrite_init(processor);
        } else if (strref_lower_cmp_literal(&tag->name, "param") == 0) {
            processor->tag = TAG_PARAM;
            processor_uri_rewrite_init(processor);
        } else {
            processor->tag = TAG_NONE;
            return false;
        }
    } else {
        processor->tag = TAG_NONE;
        return false;
    }

    return true;
}

static enum uri_base
parse_uri_base(const struct strref *s);

static enum uri_mode
parse_uri_mode(const struct strref *s);

static void
replace_attribute_value(struct processor *processor,
                        const struct parser_attr *attr,
                        istream_t value)
{
    processor_replace_add(processor,
                          attr->value_start, attr->value_end,
                          value);
}

static void
strref_split(const struct strref *in, char separator,
             struct strref *before, struct strref *after)
{
    const char *x = strref_chr(in, separator);

    if (x != NULL) {
        strref_set(before, in->data, x - in->data);
        strref_set(after, x + 1, in->data + in->length - (x + 1));
    } else {
        *before = *in;
        strref_null(after);
    }
}

static void
transform_uri_attribute(struct processor *processor,
                        const struct parser_attr *attr,
                        enum uri_base base,
                        enum uri_mode mode)
{
    struct widget *widget = NULL;
    const struct strref *value = &attr->value;
    struct strref child_id, suffix;
    istream_t istream;

    switch (base) {
    case URI_BASE_TEMPLATE:
        /* no need to rewrite the attribute */
        return;

    case URI_BASE_WIDGET:
        widget = processor->container;
        break;

    case URI_BASE_CHILD:
        strref_split(value, '/', &child_id, &suffix);

        widget = widget_get_child(processor->container,
                                  strref_dup(processor->pool, &child_id));
        if (widget == NULL)
            return;

        if (!strref_is_null(&suffix))
            /* a slash followed by a relative URI */
            value = &suffix;
        else
            /* no slash, use the default path_info */
            value = NULL;
        break;

    case URI_BASE_PARENT:
        widget = processor->container->parent;
        if (widget == NULL)
            return;

        break;
    }

    assert(widget != NULL);

    if (widget->class == NULL && widget->class_name == NULL)
        return;

    istream = rewrite_widget_uri(processor->pool, processor->env->pool,
                                 global_translate_cache,
                                 processor->env->external_uri,
                                 processor->env->args, widget,
                                 processor->env->session_id,
                                 value, mode, widget == processor->container);
    if (istream != NULL)
        replace_attribute_value(processor, attr, istream);
}

static void
parser_widget_attr_finished(struct widget *widget,
                            pool_t pool,
                            const struct strref *name,
                            const struct strref *value)
{
    if (strref_cmp_literal(name, "type") == 0) {
        widget->class_name = strref_dup(pool, value);
    } else if (strref_cmp_literal(name, "id") == 0) {
        if (!strref_is_empty(value))
            widget_set_id(widget, pool, value);
    } else if (strref_cmp_literal(name, "display") == 0) {
        if (strref_cmp_literal(value, "inline") == 0)
            widget->display = WIDGET_DISPLAY_INLINE;
        if (strref_cmp_literal(value, "none") == 0)
            widget->display = WIDGET_DISPLAY_NONE;
        else
            widget->display = WIDGET_DISPLAY_NONE;
    } else if (strref_cmp_literal(name, "session") == 0) {
        if (strref_cmp_literal(value, "resource") == 0)
            widget->session = WIDGET_SESSION_RESOURCE;
        else if (strref_cmp_literal(value, "site") == 0)
            widget->session = WIDGET_SESSION_SITE;
    }
}

static enum uri_base
parse_uri_base(const struct strref *s)
{
    if (strref_cmp_literal(s, "widget") == 0)
        return URI_BASE_WIDGET;
    else if (strref_cmp_literal(s, "child") == 0)
        return URI_BASE_CHILD;
    else if (strref_cmp_literal(s, "parent") == 0)
        return URI_BASE_PARENT;
    else
        return URI_BASE_TEMPLATE;
}

static enum uri_mode
parse_uri_mode(const struct strref *s)
{
    if (strref_cmp_literal(s, "direct") == 0)
        return URI_MODE_DIRECT;
    else if (strref_cmp_literal(s, "focus") == 0)
        return URI_MODE_FOCUS;
    else if (strref_cmp_literal(s, "partial") == 0)
        return URI_MODE_PARTIAL;
    else if (strref_cmp_literal(s, "partition") == 0)
        /* deprecated */
        return URI_MODE_PARTIAL;
    else if (strref_cmp_literal(s, "proxy") == 0)
        return URI_MODE_PROXY;
    else
        return URI_MODE_DIRECT;
}

static void
processor_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    struct processor *processor = ctx;

    processor->had_input = true;

    if (!processor_option_quiet(processor) &&
        (processor->tag == TAG_A || processor->tag == TAG_FORM ||
         processor->tag == TAG_IMG || processor->tag == TAG_SCRIPT ||
         processor->tag == TAG_PARAM || processor->tag == TAG_REWRITE_URI) &&
        strref_cmp_literal(&attr->name, "c:base") == 0) {
        processor->uri_rewrite.base = parse_uri_base(&attr->value);
        processor_uri_rewrite_delete(processor, attr->name_start, attr->end);
        return;
    }

    if (!processor_option_quiet(processor) &&
        processor->tag != TAG_NONE &&
        strref_cmp_literal(&attr->name, "c:mode") == 0) {
        processor->uri_rewrite.mode = parse_uri_mode(&attr->value);
        processor_uri_rewrite_delete(processor, attr->name_start, attr->end);
        return;
    }

    switch (processor->tag) {
    case TAG_NONE:
        break;

    case TAG_WIDGET:
        assert(processor->widget.widget != NULL);

        parser_widget_attr_finished(processor->widget.widget,
                                    processor->widget.pool,
                                    &attr->name, &attr->value);
        break;

    case TAG_WIDGET_PARAM:
    case TAG_WIDGET_HEADER:
        assert(processor->widget.widget != NULL);

        if (strref_cmp_literal(&attr->name, "name") == 0) {
            expansible_buffer_set_strref(processor->widget.param.name,
                                         &attr->value);
        } else if (strref_cmp_literal(&attr->name, "value") == 0) {
            expansible_buffer_set_strref(processor->widget.param.value,
                                         &attr->value);
        }

        break;

    case TAG_WIDGET_PATH_INFO:
        assert(processor->widget.widget != NULL);

        if (strref_cmp_literal(&attr->name, "value") == 0) {
            processor->widget.widget->path_info
                = strref_dup(processor->widget.pool, &attr->value);
        }

        break;

    case TAG_WIDGET_VIEW:
        assert(processor->widget.widget != NULL);

        if (strref_cmp_literal(&attr->name, "name") == 0) {
            if (strref_is_empty(&attr->value)) {
                daemon_log(2, "empty view name\n");
                return;
            }

            processor->widget.widget->view
                = strref_dup(processor->widget.pool, &attr->value);
        }

        break;

    case TAG_IMG:
        if (strref_lower_cmp_literal(&attr->name, "src") == 0)
            processor_uri_rewrite_attribute(processor, attr);
        break;

    case TAG_A:
        if (strref_lower_cmp_literal(&attr->name, "href") == 0 &&
            !strref_starts_with_n(&attr->value, "#", 1) &&
            !strref_starts_with_n(&attr->value, "javascript:", 11))
            processor_uri_rewrite_attribute(processor, attr);
        break;

    case TAG_FORM:
        if (strref_lower_cmp_literal(&attr->name, "action") == 0)
            processor_uri_rewrite_attribute(processor, attr);
        break;

    case TAG_SCRIPT:
        if (!processor_option_quiet(processor) &&
            processor_option_rewrite_url(processor) &&
            strref_lower_cmp_literal(&attr->name, "src") == 0)
            processor_uri_rewrite_attribute(processor, attr);
        break;

    case TAG_PARAM:
        if (strref_lower_cmp_literal(&attr->name, "value") == 0)
            processor_uri_rewrite_attribute(processor, attr);
        break;

    case TAG_REWRITE_URI:
        break;
    }
}

static istream_t
embed_widget(struct processor *processor, struct processor_env *env,
             struct widget *widget)
{
    if (widget->class_name == NULL &&
        (widget->class == NULL ||
         widget->class->address.type == RESOURCE_ADDRESS_NONE)) {
        widget_cancel(widget);
        return NULL;
    }

    widget_copy_from_request(widget, env);
    if (!widget->from_request.proxy &&
        widget->from_request.proxy_ref == NULL &&
        processor->replace == NULL) {
        widget_cancel(widget);
        return NULL;
    }

    if (widget->from_request.proxy || widget->from_request.proxy_ref != NULL) {
        pool_t caller_pool = processor->caller_pool;
        struct http_response_handler_ref handler_ref =
            processor->response_handler;
        struct async_operation_ref *async_ref = processor->async_ref;

        parser_close(processor->parser);
        processor->parser = NULL;

        embed_frame_widget(caller_pool, env, widget,
                           handler_ref.handler, handler_ref.ctx,
                           async_ref);
        pool_unref(caller_pool);

        return NULL;
    } else {
        istream_t istream;

        istream = embed_inline_widget(processor->pool, env, widget);
        if (istream != NULL)
            istream = istream_catch_new(processor->pool, istream);

        return istream;
    }
}

static istream_t
widget_element_finished(struct processor *processor)
{
    struct widget *widget;

    assert(processor->widget.widget != NULL);
    assert(processor->widget.widget->parent == processor->container);

    widget = processor->widget.widget;
    processor->widget.widget = NULL;

    if (widget->class_name != NULL && widget_check_recursion(widget->parent)) {
        daemon_log(5, "maximum widget depth exceeded\n");
        return NULL;
    }

    if (!expansible_buffer_is_empty(processor->widget.params))
        widget->query_string = expansible_buffer_strdup(processor->widget.params,
                                                        processor->widget.pool);

    return embed_widget(processor, processor->env, widget);
}

static bool
header_name_valid(const char *name, size_t length)
{
    /* name must start with "X-" */
    if (length < 3 ||
        (name[0] != 'x' && name[0] != 'X') ||
        name[1] != '-')
        return false;

    /* the rest must be letters, digits or dash */
    for (size_t i = 2; i < length;  ++i)
        if (!char_is_alphanumeric(name[i]) && name[i] != '-')
            return false;

    return true;
}

static void
processor_parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    struct processor *processor = ctx;

    processor->had_input = true;

    if (processor->postponed_rewrite.pending)
        processor_uri_rewrite_commit(processor);

    if (processor->tag == TAG_WIDGET) {
        istream_t istream;

        if (tag->type == TAG_OPEN || tag->type == TAG_SHORT)
            processor->widget.start_offset = tag->start;
        else if (processor->widget.widget == NULL)
            return;

        assert(processor->widget.widget != NULL);

        if (tag->type == TAG_OPEN)
            return;

        istream = widget_element_finished(processor);
        assert(istream == NULL || processor->replace != NULL);

        if (processor->replace != NULL)
            processor_replace_add(processor, processor->widget.start_offset,
                                  tag->end, istream);
    } else if (processor->tag == TAG_WIDGET_PARAM) {
        struct pool_mark mark;
        const char *p;
        size_t length;

        assert(processor->widget.widget != NULL);

        if (expansible_buffer_is_empty(processor->widget.param.name))
            return;

        pool_mark(tpool, &mark);

        p = expansible_buffer_read(processor->widget.param.value, &length);
        if (memchr(p, '&', length) != NULL) {
            char *q = p_memdup(tpool, p, length);
            length = html_unescape_inplace(q, length);
            p = q;
        }

        p = args_format_n(tpool, NULL,
                          expansible_buffer_read_string(processor->widget.param.name),
                          p, length,
                          NULL, NULL, 0, NULL, NULL, 0, NULL);
        length = strlen(p);

        if (!expansible_buffer_is_empty(processor->widget.params))
            expansible_buffer_write_buffer(processor->widget.params, "&", 1);
        expansible_buffer_write_buffer(processor->widget.params, p, length);

        pool_rewind(tpool, &mark);
    } else if (processor->tag == TAG_WIDGET_HEADER) {
        const char *name;
        size_t length;

        assert(processor->widget.widget != NULL);

        if (tag->type == TAG_CLOSE)
            return;

        name = expansible_buffer_read(processor->widget.param.name, &length);
        if (!header_name_valid(name, length)) {
            daemon_log(3, "invalid widget HTTP header name\n");
            return;
        }

        if (processor->widget.widget->headers == NULL)
            processor->widget.widget->headers =
                strmap_new(processor->widget.pool, 16);

        strmap_add(processor->widget.widget->headers,
                   expansible_buffer_strdup(processor->widget.param.name,
                                            processor->widget.pool),
                   expansible_buffer_strdup(processor->widget.param.value,
                                            processor->widget.pool));
    } else if (processor->tag == TAG_SCRIPT) {
        if (tag->type == TAG_OPEN)
            parser_script(processor->parser);
        else if (tag->type == TAG_CLOSE)
            processor->tag = TAG_NONE;
    } else if (processor->tag == TAG_REWRITE_URI) {
        /* the settings of this tag become the new default */
        processor->default_uri_rewrite = processor->uri_rewrite;
    }
}

static size_t
processor_parser_cdata(const char *p __attr_unused, size_t length,
                       bool escaped __attr_unused, void *ctx)
{
    struct processor *processor = ctx;

    processor->had_input = true;

    return length;
}

static void
processor_parser_eof(void *ctx, off_t length __attr_unused)
{
    struct processor *processor = ctx;

    assert(processor->parser != NULL);

    processor->parser = NULL;

    if (processor->replace != NULL)
        istream_replace_finish(processor->replace);

    if (processor->container->from_request.proxy_ref != NULL) {
        http_response_handler_invoke_message(&processor->response_handler, processor->pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "Widget not found");
        pool_unref(processor->caller_pool);
    }
}

static void
processor_parser_abort(void *ctx)
{
    struct processor *processor = ctx;

    assert(processor->parser != NULL);

    processor->parser = NULL;

    if (processor->container->from_request.proxy_ref != NULL) {
        http_response_handler_invoke_abort(&processor->response_handler);
        pool_unref(processor->caller_pool);
    }
}

static const struct parser_handler processor_parser_handler = {
    .tag_start = processor_parser_tag_start,
    .tag_finished = processor_parser_tag_finished,
    .attr_finished = processor_parser_attr_finished,
    .cdata = processor_parser_cdata,
    .eof = processor_parser_eof,
    .abort = processor_parser_abort,
};

static void
processor_parser_init(struct processor *processor, istream_t input)
{
    processor->parser = parser_new(processor->pool, input,
                                   &processor_parser_handler, processor);
}
