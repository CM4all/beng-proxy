/*
 * Replace part of a stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_REPLACE_H
#define __BENG_REPLACE_H

#include "istream.h"

struct substitution;
struct growing_buffer;

struct replace {
    pool_t pool;
    struct istream *output;
    void (*output_eof)(struct istream *output);

    int quiet, reading_source;
    struct growing_buffer *buffer;
    off_t source_length, position;

    struct substitution *first_substitution, **append_substitution_p;

    int read_locked;

#ifndef NDEBUG
    off_t last_substitution_end;
#endif
};

void
replace_init(struct replace *replace, pool_t pool,
             struct istream *output,
             void (*output_eof)(struct istream *output),
             int quiet);

void
replace_destroy(struct replace *replace);

/**
 * Read data from the source file.
 */
size_t
replace_feed(struct replace *replace, const void *data, size_t length);

/**
 * End of source file reached.
 */
void
replace_eof(struct replace *replace);

/**
 * Add a new substition.  Substitutions may not overlap.
 */
void
replace_add(struct replace *replace, off_t start, off_t end,
            istream_t istream);

/**
 * Read data from the 'replace' object.  This function will call
 * istream_invoke_data(replace->output).
 */
void
replace_read(struct replace *replace);

#endif
