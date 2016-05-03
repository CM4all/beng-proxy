#include "istream/Pointer.hxx"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct StdioSink final : IstreamHandler {
    IstreamPointer input;

    explicit StdioSink(Istream &_input)
        :input(_input, *this) {}

    void LoopRead() {
        while (input.IsDefined())
            input.Read();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override;

    void OnEof() override {
        input.Clear();
    }

    void OnError(GError *error) override {
        input.Clear();

        fprintf(stderr, "%s\n", error->message);
        g_error_free(error);
    }
};

size_t
StdioSink::OnData(const void *data, size_t length)
{
    ssize_t nbytes = write(STDOUT_FILENO, data, length);
    if (nbytes < 0) {
        perror("failed to write to stdout");
        input.ClearAndClose();
        return 0;
    }

    if (nbytes == 0) {
        fprintf(stderr, "failed to write to stdout\n");
        input.ClearAndClose();
        return 0;
    }

    return (size_t)nbytes;
}
