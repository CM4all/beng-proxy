/*
 * An istream filter that escapes the data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-escape.h"
#include "istream-internal.h"
#include "escape_class.h"

#include <assert.h>
#include <string.h>

struct istream_escape {
    struct istream output;
    struct istream *input;

    const struct escape_class *class;

    const char *escaped;
    size_t escaped_left;
};

static bool
escape_send_escaped(struct istream_escape *escape)
{
    size_t nbytes;

    assert(escape->escaped_left > 0);

    nbytes = istream_invoke_data(&escape->output, escape->escaped,
                                 escape->escaped_left);
    if (nbytes == 0)
        return false;

    escape->escaped_left -= nbytes;
    if (escape->escaped_left > 0) {
        escape->escaped += nbytes;
        return false;
    }

    if (escape->input == NULL) {
        istream_invoke_eof(&escape->output);
        return false;
    }

    return true;
}

/*
 * istream handler
 *
 */

static size_t
escape_input_data(const void *data0, size_t length, void *ctx)
{
    struct istream_escape *escape = ctx;
    const char *data = data0;
    size_t total, nbytes;

    if (escape->escaped_left > 0 && !escape_send_escaped(escape))
        return 0;

    total = 0;

    pool_ref(escape->output.pool);

    do {
        /* find the next control character */
        const char *control = escape_find(escape->class, data, length);
        if (control == NULL) {
            /* none found - just forward the data block to our sink */
            nbytes = istream_invoke_data(&escape->output, data, length);
            if (nbytes == 0 && escape->input == NULL)
                total = 0;
            else
                total += nbytes;
            break;
        }

        if (control > data) {
            /* forward the portion before the control character */
            const size_t n = control - data;
            nbytes = istream_invoke_data(&escape->output, data, n);
            if (nbytes == 0 && escape->input == NULL) {
                total = 0;
                break;
            }

            total += nbytes;
            if (nbytes < n)
                break;
        }

        /* consume everything until after the control character */

        length -= control - data + 1;
        data = control + 1;
        ++total;

        /* insert the entity into the stream */

        escape->escaped = escape_char(escape->class, *control);
        escape->escaped_left = strlen(escape->escaped);

        if (!escape_send_escaped(escape)) {
            if (escape->input == NULL)
                total = 0;
            break;
        }
    } while (length > 0);

    pool_unref(escape->output.pool);

    return total;
}

static const struct istream_handler escape_input_handler = {
    .data = escape_input_data,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_escape *
istream_to_escape(struct istream *istream)
{
    return (struct istream_escape *)(((char*)istream) - offsetof(struct istream_escape, output));
}

static void
istream_escape_read(struct istream *istream)
{
    struct istream_escape *escape = istream_to_escape(istream);

    if (escape->escaped_left > 0 && !escape_send_escaped(escape))
        return;

    assert(escape->input != NULL);

    istream_read(escape->input);
}

static void
istream_escape_close(struct istream *istream)
{
    struct istream_escape *escape = istream_to_escape(istream);

    if (escape->input != NULL)
        istream_free_handler(&escape->input);

    istream_deinit(&escape->output);
}

static const struct istream_class istream_escape = {
    .read = istream_escape_read,
    .close = istream_escape_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_escape_new(struct pool *pool, struct istream *input,
                   const struct escape_class *class)
{
    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(class != NULL);
    assert(class->escape_find != NULL);
    assert(class->escape_char != NULL);

    struct istream_escape *escape = istream_new_macro(pool, escape);

    istream_assign_handler(&escape->input, input,
                           &escape_input_handler, escape,
                           0);

    escape->class = class;
    escape->escaped_left = 0;

    return istream_struct_cast(&escape->output);
}
