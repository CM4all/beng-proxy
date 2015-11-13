/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_null.hxx"
#include "istream_pointer.hxx"

#include <glib.h>

class SinkNull {
    IstreamPointer input;

public:
    explicit SinkNull(Istream &_input)
        :input(_input, MakeIstreamHandler<SinkNull>::handler, this) {}

    /* request istream handler */
    size_t OnData(gcc_unused const void *data, size_t length) {
        return length;
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
    }

    void OnError(GError *error) {
        g_error_free(error);
    }
};

void
sink_null_new(struct pool &p, Istream &istream)
{
    NewFromPool<SinkNull>(p, istream);
}
