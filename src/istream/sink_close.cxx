/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_close.hxx"
#include "istream_pointer.hxx"

class SinkClose {
    IstreamPointer input;

public:
    explicit SinkClose(Istream &_input)
        :input(_input, MakeIstreamHandler<SinkClose>::handler, this) {}

    /* request istream handler */
    size_t OnData(gcc_unused const void *data, gcc_unused size_t length) {
        input.Close();
        return 0;
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        /* should not be reachable, because we expect the Istream to
           call the OnData() callback at least once */

        abort();
    }

    void OnError(gcc_unused GError *error) {
        /* should not be reachable, because we expect the Istream to
           call the OnData() callback at least once */

        abort();
    }
};

void
sink_close_new(struct pool &p, Istream &istream)
{
    NewFromPool<SinkClose>(p, istream);
}
