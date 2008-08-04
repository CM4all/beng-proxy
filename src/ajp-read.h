/*
 * Read an AJP stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_READ_H
#define __BENG_AJP_READ_H

struct ajp_input {
    const char *start, *end;
};

struct ajp_field {
    char *buffer;
    size_t nbytes, length;
};

static inline void
ajp_input_init(struct ajp_input *input, const void *data, size_t length)
{
    input->start = data;
    input->end = input->start + length;
}

static inline size_t
ajp_input_length(const struct ajp_input *input)
{
    assert(input->start <= input->end);

    return input->end - input->start;
}

static inline void
ajp_input_consume(struct ajp_input *input, size_t nbytes)
{
    assert(nbytes > 0);
    assert(input->start < input->end);
    assert(input->start + nbytes <= input->end);

    input->start += nbytes;
}

static inline void
ajp_field_init(struct ajp_field *field, char *buffer, size_t length)
{
    assert(buffer != NULL);
    assert(length > 0);

    field->buffer = buffer;
    field->nbytes = 0;
    field->length = length;
}

static inline void
ajp_field_extend(struct ajp_field *field, size_t length)
{
    assert(field->nbytes < field->length);
    assert(field->length <= length);

    field->length = length;
}

static inline bool
ajp_field_read(struct ajp_field *field, struct ajp_input *input)
{
    size_t nbytes, input_length;

    assert(field->nbytes < field->length);

    input_length = ajp_input_length(input);
    if (input_length == 0)
        return false;

    nbytes = field->length - field->nbytes;
    if (nbytes > input_length)
        nbytes = input_length;

    memcpy(field->buffer + field->nbytes, input->start, nbytes);
    ajp_input_consume(input, nbytes);

    field->nbytes += nbytes;
    return field->nbytes == field->length;
}

#endif
