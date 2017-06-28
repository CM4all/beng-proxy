/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_null.hxx"
#include "Sink.hxx"

class SinkNull final : IstreamSink {
public:
    explicit SinkNull(Istream &_input)
        :IstreamSink(_input) {}

    /* virtual methods from class IstreamHandler */

    size_t OnData(gcc_unused const void *data, size_t length) override {
        return length;
    }

    void OnEof() override {
    }

    void OnError(std::exception_ptr) override {
    }
};

void
sink_null_new(struct pool &p, Istream &istream)
{
    NewFromPool<SinkNull>(p, istream);
}
