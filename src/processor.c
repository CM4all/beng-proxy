/*
 * Process CM4all commands in a HTML stream, e.g. embeddings.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "processor.h"
#include "parser.h"
#include "strutil.h"
#include "replace.h"
#include "args.h"
#include "widget.h"
#include "growing-buffer.h"
#include "js-filter.h"
#include "js-generator.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

typedef struct processor *processor_t;

struct processor {
    struct istream output;
    istream_t input;
    int had_input;

    pool_t widget_pool;

    struct widget *widget;
    struct processor_env *env;
    unsigned options;

    struct replace replace;

    struct parser parser;
    int in_html, in_head, in_body;
    off_t end_of_body;
    enum {
        TAG_NONE,
        TAG_BODY,
        TAG_WIDGET,
        TAG_WIDGET_PATH_INFO,
        TAG_WIDGET_PARAM,
        TAG_A,
        TAG_FORM,
        TAG_IMG,
        TAG_SCRIPT,
    } tag;
    off_t widget_start_offset;
    struct widget *embedded_widget;
    struct {
        size_t name_length, value_length;
        char name[64];
        char value[64];
    } widget_param;
    char widget_params[512];
    size_t widget_params_length;

    growing_buffer_t script;
    off_t script_start_offset;
};


static int
processor_option_quiet(const struct processor *processor)
{
    return (processor->options & PROCESSOR_QUIET) != 0;
}

static int
processor_option_body(const struct processor *processor)
{
    return (processor->options & PROCESSOR_BODY) != 0;
}

static int
processor_option_jscript(const struct processor *processor)
{
    return (processor->options & (PROCESSOR_JSCRIPT|PROCESSOR_QUIET))
        == PROCESSOR_JSCRIPT;
}

static int
processor_option_jscript_root(const struct processor *processor)
{
    return (processor->options & (PROCESSOR_JSCRIPT|PROCESSOR_JSCRIPT_ROOT|PROCESSOR_QUIET))
        == (PROCESSOR_JSCRIPT|PROCESSOR_JSCRIPT_ROOT);
}

static inline int
processor_is_quiet(processor_t processor)
{
    return processor->replace.quiet ||
        (processor_option_body(processor) && !processor->in_body);
}


static void
processor_close(processor_t processor)
{
    assert(processor != NULL);

    processor->replace.output = NULL;
    replace_destroy(&processor->replace);

    if (processor->input != NULL)
        istream_free_unref_handler(&processor->input);

    pool_unref(processor->output.pool);
}

static void
processor_abort(processor_t processor)
{
    assert(processor != NULL);

    processor->replace.output = NULL;
    replace_destroy(&processor->replace);

    if (processor->input != NULL)
        istream_free_unref_handler(&processor->input);

    istream_invoke_abort(&processor->output);

    pool_unref(processor->output.pool);
}

/*
 * istream implementation
 *
 */

static inline processor_t
istream_to_processor(istream_t istream)
{
    return (processor_t)(((char*)istream) - offsetof(struct processor, output));
}

static off_t
processor_output_stream_available(istream_t istream, int partial)
{
    processor_t processor = istream_to_processor(istream);

    if (partial)
        return replace_available(&processor->replace);
    else
        return (off_t)-1;
}

static void
processor_output_stream_read(istream_t istream)
{
    processor_t processor = istream_to_processor(istream);

    if (processor->input != NULL) {
        do {
            processor->had_input = 0;
            istream_read(processor->input);
        } while (processor->input != NULL && processor->had_input);
    } else
        replace_read(&processor->replace);
}

static void
processor_output_stream_close(istream_t istream)
{
    processor_t processor = istream_to_processor(istream);

    processor_abort(processor);
}

static const struct istream processor_output_stream = {
    .available = processor_output_stream_available,
    .read = processor_output_stream_read,
    .close = processor_output_stream_close,
};


static void
replace_output_eof(struct istream *istream)
{
    processor_t processor = istream_to_processor(istream_struct_cast(istream));

    assert(processor->input == NULL);

    processor_close(processor);
}


/*
 * istream handler
 *
 */

static size_t
processor_input_data(const void *data, size_t length, void *ctx)
{
    processor_t processor = ctx;
    off_t position;
    size_t nbytes;

    assert(processor != NULL);
    assert(data != NULL);
    assert(length > 0);

    position = processor->replace.source_length;

    nbytes = replace_feed(&processor->replace, data, length);
    if (nbytes == 0)
        return 0;

    parser_feed(&processor->parser, position, (const char*)data, nbytes);

    if (!processor->replace.quiet &&
        processor->replace.source_length >= 8 * 1024 * 1024) {
        daemon_log(2, "file too large for processor\n");
        processor_abort(processor);
        return 0;
    }

    processor->had_input = 1;

    return (size_t)nbytes;
}

static void
processor_input_eof(void *ctx)
{
    processor_t processor = ctx;

    assert(processor != NULL);
    assert(processor->input != NULL);

    istream_clear_unref(&processor->input);

    if (processor->end_of_body != (off_t)-1) {
        /* remove everything between closing body tag and end of
           file */
        assert(processor_option_body(processor));

        replace_add(&processor->replace, processor->end_of_body,
                    processor->replace.source_length, NULL);
    } else if (processor_option_body(processor) &&
               processor->in_html && !processor->in_body) {
        /* no body */

        replace_add(&processor->replace, 0,
                    processor->replace.source_length,
                    istream_string_new(processor->output.pool,
                                       "<!-- the widget has no HTML body -->"));
    }

    replace_eof(&processor->replace);
}

static void
processor_input_abort(void *ctx)
{
    processor_t processor = ctx;

    assert(processor->input != NULL);

    istream_clear_unref(&processor->input);

    processor_abort(processor); /* XXX */
}

static const struct istream_handler processor_input_handler = {
    .data = processor_input_data,
    .eof = processor_input_eof,
    .abort = processor_input_abort,
};


static istream_t
processor_jscript(processor_t processor)
{
    growing_buffer_t gb = growing_buffer_new(processor->output.pool, 512);

    assert(processor_option_jscript(processor));

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n");

    if (processor_option_jscript_root(processor))
        js_generate_root_widget(gb, strmap_get(processor->env->args, "session"));

    js_generate_widget(gb, processor->widget, processor->output.pool);

    growing_buffer_write_string(gb, "</script>\n");

    return growing_buffer_istream(gb);
}

/*
 * constructor
 *
 */

static void
processor_parser_init(processor_t processor);

istream_t
processor_new(pool_t pool, istream_t istream,
              struct widget *widget,
              struct processor_env *env,
              unsigned options)
{
    processor_t processor;
    const char *path, *prefix;

    assert(istream != NULL);
    assert(!istream_has_handler(istream));
    assert(widget != NULL);

    path = widget_path(pool, widget);
    if (path == NULL)
        path = "";
    istream = istream_subst_new(pool, istream,
                                "&c:path;", path);

    prefix = widget_prefix(pool, widget);
    if (prefix == NULL)
        prefix = "";
    istream = istream_subst_new(pool, istream,
                                "&c:prefix;", prefix);

    if (env->absolute_uri != NULL)
        istream = istream_subst_new(pool, istream,
                                    "&c:uri;", env->absolute_uri);

#ifdef NDEBUG
    pool_ref(pool);
#else
    pool = pool_new_linear(pool, "processor", 16384);
#endif

    processor = p_malloc(pool, sizeof(*processor));

    processor->output = processor_output_stream;
    processor->output.pool = pool;

    processor->widget_pool = env->pool;

    istream_assign_ref_handler(&processor->input, istream,
                               &processor_input_handler, processor,
                               0);

    processor->widget = widget;
    processor->env = env;
    processor->options = options;

    replace_init(&processor->replace, pool,
                 &processor->output,
                 replace_output_eof,
                 processor_option_quiet(processor));

    processor_parser_init(processor);

    processor->in_html = 0;
    processor->in_head = 0;
    processor->in_body = 0;
    processor->end_of_body = (off_t)-1;
    processor->embedded_widget = NULL;
    processor->script = NULL;

    if (processor_option_jscript(processor) &&
        processor_option_body(processor))
        replace_add(&processor->replace, 0, 0,
                    processor_jscript(processor));

    return istream_struct_cast(&processor->output);
}


static void
processor_finish_script(processor_t processor, off_t end)
{
    assert(processor->script != NULL);
    assert(processor->script_start_offset <= end);

    if (processor->script_start_offset < end)
        replace_add(&processor->replace,
                    processor->script_start_offset, end,
                    js_filter_new(processor->output.pool,
                                  growing_buffer_istream(processor->script)));

    processor->script = NULL;
}

/*
 * parser callbacks
 *
 */

static void
parser_element_start_in_body(processor_t processor,
                             enum parser_tag_type type,
                             const struct strref *name)
{
    if (strref_cmp_literal(name, "a") == 0) {
        processor->tag = TAG_A;
    } else if (strref_cmp_literal(name, "form") == 0) {
        processor->tag = TAG_FORM;
    } else if (strref_cmp_literal(name, "img") == 0) {
        processor->tag = TAG_IMG;
    } else if (strref_cmp_literal(name, "script") == 0) {
        if (type == TAG_OPEN)
            processor->tag = TAG_SCRIPT;
    } else {
        processor->tag = TAG_NONE;
    }
}

static void
parser_element_start_in_widget(processor_t processor,
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
    processor_t processor = ctx;

    if (processor->script != NULL)
        processor_finish_script(processor, tag->start);

    if (processor->embedded_widget != NULL) {
        parser_element_start_in_widget(processor, tag->type, &tag->name);
        return;
    }

    if (strref_cmp_literal(&tag->name, "body") == 0) {
        processor->tag = TAG_BODY;
    } else if (strref_cmp_literal(&tag->name, "html") == 0) {
        processor->in_html = 1;
        processor->tag = TAG_NONE;
    } else if (processor->in_html && !processor->in_head &&
               !processor->in_body &&
               processor_option_jscript(processor) &&
               !processor_option_body(processor) &&
               tag->type == TAG_CLOSE &&
               strref_cmp_literal(&tag->name, "head") == 0) {
        replace_add(&processor->replace,
                    tag->start, tag->start,
                    processor_jscript(processor));
        processor->in_head = 1;
    } else if (processor->end_of_body != (off_t)-1) {
        /* we have left the body, ignore the rest */
        assert(processor_option_body(processor));

        processor->tag = TAG_NONE;
    } else if (strref_cmp_literal(&tag->name, "c:widget") == 0) {
        if (tag->type == TAG_CLOSE) {
            assert(processor->embedded_widget == NULL);
            return;
        }

        processor->tag = TAG_WIDGET;
        processor->embedded_widget = p_malloc(processor->widget_pool,
                                              sizeof(*processor->embedded_widget));
        widget_init(processor->embedded_widget, NULL);
        processor->widget_params_length = 0;

        list_add(&processor->embedded_widget->siblings,
                 &processor->widget->children);
        processor->embedded_widget->parent = processor->widget;
    } else if (processor_is_quiet(processor)) {
        /* since we are not going to print anything, we don't need to
           parse the rest anyway */

        if (processor->in_html)
            processor->tag = TAG_NONE;
        else {
            /* fall back to returning everything if there is no HTML
               tag */
            processor->in_body = 1;
            parser_element_start_in_body(processor, tag->type, &tag->name);
        }
    } else {
        parser_element_start_in_body(processor, tag->type, &tag->name);
    }
}

static void
replace_attribute_value(processor_t processor,
                        const struct parser_attr *attr,
                        istream_t value)
{
    replace_add(&processor->replace,
                attr->value_start, attr->value_end,
                value);
}

static void
make_url_attribute_absolute(processor_t processor,
                            const struct parser_attr *attr)
{
    const char *new_uri = widget_absolute_uri(processor->output.pool,
                                              processor->widget,
                                              attr->value.data,
                                              attr->value.length);
    if (new_uri != NULL)
        replace_attribute_value(processor, attr,
                                istream_string_new(processor->output.pool,
                                                   new_uri));
}

static void
transform_url_attribute(processor_t processor,
                        const struct parser_attr *attr)
{
    const char *new_uri
        = widget_external_uri(processor->output.pool,
                              processor->env->external_uri,
                              processor->env->args,
                              processor->widget,
                              attr->value.data,
                              attr->value.length);
    if (new_uri == NULL)
        return;

    replace_attribute_value(processor, attr,
                            istream_string_new(processor->output.pool,
                                               new_uri));
}

static void
parser_widget_attr_finished(struct widget *widget,
                            pool_t pool,
                            const struct strref *name,
                            const struct strref *value)
{
    if (strref_cmp_literal(name, "href") == 0) {
        const char *class_name = strref_dup(pool, value);
        widget->class = get_widget_class(pool, class_name);
    } else if (strref_cmp_literal(name, "id") == 0)
        widget->id = strref_dup(pool, value);
    else if (strref_cmp_literal(name, "display") == 0) {
        if (strref_cmp_literal(value, "inline") == 0)
            widget->display = WIDGET_DISPLAY_INLINE;
        else if (strref_cmp_literal(value, "iframe") == 0)
            widget->display = WIDGET_DISPLAY_IFRAME;
        else if (strref_cmp_literal(value, "img") == 0)
            widget->display = WIDGET_DISPLAY_IMG;
    } else if (strref_cmp_literal(name, "session") == 0) {
        if (strref_cmp_literal(value, "resource") == 0)
            widget->session = WIDGET_SESSION_RESOURCE;
        else if (strref_cmp_literal(value, "site") == 0)
            widget->session = WIDGET_SESSION_SITE;
    } else if (strref_cmp_literal(name, "tag") == 0)
        widget->decoration.tag = strref_dup(pool, value);
    else if (strref_cmp_literal(name, "width") == 0)
        widget->decoration.width = strref_dup(pool, value);
    else if (strref_cmp_literal(name, "height") == 0)
        widget->decoration.height = strref_dup(pool, value);
    else if (strref_cmp_literal(name, "style") == 0)
        widget->decoration.style = strref_dup(pool, value);
}

static void
processor_parser_attr_finished(const struct parser_attr *attr, void *ctx)
{
    processor_t processor = ctx;

    if (!processor_is_quiet(processor) &&
        attr->name.length > 2 &&
        attr->name.data[0] == 'o' && attr->name.data[1] == 'n' &&
        !strref_is_empty(&attr->value)) {
        istream_t value_stream = istream_memory_new(processor->output.pool,
                                                    strref_dup(processor->output.pool, &attr->value),
                                                    attr->value.length);
        replace_attribute_value(processor, attr,
                                js_filter_new(processor->output.pool,
                                              value_stream));
        return;
    }

    switch (processor->tag) {
    case TAG_NONE:
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
            make_url_attribute_absolute(processor, attr);
        break;

    case TAG_A:
        if (strref_cmp_literal(&attr->name, "href") == 0)
            transform_url_attribute(processor, attr);
        break;

    case TAG_FORM:
        if (strref_cmp_literal(&attr->name, "action") == 0)
            transform_url_attribute(processor, attr);
        break;

    case TAG_SCRIPT:
        break;
    }
}

static istream_t
embed_widget(pool_t pool, struct processor_env *env, struct widget *widget)
{
    if (widget->class == NULL || widget->class->uri == NULL)
        return istream_string_new(pool, "Error: no widget class specified");

    widget_copy_from_request(widget, env);
    widget_determine_real_uri(pool, widget);

    return env->widget_callback(pool, env, widget);
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
    growing_buffer_write_string(tag, " class=\"embed\"");

    prefix = widget_prefix(pool, widget);
    if (prefix != NULL) {
        growing_buffer_write_string(tag, " id=\"beng_widget_");
        growing_buffer_write_string(tag, prefix);
        growing_buffer_write_string(tag, "\"");
    }

    growing_buffer_write_string(tag, " style='overflow:auto; margin:5pt; border:1px dotted red;");

    if (widget->decoration.width != NULL) {
        growing_buffer_write_string(tag, "width:");
        growing_buffer_write_string(tag, widget->decoration.width);
        growing_buffer_write_string(tag, ";");
    }

    if (widget->decoration.height != NULL) {
        growing_buffer_write_string(tag, "height:");
        growing_buffer_write_string(tag, widget->decoration.height);
        growing_buffer_write_string(tag, ";");
    }

    if (widget->decoration.style != NULL)
        growing_buffer_write_string(tag, widget->decoration.style);

    growing_buffer_write_string(tag, "'>");

    return istream_cat_new(pool,
                           growing_buffer_istream(tag),
                           istream,
                           istream_string_new(pool, p_strcat(pool, "</",
                                                             tag_name,
                                                             ">", NULL)),
                           NULL);
}

static istream_t
embed_element_finished(processor_t processor)
{
    struct widget *widget;
    istream_t istream;

    widget = processor->embedded_widget;
    processor->embedded_widget = NULL;

    if (processor->widget_params_length > 0)
        widget->query_string = p_strndup(processor->output.pool,
                                         processor->widget_params,
                                         processor->widget_params_length);

    istream = embed_widget(processor->output.pool, processor->env, widget);
    if (istream != NULL && !processor_option_quiet(processor))
        istream = embed_decorate(processor->output.pool, istream, widget);

    return istream;
}

static void
body_element_finished(processor_t processor, const struct parser_tag *tag)
{

    if (tag->type != TAG_CLOSE) {
        if (processor->in_body)
            return;

        if (processor_option_body(processor))
            replace_add(&processor->replace, 0, tag->end, NULL);

        processor->in_body = 1;
    } else {
        if (!processor_option_body(processor) ||
            processor->end_of_body != (off_t)-1)
            return;

        processor->end_of_body = tag->start;
    }
}

static void
processor_parser_tag_finished(const struct parser_tag *tag, void *ctx)
{
    processor_t processor = ctx;

    if (processor->tag == TAG_BODY) {
        body_element_finished(processor, tag);
    } else if (processor->tag == TAG_WIDGET) {
        if (tag->type == TAG_OPEN || tag->type == TAG_SHORT)
            processor->widget_start_offset = tag->start;
        else if (processor->embedded_widget == NULL)
            return;

        assert(processor->embedded_widget != NULL);

        if (tag->type == TAG_OPEN)
            return;
        
        istream_t istream = embed_element_finished(processor);
        replace_add(&processor->replace, processor->widget_start_offset,
                    tag->end, istream);
    } else if (processor->tag == TAG_WIDGET_PARAM) {
        assert(processor->embedded_widget != NULL);

        /* XXX escape */

        if (processor->widget_param.name_length == 0 ||
            processor->widget_params_length + 1 +
            processor->widget_param.name_length + 1 +
            processor->widget_param.value_length >= sizeof(processor->widget_params))
            return;

        if (processor->widget_params_length > 0)
            processor->widget_params[processor->widget_params_length++] = '&';

        memcpy(processor->widget_params + processor->widget_params_length,
               processor->widget_param.name,
               processor->widget_param.name_length);
        processor->widget_params_length += processor->widget_param.name_length;

        processor->widget_params[processor->widget_params_length++] = '=';

        memcpy(processor->widget_params + processor->widget_params_length,
               processor->widget_param.value,
               processor->widget_param.value_length);
        processor->widget_params_length += processor->widget_param.value_length;
    } else if (processor->tag == TAG_SCRIPT) {
        processor->script = growing_buffer_new(processor->output.pool, 4096);
        processor->script_start_offset = tag->end;
    }
}

static void
processor_parser_cdata(const char *p, size_t length, int escaped, void *ctx)
{
    processor_t processor = ctx;

    (void)escaped;

    if (processor->script != NULL)
        growing_buffer_write_buffer(processor->script, p, length);
}

static const struct parser_handler processor_parser_handler = {
    .tag_start = processor_parser_tag_start,
    .tag_finished = processor_parser_tag_finished,
    .attr_finished = processor_parser_attr_finished,
    .cdata = processor_parser_cdata,
};

static void
processor_parser_init(processor_t processor)
{
    parser_init(&processor->parser, &processor_parser_handler, processor);
}
