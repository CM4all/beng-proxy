/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_catch {
    struct istream output;
    istream_t input;
    off_t available;
};

static const char space[] = 
    "                                "
    "                                "
    "                                "
    "                                ";

static void
catch_send_whitespace(struct istream_catch *catch)
{
    size_t length, nbytes;

    assert(catch->input == NULL);
    assert(catch->available > 0);

    do {
        if (catch->available >= (off_t)sizeof(space) - 1)
            length = sizeof(space) - 1;
        else
            length = (size_t)catch->available;

        nbytes = istream_invoke_data(&catch->output, space, length);
        if (nbytes == 0)
            return;

        catch->available -= nbytes;
        if (nbytes < length)
            return;
    } while (catch->available > 0);

    istream_deinit_eof(&catch->output);
}


/*
 * istream handler
 *
 */

static size_t
catch_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_catch *catch = ctx;
    size_t nbytes;

    nbytes = istream_invoke_data(&catch->output, data, length);
    if (nbytes > 0) {
        if ((off_t)nbytes < catch->available)
            catch->available -= (off_t)nbytes;
        else
            catch->available = 0;
    }

    return nbytes;
}

static ssize_t
catch_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_catch *catch = ctx;
    ssize_t nbytes;

    nbytes = istream_invoke_direct(&catch->output, type, fd, max_length);
    if (nbytes > 0) {
        if ((off_t)nbytes < catch->available)
            catch->available -= (off_t)nbytes;
        else
            catch->available = 0;
    }

    return nbytes;
}

static void
catch_input_abort(void *ctx)
{
    struct istream_catch *catch = ctx;

    catch->input = NULL;

    if (catch->available > 0) {
        /* according to a previous call to method "available", there
           is more data which we must provide - fill that with space
           characters */
        catch->input = NULL;
        catch_send_whitespace(catch);
    } else
        istream_deinit_eof(&catch->output);
}

static const struct istream_handler catch_input_handler = {
    .data = catch_input_data,
    .direct = catch_input_direct,
    .eof = istream_forward_eof,
    .abort = catch_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_catch *
istream_to_catch(istream_t istream)
{
    return (struct istream_catch *)(((char*)istream) - offsetof(struct istream_catch, output));
}

static off_t 
istream_catch_available(istream_t istream, bool partial)
{
    struct istream_catch *catch = istream_to_catch(istream);

    if (catch->input != NULL) {
        off_t available = istream_available(catch->input, partial);
        if (available != (off_t)-1 && available > catch->available)
            catch->available = available;

        return available;
    } else
        return catch->available;
}

static void
istream_catch_read(istream_t istream)
{
    struct istream_catch *catch = istream_to_catch(istream);

    if (catch->input != NULL) {
        istream_handler_set_direct(catch->input, catch->output.handler_direct);
        istream_read(catch->input);
    } else
        catch_send_whitespace(catch);
}

static void
istream_catch_close(istream_t istream)
{
    struct istream_catch *catch = istream_to_catch(istream);

    if (catch->input != NULL)
        istream_free_handler(&catch->input);

    istream_deinit_abort(&catch->output);
}

static const struct istream istream_catch = {
    .available = istream_catch_available,
    .read = istream_catch_read,
    .close = istream_catch_close,
};


/*
 * constructor
 *
 */

istream_t
istream_catch_new(pool_t pool, istream_t input)
{
    struct istream_catch *catch = istream_new_macro(pool, catch);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&catch->input, input,
                           &catch_input_handler, catch,
                           0);
    catch->available = 0;

    return istream_struct_cast(&catch->output);
}
