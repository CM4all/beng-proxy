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
#include "js-filter.h"
#include "js-generator.h"
#include "widget-stream.h"
#include "tpool.h"

#include <assert.h>
#include <string.h>

enum uri_base {
    URI_BASE_TEMPLATE,
    URI_BASE_WIDGET,
    URI_BASE_FOCUS,
    URI_BASE_PROXY,
};

struct processor {
    pool_t pool;

    int had_input;

    pool_t widget_pool;

    struct widget *widget;
    struct processor_env *env;
    unsigned options;

    unsigned response_sent:1;

    istream_t replace;

    struct parser *parser;
    int js_generated;
    enum {
        TAG_NONE,
        TAG_HEAD,
        TAG_BODY,
        TAG_WIDGET,
        TAG_WIDGET_PATH_INFO,
        TAG_WIDGET_PARAM,
        TAG_A,
        TAG_FORM,
        TAG_IMG,
        TAG_SCRIPT,
    } tag;
    enum uri_base uri_base;
    off_t widget_start_offset;
    struct widget *embedded_widget;
    struct {
        size_t name_length, value_length;
        char name[64];
        char value[64];
    } widget_param;
    char widget_params[512];
    size_t widget_params_length;

    unsigned in_script:1, script_tail:1;
    growing_buffer_t script;
    off_t script_start_offset;

    struct async_operation async;

    struct http_response_handler_ref response_handler;
    struct async_operation_ref *async_ref;
};


static int
processor_option_quiet(const struct processor *processor)
{
    return processor->replace == NULL;
}

static int
processor_option_rewrite_url(const struct processor *processor)
{
    return (processor->options & PROCESSOR_REWRITE_URL) != 0;
}

static int
processor_option_fragment(const struct processor *processor)
{
    return (processor->options & PROCESSOR_FRAGMENT) != 0;
}

static int
processor_option_jscript(const struct processor *processor)
{
    return !processor_option_quiet(processor) &&
        processor->widget->class->old_style &&
        (processor->options & PROCESSOR_JSCRIPT) != 0;
}

static int
processor_option_jscript_root(const struct processor *processor)
{
    return !processor_option_quiet(processor) &&
        processor->widget->class->old_style &&
        (processor->options & (PROCESSOR_JSCRIPT|PROCESSOR_FRAGMENT))
        == PROCESSOR_JSCRIPT;
}

static void
processor_replace_add(struct processor *processor, off_t start, off_t end,
                      istream_t istream)
{
    istream_replace_add(processor->replace, start, end, istream);
}

static istream_t
processor_jscript(struct processor *processor)
{
    growing_buffer_t gb = growing_buffer_new(processor->pool, 512);

    assert(processor_option_jscript(processor));

    if (processor_option_jscript_root(processor))
        js_generate_includes(gb);

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n");

    if (processor_option_jscript_root(processor))
        js_generate_root_widget(gb, strmap_get(processor->env->args, "session"));

    js_generate_widget(gb, processor->widget, processor->pool);

    if ((processor->options & PROCESSOR_JSCRIPT_PREFS) != 0)
        js_generate_preferences(gb, processor->widget);

    growing_buffer_write_string(gb, "</script>\n");

    return growing_buffer_istream(gb);
}

static void
processor_insert_jscript(struct processor *processor, off_t offset)
{
    if (processor->js_generated || !processor_option_jscript(processor))
        return;

    processor_replace_add(processor, offset, offset,
                          processor_jscript(processor));
    processor->js_generated = 1;
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

    if (processor->parser != NULL)
        parser_close(processor->parser);
}

static struct async_operation_class processor_async_operation = {
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
                      base_uri(env->pool, env->absolute_uri));
    istream_subst_add(istream, "&c:frame;",
                      strmap_get(env->args, "frame"));
    istream_subst_add(istream, "&c:session;",
                      strmap_get(env->args, "session"));
}

static void
processor_parser_init(struct processor *processor, istream_t input);

void
processor_new(pool_t pool, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options,
              const struct http_response_handler *handler,
              void *handler_ctx,
              struct async_operation_ref *async_ref)
{
    struct processor *processor;

    assert(istream != NULL);
    assert(!istream_has_handler(istream));
    assert(widget != NULL);

    pool = pool_new_linear(pool, "processor", 32768);

    if (widget->from_request.proxy_ref == NULL &&
        widget->class->type == WIDGET_TYPE_BENG) {
        istream = istream_subst_new(pool, istream);
        processor_subst_beng_widget(istream, widget, env);
    }

    processor = p_malloc(pool, sizeof(*processor));
    processor->pool = pool;

    processor->widget_pool = env->pool;

    processor->widget = widget;
    processor->env = env;
    processor->options = options;

    processor->js_generated = 0;
    processor->embedded_widget = NULL;
    processor->in_script = 0;
    processor->script_tail = 0;

    if (widget->from_request.proxy_ref == NULL) {
        istream = istream_tee_new(pool, istream, 1);
        processor->replace = istream_replace_new(pool, istream_tee_second(istream), 0);
    } else {
        processor->replace = NULL;
    }

    processor_parser_init(processor, istream);

    if (widget->from_request.proxy_ref == NULL) {
        struct http_response_handler_ref response_handler;
        strmap_t headers;

        processor->response_sent = 1;

        if (processor_option_fragment(processor))
            processor_insert_jscript(processor, 0);

        headers = strmap_new(processor->pool, 4);
        strmap_addn(headers, "content-type", "text/html; charset=utf-8");

        http_response_handler_set(&response_handler,
                                  handler, handler_ctx);
        http_response_handler_invoke_response(&response_handler,
                                              HTTP_STATUS_OK, headers,
                                              processor->replace);
    } else {
        processor->response_sent = 0;

        http_response_handler_set(&processor->response_handler,
                                  handler, handler_ctx);

        async_init(&processor->async, &processor_async_operation);
        async_ref_set(async_ref, &processor->async);
        processor->async_ref = async_ref;

        istream_read(istream);
    }
}


static void
processor_finish_script(struct processor *processor, off_t end)
{
    assert(processor->in_script);

    processor->in_script = 0;

    if (processor->script == NULL)
        return;

    assert(processor->script_start_offset <= end);

    if (processor->script_start_offset < end)
        processor_replace_add(processor,
                              processor->script_start_offset, end,
                              js_filter_new(processor->pool,
                                            growing_buffer_istream(processor->script)));

    processor->script = NULL;
}

/*
 * parser callbacks
 *
 */

static void
parser_element_start_in_widget(struct processor *processor,
                               enum parser_tag_type type,
                               const struct strref *name)
{
    if (strref_cmp_literal(name, "c:widget") == 0) {
        if (type == TAG_CLOSE)
            processor->tag = TAG_WIDGET;
    } else if (strref_cmp_literal(name, "path-info") == 0) {
        processor->tag = TAG_WIDGET_PATH_INFO;
    } else if (strref_cmp_literal(name, "param") == 0) {
        processor->tag = TAG_WIDGET_PARAM;
        processor->widget_param.name_length = 0;
        processor->widget_param.value_length = 0;
    } else {
        processor->tag = TAG_NONE;
    }
}

static void
processor_parser_tag_start(const struct parser_tag *tag, void *ctx)
{
    struct processor *processor = ctx;

    processor->tag = TAG_NONE;

    if (processor->in_script) {
        /* workaround for bugged scripts: ignore all closing tags
           except </SCRIPT> */
        if (strref_cmp_literal(&tag->name, "script") != 0)
            return;

        processor_finish_script(processor, tag->start);
    }

    if (processor->embedded_widget != NULL) {
        parser_element_start_in_widget(processor, tag->type, &tag->name);
        return;
    }

    if (strref_cmp_literal(&tag->name, "body") == 0) {
        processor->tag = TAG_BODY;

        if (tag->type == TAG_CLOSE && !processor->script_tail &&
            processor_option_jscript_root(processor)) {
            istream_replace_add(processor->replace, tag->start, tag->start,
                                js_generate_tail(processor->pool));
            processor->script_tail = 1;
        }
    } else if (strref_cmp_literal(&tag->name, "head") == 0) {
        processor->tag = TAG_HEAD;
    } else if (strref_cmp_literal(&tag->name, "c:widget") == 0) {
        if (tag->type == TAG_CLOSE) {
            assert(processor->embedded_widget == NULL);
            return;
        }

        if ((processor->options & PROCESSOR_CONTAINER) == 0)
            return;

        processor->tag = TAG_WIDGET;
        processor->embedded_widget = p_malloc(processor->widget_pool,
                                              sizeof(*processor->embedded_widget));
        widget_init(processor->embedded_widget, NULL);
        processor->widget_params_length = 0;

        list_add(&processor->embedded_widget->siblings,
                 &processor->widget->children);
        processor->embedded_widget->parent = processor->widget;
    } else if (strref_cmp_literal(&tag->name, "script") == 0) {
        processor->tag = TAG_SCRIPT;
        processor->uri_base = processor->widget->class->old_style
            ? URI_BASE_WIDGET
            : URI_BASE_TEMPLATE;

        if (tag->type != TAG_CLOSE)
            processor_insert_jscript(processor, 0);
    } else if (!processor_option_quiet(processor) &&
               processor_option_rewrite_url(processor)) {
        if (strref_cmp_literal(&tag->name, "a") == 0) {
            processor->tag = TAG_A;
            processor->uri_base = processor->widget->class->old_style
                ? URI_BASE_FOCUS
                : URI_BASE_TEMPLATE;
        } else if (strref_cmp_literal(&tag->name, "form") == 0) {
            processor->tag = TAG_FORM;
            processor->uri_base = processor->widget->class->old_style
                ? URI_BASE_FOCUS
                : URI_BASE_TEMPLATE;
        } else if (strref_cmp_literal(&tag->name, "img") == 0) {
            processor->tag = TAG_IMG;
            processor->uri_base = processor->widget->class->old_style
                ? URI_BASE_WIDGET
                : URI_BASE_TEMPLATE;
        } else if (strref_cmp_literal(&tag->name, "iframe") == 0) {
            /* this isn't actually an IMG, but we are only interested
               in the SRC attribute */
            processor->tag = TAG_IMG;
            processor->uri_base = URI_BASE_TEMPLATE;
        } else {
            processor->tag = TAG_NONE;
        }
    } else {
        processor->tag = TAG_NONE;
    }
}

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
transform_uri_attribute(struct processor *processor,
                        const struct parser_attr *attr,
                        enum uri_base base)
{
    const char *uri;

    switch (base) {
    case URI_BASE_TEMPLATE:
        /* no need to rewrite the attribute */
        uri = NULL;
        break;

    case URI_BASE_WIDGET:
        uri = widget_absolute_uri(processor->pool,
                                  processor->widget,
                                  attr->value.data, attr->value.length);
        break;

    case URI_BASE_FOCUS:
        uri = widget_external_uri(processor->pool,
                                  processor->env->external_uri,
                                  processor->env->args,
                                  processor->widget,
                                  1,
                                  attr->value.data, attr->value.length,
                                  0, 0);
        break;

    case URI_BASE_PROXY:
        uri = widget_external_uri(processor->pool,
                                  processor->env->external_uri,
                                  processor->env->args,
                                  processor->widget,
                                  1,
                                  attr->value.data, attr->value.length,
                                  1, 1);
        break;
    }

    if (uri != NULL)
        replace_attribute_value(processor, attr,
                                istream_string_new(processor->pool,
                                                   uri));
}

static void
parser_widget_attr_finished(struct widget *widget,
                            pool_t pool,
                            const struct strref *name,
                            const struct strref *value)
{
    if (strref_cmp_literal(name, "type") == 0) {
        widget->class_name = strref_dup(pool, value);
    } else if (strref_cmp_literal(name, "href") == 0) {
        enum widget_type type = WIDGET_TYPE_BENG;
        const char *class_name = strref_dup(pool, value);

        if (strncmp(class_name, "raw:", 4) == 0) {
            type = WIDGET_TYPE_RAW;
            class_name += 4;
        } else if (strncmp(class_name, "google:", 7) == 0) {
            type = WIDGET_TYPE_GOOGLE_GADGET;
            class_name += 7;
        }

        widget->class = get_widget_class(pool, class_name, type);
    } else if (strref_cmp_literal(name, "id") == 0) {
        if (!strref_is_empty(value))
            widget_set_id(widget, pool, value);
    } else if (strref_cmp_literal(name, "display") == 0) {
        if (strref_cmp_literal(value, "inline") == 0)
            widget->display = WIDGET_DISPLAY_INLINE;
        if (strref_cmp_literal(value, "none") == 0)
            widget->display = WIDGET_DISPLAY_NONE;
        else if (strref_cmp_literal(value, "iframe") == 0)
            widget->display = WIDGET_DISPLAY_IFRAME;
        else if (strref_cmp_literal(value, "img") == 0)
            widget->display = WIDGET_DISPLAY_IMG;
        else
            widget->display = WIDGET_DISPLAY_NONE;
    } else if (strref_cmp_literal(name, "session") == 0) {
        if (strref_cmp_literal(value, "resource") == 0)
            widget->session = WIDGET_SESSION_RESOURCE;
        else if (strref_cmp_literal(value, "site") == 0)
            widget->session = WIDGET_SESSION_SITE;
    } else if (strref_cmp_literal(name, "tag") == 0)
        widget->decoration.tag = strref_dup(pool, value);
    else if (strref_cmp_literal(name, "style") == 0)
        widget->decoration.style = strref_dup(pool, value);
}

static void
processor_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    struct processor *processor = ctx;

    if (!processor_option_quiet(processor) &&
        (processor->options & PROCESSOR_JS_FILTER) != 0 &&
        attr->name.length > 2 &&
        attr->name.data[0] == 'o' && attr->name.data[1] == 'n' &&
        !strref_is_empty(&attr->value)) {
        istream_t value_stream = istream_memory_new(processor->pool,
                                                    strref_dup(processor->pool, &attr->value),
                                                    attr->value.length);
        replace_attribute_value(processor, attr,
                                js_filter_new(processor->pool,
                                              value_stream));
        return;
    }

    if (!processor_option_quiet(processor) &&
        processor->tag != TAG_NONE &&
        strref_cmp_literal(&attr->name, "c:base") == 0) {
        if (strref_cmp_literal(&attr->value, "widget") == 0)
            processor->uri_base = URI_BASE_WIDGET;
        else if (strref_cmp_literal(&attr->value, "focus") == 0)
            processor->uri_base = URI_BASE_FOCUS;
        else if (strref_cmp_literal(&attr->value, "proxy") == 0)
            processor->uri_base = URI_BASE_PROXY;
        else
            processor->uri_base = URI_BASE_TEMPLATE;
        /* XXX remove the whole attribute */
        return;
    }

    switch (processor->tag) {
    case TAG_NONE:
        break;

    case TAG_HEAD:
        break;

    case TAG_BODY:
        break;

    case TAG_WIDGET:
        assert(processor->embedded_widget != NULL);

        parser_widget_attr_finished(processor->embedded_widget,
                                    processor->widget_pool,
                                    &attr->name, &attr->value);
        break;

    case TAG_WIDGET_PARAM:
        assert(processor->embedded_widget != NULL);

        if (strref_cmp_literal(&attr->name, "name") == 0) {
            size_t length = attr->value.length;
            if (length > sizeof(processor->widget_param.name))
                length = sizeof(processor->widget_param.name);
            processor->widget_param.name_length = length;
            memcpy(processor->widget_param.name, attr->value.data, length);
        } else if (strref_cmp_literal(&attr->name, "value") == 0) {
            size_t length = attr->value.length;
            if (length > sizeof(processor->widget_param.value))
                length = sizeof(processor->widget_param.value);
            processor->widget_param.value_length = length;
            memcpy(processor->widget_param.value, attr->value.data, length);
        }

        break;

    case TAG_WIDGET_PATH_INFO:
        assert(processor->embedded_widget != NULL);

        if (strref_cmp_literal(&attr->name, "value") == 0) {
            processor->embedded_widget->path_info
                = strref_dup(processor->widget_pool, &attr->value);
        }

        break;

    case TAG_IMG:
        if (strref_cmp_literal(&attr->name, "src") == 0)
            transform_uri_attribute(processor, attr, processor->uri_base);
        break;

    case TAG_A:
        if (strref_cmp_literal(&attr->name, "href") == 0 &&
            !strref_starts_with_n(&attr->value, "#", 1) &&
            !strref_starts_with_n(&attr->value, "javascript:", 11))
            transform_uri_attribute(processor, attr, processor->uri_base);
        break;

    case TAG_FORM:
        if (strref_cmp_literal(&attr->name, "action") == 0)
            transform_uri_attribute(processor, attr, processor->uri_base);
        break;

    case TAG_SCRIPT:
        if (strref_cmp_literal(&attr->name, "src") == 0)
            transform_uri_attribute(processor, attr, processor->uri_base);
        break;
    }
}

static istream_t
embed_widget(struct processor *processor, struct processor_env *env,
             struct widget *widget)
{
    pool_t pool = processor->pool;

    if (widget->class_name == NULL &&
        (widget->class == NULL || widget->class->uri == NULL))
        return NULL;

    widget_copy_from_request(widget, env);
    if (!widget->from_request.proxy &&
        widget->from_request.proxy_ref == NULL &&
        processor->replace == NULL)
        return NULL;

    if (widget->from_request.proxy || widget->from_request.proxy_ref != NULL) {
        processor->response_sent = 1;
        env->widget_callback(pool, env, widget,
                             processor->response_handler.handler,
                             processor->response_handler.ctx,
                             processor->async_ref);
        parser_close(processor->parser);
        return NULL;
    } else {
        struct widget_stream *ws;
        istream_t hold;

        ws = widget_stream_new(pool);
        hold = istream_hold_new(pool, istream_catch_new(pool, ws->delayed));

        env->widget_callback(pool, env, widget,
                             &widget_stream_response_handler, ws,
                             &ws->async_ref);

        return hold;
    }
}

static istream_t
embed_decorate(pool_t pool, istream_t istream, const struct widget *widget)
{
    growing_buffer_t tag;
    const char *tag_name, *prefix;

    assert(istream != NULL);
    assert(!istream_has_handler(istream));

    tag_name = widget->decoration.tag;
    if (tag_name == NULL)
        tag_name = "div";
    else if (tag_name[0] == 0)
        return istream;

    tag = growing_buffer_new(pool, 256);
    growing_buffer_write_string(tag, "<");
    growing_buffer_write_string(tag, tag_name);
    growing_buffer_write_string(tag, " class=\"widget\"");

    prefix = widget_prefix(widget);
    if (prefix != NULL) {
        growing_buffer_write_string(tag, " id=\"beng_widget_");
        growing_buffer_write_string(tag, prefix);
        growing_buffer_write_string(tag, "\"");
    }

    if (widget->decoration.style != NULL) {
        growing_buffer_write_string(tag, " style=\"");
        growing_buffer_write_string(tag, widget->decoration.style);
        growing_buffer_write_string(tag, "\"");
    }

    growing_buffer_write_string(tag, ">");

    return istream_cat_new(pool,
                           growing_buffer_istream(tag),
                           istream,
                           istream_string_new(pool, p_strcat(pool, "</",
                                                             tag_name,
                                                             ">", NULL)),
                           NULL);
}

static istream_t
embed_element_finished(struct processor *processor)
{
    struct widget *widget;
    istream_t istream;

    widget = processor->embedded_widget;
    processor->embedded_widget = NULL;

    if (processor->widget_params_length > 0)
        widget->query_string = p_strndup(processor->pool,
                                         processor->widget_params,
                                         processor->widget_params_length);

    istream = embed_widget(processor, processor->env, widget);
    if (istream != NULL && widget->class != NULL &&
        widget->class->old_style)
        istream = embed_decorate(processor->pool, istream, widget);

    return istream;
}

static void
body_element_finished(struct processor *processor, const struct parser_tag *tag)
{

    if (tag->type != TAG_CLOSE)
        processor_insert_jscript(processor, tag->end);
}

static void
processor_parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    struct processor *processor = ctx;

    if (processor->tag == TAG_HEAD) {
        if (tag->type == TAG_OPEN)
            processor_insert_jscript(processor, tag->end);
    } else if (processor->tag == TAG_BODY) {
        body_element_finished(processor, tag);
    } else if (processor->tag == TAG_WIDGET) {
        istream_t istream;

        if (tag->type == TAG_OPEN || tag->type == TAG_SHORT)
            processor->widget_start_offset = tag->start;
        else if (processor->embedded_widget == NULL)
            return;

        assert(processor->embedded_widget != NULL);

        if (tag->type == TAG_OPEN)
            return;

        istream = embed_element_finished(processor);
        assert(istream == NULL || processor->replace != NULL);

        if (processor->replace != NULL)
            processor_replace_add(processor, processor->widget_start_offset,
                                  tag->end, istream);
    } else if (processor->tag == TAG_WIDGET_PARAM) {
        struct pool_mark mark;
        const char *p;
        size_t length;

        assert(processor->embedded_widget != NULL);

        /* XXX escape */

        if (processor->widget_param.name_length == 0)
            return;

        pool_mark(tpool, &mark);

        p = args_format_n(tpool, NULL,
                          p_strndup(tpool, processor->widget_param.name,
                                    processor->widget_param.name_length),
                          processor->widget_param.value,
                          processor->widget_param.value_length,
                          NULL, NULL, 0, NULL, NULL, 0, NULL);
        length = strlen(p);

        if (processor->widget_params_length + 1 + length >= sizeof(processor->widget_params)) {
            pool_rewind(tpool, &mark);
            return;
        }

        if (processor->widget_params_length > 0)
            processor->widget_params[processor->widget_params_length++] = '&';
        memcpy(processor->widget_params + processor->widget_params_length, p, length);
        processor->widget_params_length += length;

        pool_rewind(tpool, &mark);
    } else if (processor->tag == TAG_SCRIPT &&
               tag->type == TAG_OPEN) {
        processor->in_script = 1;
        parser_script(processor->parser);

        if ((processor->options & PROCESSOR_JS_FILTER) != 0) {
            processor->script = growing_buffer_new(processor->pool, 4096);
            processor->script_start_offset = tag->end;
        } else {
            processor->script = NULL;
        }
    }
}

static size_t
processor_parser_cdata(const char *p, size_t length, int escaped, void *ctx)
{
    struct processor *processor = ctx;

    (void)escaped;

    if (processor->in_script && processor->script != NULL)
        growing_buffer_write_buffer(processor->script, p, length);

    return length;
}

static void
processor_parser_eof(void *ctx, off_t length)
{
    struct processor *processor = ctx;

    assert(processor->parser != NULL);

    processor->parser = NULL;

    if (!processor->script_tail &&
        processor_option_jscript_root(processor)) {
        istream_replace_add(processor->replace, length, length,
                            js_generate_tail(processor->pool));
    }

    if (processor->replace != NULL)
        istream_replace_finish(processor->replace);

    if (!processor->response_sent) {
        processor->response_sent = 1;
        http_response_handler_invoke_message(&processor->response_handler, processor->pool,
                                             HTTP_STATUS_NOT_FOUND,
                                             "Widget not found");
    }

    pool_unref(processor->pool);
}

static void
processor_parser_abort(void *ctx)
{
    struct processor *processor = ctx;

    processor->parser = NULL;

    if (!processor->response_sent) {
        processor->response_sent = 1;
        http_response_handler_invoke_abort(&processor->response_handler);
    }

    pool_unref(processor->pool);
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
