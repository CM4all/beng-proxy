#include "istream/istream_oo.hxx"
#include "istream/istream_pointer.hxx"

#include <glib.h>

#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

struct StdioSink {
    IstreamPointer input;

    explicit StdioSink(struct istream &_input)
        :input(_input, MakeIstreamHandler<StdioSink>::handler, this) {}

    void LoopRead() {
        while (input.IsDefined())
            input.Read();
    }

    /* istream handler */

    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        input.Clear();
    }

    void OnError(GError *error) {
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
