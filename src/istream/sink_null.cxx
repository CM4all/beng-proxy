/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_null.hxx"
#include "istream_pointer.hxx"

#include <glib.h>

class SinkNull final : IstreamHandler {
    IstreamPointer input;

public:
    explicit SinkNull(Istream &_input)
        :input(_input, *this) {}

    /* virtual methods from class IstreamHandler */

    size_t OnData(gcc_unused const void *data, size_t length) override {
        return length;
    }

    void OnEof() override {
    }

    void OnError(GError *error) override {
        g_error_free(error);
    }
};

void
sink_null_new(struct pool &p, Istream &istream)
{
    NewFromPool<SinkNull>(p, istream);
}
