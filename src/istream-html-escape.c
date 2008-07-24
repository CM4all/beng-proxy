/*
 * This istream filter which escapes control characters with HTML
 * entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>
#include <string.h>

struct istream_html_escape {
    struct istream output;
    istream_t input;

    const char *entity;
    size_t entity_left;
};


static bool
html_escape_send_entity(struct istream_html_escape *html_escape)
{
    size_t nbytes;

    assert(html_escape->entity_left > 0);

    nbytes = istream_invoke_data(&html_escape->output, html_escape->entity,
                                 html_escape->entity_left);
    if (nbytes == 0)
        return false;

    html_escape->entity_left -= nbytes;
    if (html_escape->entity_left > 0) {
        html_escape->entity += nbytes;
        return false;
    }

    if (html_escape->input == NULL) {
        istream_invoke_eof(&html_escape->output);
        return false;
    }

    return true;
}

/*
 * istream handler
 *
 */

static size_t
html_escape_input_data(const void *data0, size_t length, void *ctx)
{
    struct istream_html_escape *html_escape = ctx;
    const char *data = data0, *lt, *amp, *control, *entity;
    size_t total, nbytes;

    if (html_escape->entity_left > 0 && !html_escape_send_entity(html_escape))
        return 0;

    total = 0;

    pool_ref(html_escape->output.pool);

    do {
        /* find the next control character */
        lt = memchr(data, '<', length);
        amp = memchr(data, '&', length);

        if (lt != NULL && (amp == NULL || amp > lt)) {
            control = lt;
            entity = "&lt;";
        } else if (amp != NULL && (lt == NULL || lt > amp)) {
            control = amp;
            entity = "&amp;";
        } else {
            /* none found - just forward the data block to our sink */
            nbytes = istream_invoke_data(&html_escape->output, data, length);
            if (nbytes == 0 && html_escape->input == NULL)
                total = 0;
            else
                total += nbytes;
            break;
        }

        if (control > data) {
            /* forward the portion before the control character */
            nbytes = istream_invoke_data(&html_escape->output, data, control - data);
            if (nbytes == 0 && html_escape->input == NULL) {
                total = 0;
                break;
            }

            total += nbytes;
            if (nbytes < (size_t)(control - data))
                break;
        }

        /* consume everything until after the control character */

        length -= control - data + 1;
        data = control + 1;
        ++total;

        /* insert the entity into the stream */

        html_escape->entity = entity;
        html_escape->entity_left = strlen(entity);

        if (!html_escape_send_entity(html_escape)) {
            if (html_escape->input == NULL)
                total = 0;
            break;
        }
    } while (length > 0);

    pool_unref(html_escape->output.pool);

    return total;
}

static const struct istream_handler html_escape_input_handler = {
    .data = html_escape_input_data,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_html_escape *
istream_to_html_escape(istream_t istream)
{
    return (struct istream_html_escape *)(((char*)istream) - offsetof(struct istream_html_escape, output));
}

static void
istream_html_escape_read(istream_t istream)
{
    struct istream_html_escape *html_escape = istream_to_html_escape(istream);

    if (html_escape->entity_left > 0 && !html_escape_send_entity(html_escape))
        return;

    assert(html_escape->input != NULL);

    istream_read(html_escape->input);
}

static void
istream_html_escape_close(istream_t istream)
{
    struct istream_html_escape *html_escape = istream_to_html_escape(istream);

    if (html_escape->input != NULL)
        istream_free_handler(&html_escape->input);

    istream_deinit_abort(&html_escape->output);
}

static const struct istream istream_html_escape = {
    .read = istream_html_escape_read,
    .close = istream_html_escape_close,
};


/*
 * constructor
 *
 */

istream_t
istream_html_escape_new(pool_t pool, istream_t input)
{
    struct istream_html_escape *html_escape = istream_new_macro(pool, html_escape);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&html_escape->input, input,
                           &html_escape_input_handler, html_escape,
                           0);

    html_escape->entity_left = 0;

    return istream_struct_cast(&html_escape->output);
}
