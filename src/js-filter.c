/*
 * This istream filter reads JavaScript code and performs some
 * transformations on it (to be implemented).
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "js-filter.h"
#include "istream-internal.h"

#include <assert.h>

struct js_filter {
    struct istream output;
    istream_t input;
    bool had_input:1, had_output:1;
};


/*
 * istream handler
 *
 */

static size_t
js_input_data(const void *data, size_t length, void *ctx)
{
    struct js_filter *js = ctx;

    js->had_input = true;

    /* XXX insert filtering code here */

    js->had_output = true;
    return istream_invoke_data(&js->output, data, length);
}

static void
js_input_eof(void *ctx)
{
    struct js_filter *js = ctx;

    assert(js->input != NULL);

    js->input = NULL;
    istream_deinit_eof(&js->output);
}

static void
js_input_abort(void *ctx)
{
    struct js_filter *js = ctx;

    assert(js->input != NULL);

    js->input = NULL;
    istream_deinit_abort(&js->output);
}

static const struct istream_handler js_input_handler = {
    .data = js_input_data,
    .eof = js_input_eof,
    .abort = js_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct js_filter *
istream_to_js(istream_t istream)
{
    return (struct js_filter *)(((char*)istream) - offsetof(struct js_filter, output));
}

static void
js_filter_read(istream_t istream)
{
    struct js_filter *js = istream_to_js(istream);

    /* the following loop ensures that this istream implementation
       provides data unless its input is blocking or finished, as
       demanded by the istream API specification */

    js->had_output = false;

    do {
        js->had_input = false;
        istream_read(js->input);
    } while (js->input != NULL && js->had_input &&
             !js->had_output);
}

static void
js_filter_close(istream_t istream)
{
    struct js_filter *js = istream_to_js(istream);

    assert(js->input != NULL);

    istream_free_handler(&js->input);
    istream_deinit_abort(&js->output);
}

static const struct istream js_filter = {
    .read = js_filter_read,
    .close = js_filter_close,
};


/*
 * constructor
 *
 */

istream_t
js_filter_new(pool_t pool, istream_t input)
{
    struct js_filter *js = (struct js_filter *)
        istream_new(pool, &js_filter, sizeof(*js));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&js->input, input,
                           &js_input_handler, js,
                           0);

    return istream_struct_cast(&js->output);
}
