/*
 * Concatenate several istreams.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_CAT_HXX
#define BENG_PROXY_ISTREAM_CAT_HXX

struct pool;
class Istream;

Istream *
_istream_cat_new(struct pool &pool, Istream *const* inputs, unsigned n_inputs);

template<typename... Args>
Istream *
istream_cat_new(struct pool &pool, Args&&... args)
{
    Istream *const inputs[]{args...};
    return _istream_cat_new(pool, inputs, sizeof...(args));
}

#endif
