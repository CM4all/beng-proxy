/*
 * An istream which duplicates data.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_TEE_HXX
#define BENG_PROXY_ISTREAM_TEE_HXX

struct pool;
class Istream;

/**
 * Create two new streams fed from one input.
 *
 * Data gets delivered to the first output, then to the second output.
 * Destruction (eof / abort) goes the reverse order: the second output
 * gets destructed first.
 *
 * @param input the istream which is duplicated
 * @param first_weak if true, closes the whole object if only the
 * first output remains
 * @param second_weak if true, closes the whole object if only the
 * second output remains
 */
Istream *
istream_tee_new(struct pool &pool, Istream &input,
                bool first_weak, bool second_weak);

Istream &
istream_tee_second(Istream &istream);

#endif
