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
    size_t nbytes;

    assert(processor != NULL);
    assert(data != NULL);
    assert(length > 0);

    processor->parser.position = processor->replace.source_length;

    nbytes = replace_feed(&processor->replace, data, length);
    if (nbytes == 0)
        return 0;

    parser_feed(&processor->parser, (const char*)data, nbytes);

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
        assert((processor->options & PROCESSOR_BODY) != 0);

        replace_add(&processor->replace, processor->end_of_body,
                    processor->replace.source_length, NULL);
    } else if ((processor->options & PROCESSOR_BODY) != 0 &&
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


static void
growing_buffer_write_jscript_string(growing_buffer_t gb, const char *s)
{
    if (s == NULL)
        growing_buffer_write_string(gb, "null");
    else {
        growing_buffer_write_string(gb, "\"");
        growing_buffer_write_string(gb, s); /* XXX escape */
        growing_buffer_write_string(gb, "\"");
    }
}

static void
widget_jscript(growing_buffer_t gb, const struct widget *widget, pool_t pool)
{
    const char *prefix, *parent_prefix;

    prefix = widget_prefix(pool, widget);
    if (prefix == NULL)
        return;

    growing_buffer_write_string(gb, "var ");
    growing_buffer_write_string(gb, prefix);
    growing_buffer_write_string(gb, "widget = ");

    if (widget->parent == NULL) {
        growing_buffer_write_string(gb, "rootWidget;\n");
    } else {
        growing_buffer_write_string(gb, "new beng_widget(");

        parent_prefix = widget_prefix(pool, widget->parent);
        assert(parent_prefix != NULL);

        growing_buffer_write_string(gb, parent_prefix);
        growing_buffer_write_string(gb, "widget, ");
        growing_buffer_write_jscript_string(gb, widget->id);
        growing_buffer_write_string(gb, ");\n");
    }
}

static istream_t
processor_jscript(processor_t processor)
{
    growing_buffer_t gb = growing_buffer_new(processor->output.pool, 512);

    assert(processor_option_jscript(processor));

    growing_buffer_write_string(gb, "<script type=\"text/javascript\">\n");

    if (processor_option_jscript_root(processor)) {
        const char *session_id;

        growing_buffer_write_string(gb, "var rootWidget = new beng_root_widget(beng_proxy(\"");

        session_id = strmap_get(processor->env->args, "session");
        if (session_id != NULL)
            growing_buffer_write_string(gb, session_id);

        growing_buffer_write_string(gb, "\"));\n");
    }

    widget_jscript(gb, processor->widget, processor->output.pool);

    growing_buffer_write_string(gb, "</script>\n");

    return growing_buffer_istream(gb);
}

/*
 * constructor
 *
 */

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
                 (options & PROCESSOR_QUIET) != 0);

    parser_init(&processor->parser);

    processor->in_html = 0;
    processor->in_head = 0;
    processor->in_body = 0;
    processor->end_of_body = (off_t)-1;
    processor->embedded_widget = NULL;
    processor->script = NULL;

    if ((processor->options & (PROCESSOR_JSCRIPT|PROCESSOR_BODY|PROCESSOR_QUIET)) == (PROCESSOR_JSCRIPT|PROCESSOR_BODY))
        replace_add(&processor->replace, 0, 0,
                    processor_jscript(processor));

    return istream_struct_cast(&processor->output);
}

static inline int
processor_is_quiet(processor_t processor)
{
    return processor->replace.quiet ||
        ((processor->options & PROCESSOR_BODY) != 0 && !processor->in_body);
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

static inline processor_t
parser_to_processor(struct parser *parser)
{
    return (processor_t)(((char*)parser) - offsetof(struct processor, parser));
}

static void
parser_element_start_in_body(processor_t processor, struct parser *parser)
{
    if (parser->element_name_length == 1 &&
        parser->element_name[0] == 'a') {
        processor->tag = TAG_A;
    } else if (parser->element_name_length == 4 &&
               memcmp(parser->element_name, "form", 4) == 0) {
        processor->tag = TAG_FORM;
    } else if (parser->element_name_length == 3 &&
               memcmp(parser->element_name, "img", 3) == 0) {
        processor->tag = TAG_IMG;
    } else if (parser->element_name_length == 6 &&
               memcmp(parser->element_name, "script", 6) == 0) {
        if (parser->tag_type == TAG_OPEN)
            processor->tag = TAG_SCRIPT;
    } else {
        processor->tag = TAG_NONE;
    }
}

static void
parser_element_start_in_widget(processor_t processor, struct parser *parser)
{
    if (parser->element_name_length == 8 &&
        memcmp(parser->element_name, "c:widget", 8) == 0) {
        if (parser->tag_type == TAG_CLOSE)
            processor->tag = TAG_WIDGET;
    } else if (parser->element_name_length == 9 &&
               memcmp(parser->element_name, "path-info", 9) == 0) {
        processor->tag = TAG_WIDGET_PATH_INFO;
    } else if (parser->element_name_length == 5 &&
               memcmp(parser->element_name, "param", 5) == 0) {
        processor->tag = TAG_WIDGET_PARAM;
        processor->widget_param.name_length = 0;
        processor->widget_param.value_length = 0;
    } else {
        processor->tag = TAG_NONE;
    }
}

void
parser_element_start(struct parser *parser)
{
    processor_t processor = parser_to_processor(parser);

    if (processor->script != NULL)
        processor_finish_script(processor, processor->parser.element_offset);

    if (processor->embedded_widget != NULL) {
        parser_element_start_in_widget(processor, parser);
        return;
    }

    if (parser->element_name_length == 4 &&
        memcmp(parser->element_name, "body", 4) == 0) {
        processor->tag = TAG_BODY;
    } else if (parser->element_name_length == 4 &&
               memcmp(parser->element_name, "html", 4) == 0) {
        processor->in_html = 1;
        processor->tag = TAG_NONE;
    } else if (processor->in_html && !processor->in_head &&
               !processor->in_body &&
               (processor->options & (PROCESSOR_JSCRIPT|PROCESSOR_BODY|PROCESSOR_QUIET)) == PROCESSOR_JSCRIPT &&
               parser->element_name_length == 4 &&
               parser->tag_type == TAG_CLOSE &&
               memcmp(parser->element_name, "head", 4) == 0) {
        replace_add(&processor->replace,
                    processor->parser.element_offset,
                    processor->parser.element_offset,
                    processor_jscript(processor));
        processor->in_head = 1;
    } else if (processor->end_of_body != (off_t)-1) {
        /* we have left the body, ignore the rest */
        assert((processor->options & PROCESSOR_BODY) != 0);

        processor->tag = TAG_NONE;
    } else if (parser->element_name_length == 8 &&
        memcmp(parser->element_name, "c:widget", 8) == 0) {

        if (parser->tag_type == TAG_CLOSE) {
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
            parser_element_start_in_body(processor, parser);
        }
    } else {
        parser_element_start_in_body(processor, parser);
    }
}

static void
replace_attribute_value(processor_t processor, istream_t value)
{
    assert(processor->parser.state == PARSER_ATTR_VALUE ||
           processor->parser.state == PARSER_ATTR_VALUE_COMPAT);

    replace_add(&processor->replace,
                processor->parser.attr_value_start,
                processor->parser.attr_value_end,
                value);
}

static void
make_url_attribute_absolute(processor_t processor)
{
    const char *new_uri = widget_absolute_uri(processor->output.pool,
                                              processor->widget,
                                              processor->parser.attr_value,
                                              processor->parser.attr_value_length);
    if (new_uri != NULL)
        replace_attribute_value(processor,
                                istream_string_new(processor->output.pool,
                                                   new_uri));
}

static void
transform_url_attribute(processor_t processor)
{
    const char *new_uri
        = widget_external_uri(processor->output.pool,
                              processor->env->external_uri,
                              processor->env->args,
                              processor->widget,
                              processor->parser.attr_value,
                              processor->parser.attr_value_length);
    if (new_uri == NULL)
        return;

    replace_attribute_value(processor,
                            istream_string_new(processor->output.pool,
                                               new_uri));
}

static void
parser_widget_attr_finished(struct parser *parser, struct widget *widget,
                            pool_t pool)
{
    if (parser->attr_name_length == 4 &&
        memcmp(parser->attr_name, "href", 4) == 0) {
        const char *class_name = p_strndup(pool, parser->attr_value,
                                           parser->attr_value_length);
        widget->class = get_widget_class(pool, class_name);
    } else if (parser->attr_name_length == 2 &&
             memcmp(parser->attr_name, "id", 2) == 0)
        widget->id = p_strndup(pool, parser->attr_value,
                               parser->attr_value_length);
    else if (parser->attr_name_length == 7 &&
             memcmp(parser->attr_name, "display", 7) == 0) {
        if (parser->attr_value_length == 6 &&
            memcmp(parser->attr_value, "inline", 6) == 0)
            widget->display = WIDGET_DISPLAY_INLINE;
        else if (parser->attr_value_length == 6 &&
                 memcmp(parser->attr_value, "iframe", 6) == 0)
            widget->display = WIDGET_DISPLAY_IFRAME;
        else if (parser->attr_value_length == 3 &&
                 memcmp(parser->attr_value, "img", 3) == 0)
            widget->display = WIDGET_DISPLAY_IMG;
    } else if (parser->attr_name_length == 7 &&
               memcmp(parser->attr_name, "session", 7) == 0) {
        if (parser->attr_value_length == 8 &&
            memcmp(parser->attr_value, "resource", 8) == 0)
            widget->session = WIDGET_SESSION_RESOURCE;
        else if (parser->attr_value_length == 4 &&
                 memcmp(parser->attr_value, "site", 4) == 0)
            widget->session = WIDGET_SESSION_SITE;
    } else if (parser->attr_name_length == 3 &&
               memcmp(parser->attr_name, "tag", 3) == 0)
        widget->decoration.tag = p_strndup(pool, parser->attr_value,
                                           parser->attr_value_length);
    else if (parser->attr_name_length == 5 &&
             memcmp(parser->attr_name, "width", 5) == 0)
        widget->decoration.width = p_strndup(pool, parser->attr_value,
                                             parser->attr_value_length);
    else if (parser->attr_name_length == 6 &&
             memcmp(parser->attr_name, "height", 6) == 0)
        widget->decoration.height = p_strndup(pool, parser->attr_value,
                                              parser->attr_value_length);
    else if (parser->attr_name_length == 5 &&
             memcmp(parser->attr_name, "style", 5) == 0)
        widget->decoration.style = p_strndup(pool, parser->attr_value,
                                             parser->attr_value_length);
}

void
parser_attr_finished(struct parser *parser)
{
    processor_t processor = parser_to_processor(parser);

    if (!processor_is_quiet(processor) &&
        parser->attr_name_length > 2 &&
        parser->attr_name[0] == 'o' &&
        parser->attr_name[1] == 'n' &&
        parser->attr_value_length > 0) {
        char *value = p_memdup(processor->output.pool,
                               parser->attr_value,
                               parser->attr_value_length);
        istream_t value_stream = istream_memory_new(processor->output.pool,
                                                    value,
                                                    parser->attr_value_length);
        replace_attribute_value(processor,
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

        parser_widget_attr_finished(parser, processor->embedded_widget,
                                    processor->widget_pool);
        break;

    case TAG_WIDGET_PARAM:
        assert(processor->embedded_widget != NULL);

        if (parser->attr_name_length == 4 &&
            memcmp(parser->attr_name, "name", 4) == 0) {
            if (parser->attr_value_length > sizeof(processor->widget_param.name))
                parser->attr_value_length = sizeof(processor->widget_param.name);
            processor->widget_param.name_length = parser->attr_value_length;
            memcpy(processor->widget_param.name, parser->attr_value,
                   parser->attr_value_length);
        } else if (parser->attr_name_length == 5 &&
                   memcmp(parser->attr_name, "value", 5) == 0) {
            if (parser->attr_value_length > sizeof(processor->widget_param.value))
                parser->attr_value_length = sizeof(processor->widget_param.value);
            processor->widget_param.value_length = parser->attr_value_length;
            memcpy(processor->widget_param.value, parser->attr_value,
                   parser->attr_value_length);
        }

        break;

    case TAG_WIDGET_PATH_INFO:
        assert(processor->embedded_widget != NULL);

        if (parser->attr_name_length == 5 &&
            memcmp(parser->attr_name, "value", 5) == 0) {
            processor->embedded_widget->path_info
                = p_strndup(processor->widget_pool, parser->attr_value,
                            parser->attr_value_length);
        }

        break;

    case TAG_IMG:
        if (parser->attr_name_length == 3 &&
            memcmp(parser->attr_name, "src", 3) == 0)
            make_url_attribute_absolute(processor);
        break;

    case TAG_A:
        if (parser->attr_name_length == 4 &&
            memcmp(parser->attr_name, "href", 4) == 0)
            transform_url_attribute(processor);
        break;

    case TAG_FORM:
        if (parser->attr_name_length == 6 &&
            memcmp(parser->attr_name, "action", 6) == 0)
            transform_url_attribute(processor);
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

    widget_determine_real_uri(pool, env, widget);

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
    if (tag_name != NULL && tag_name[0] == 0)
        return istream;

    if (tag_name == NULL)
        tag_name = "div";

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
    if (istream != NULL && (processor->options & PROCESSOR_QUIET) == 0)
        istream = embed_decorate(processor->output.pool, istream, widget);

    return istream;
}

static void
body_element_finished(processor_t processor, off_t end)
{

    if (processor->parser.tag_type != TAG_CLOSE) {
        if (processor->in_body)
            return;

        if ((processor->options & PROCESSOR_BODY) != 0)
            replace_add(&processor->replace, 0, end, NULL);

        processor->in_body = 1;
    } else {
        if ((processor->options & PROCESSOR_BODY) == 0 ||
            processor->end_of_body != (off_t)-1)
            return;

        processor->end_of_body = processor->parser.element_offset;
    }
}

void
parser_element_finished(struct parser *parser, off_t end)
{
    processor_t processor = parser_to_processor(parser);

    if (processor->tag == TAG_BODY) {
        body_element_finished(processor, end);
    } else if (processor->tag == TAG_WIDGET) {
        if (processor->parser.tag_type == TAG_OPEN ||
            processor->parser.tag_type == TAG_SHORT)
            processor->widget_start_offset = processor->parser.element_offset;
        else if (processor->embedded_widget == NULL)
            return;

        assert(processor->embedded_widget != NULL);

        if (processor->parser.tag_type == TAG_OPEN)
            return;
        
        istream_t istream = embed_element_finished(processor);
        replace_add(&processor->replace, processor->widget_start_offset,
                    end, istream);
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
        processor->script_start_offset = end;
    }
}

void
parser_cdata(struct parser *parser, const char *p, size_t length, int escaped)
{
    processor_t processor = parser_to_processor(parser);

    (void)escaped;

    if (processor->script != NULL)
        growing_buffer_write_buffer(processor->script, p, length);
}

