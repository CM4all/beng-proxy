/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "PrefixLogger.hxx"
#include "event/SocketEvent.hxx"
#include "system/UniqueFileDescriptor.hxx"
#include "util/Error.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

class PrefixLogger {
    UniqueFileDescriptor fd;

    SocketEvent event;

    char buffer[256];
    size_t prefix_length = 0;
    size_t line_length = 0;

public:
    explicit PrefixLogger(EventLoop &event_loop, UniqueFileDescriptor &&_fd)
        :fd(std::move(_fd)),
         event(event_loop, fd.Get(), EV_READ|EV_PERSIST,
               BIND_THIS_METHOD(SocketEventCallback)) {
        event.Add();
    }

    ~PrefixLogger() {
        event.Delete();
    }

    void SetPrefix(const char *prefix) {
        assert(prefix_length == 0);
        assert(line_length == 0);

        prefix_length = line_length = std::min(strlen(prefix),
                                               sizeof(buffer) / 2);
        memcpy(buffer, prefix, prefix_length);
    }

    bool ConsumeLine() {
        assert(line_length < sizeof(buffer));

        size_t length;
        char *newline = (char *)
            memchr(buffer + prefix_length, '\n', line_length - prefix_length);
        if (newline == nullptr) {
            if (line_length < sizeof(buffer) - 1)
                return false;

            buffer[line_length++] = '\n';
            length = line_length;
        } else
            length = newline - buffer + 1;

        write(STDERR_FILENO, buffer, length);
        memmove(buffer + prefix_length,
                buffer + length,
                line_length - length);
        line_length -= length - prefix_length;

        assert(line_length >= prefix_length);

        return true;
    }

private:
    void SocketEventCallback(gcc_unused short events) {
        /* reserve 1 byte for newline for overlong lines */
        ssize_t nbytes = fd.Read(buffer + line_length,
                                 sizeof(buffer) - line_length - 1);
        if (nbytes <= 0) {
            delete this;
            return;
        }

        line_length += nbytes;

        while (ConsumeLine()) {}
    }
};

std::pair<PrefixLogger *, UniqueFileDescriptor>
CreatePrefixLogger(EventLoop &event_loop, Error &error)
{
    UniqueFileDescriptor r, w;
    if (!UniqueFileDescriptor::CreatePipe(r, w)) {
        error.SetErrno("pipe() failed");
        return std::make_pair(nullptr, UniqueFileDescriptor());
    }

    return std::make_pair(new PrefixLogger(event_loop, std::move(r)),
                          std::move(w));
}

void
DeletePrefixLogger(PrefixLogger *pl)
{
    delete pl;
}

void
PrefixLoggerSetPrefix(PrefixLogger &pl, const char *prefix)
{
    pl.SetPrefix(prefix);
}

void
PrefixLoggerSetPid(PrefixLogger &pl, int pid)
{
    char prefix[32];
    sprintf(prefix, "[pid=%d] ", int(pid));
    PrefixLoggerSetPrefix(pl, prefix);
}
