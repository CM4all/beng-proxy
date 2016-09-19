/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Prepared.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ConstBuffer.hxx"

#include <string.h>
#include <unistd.h>

PreparedChildProcess::PreparedChildProcess()
{
}

PreparedChildProcess::~PreparedChildProcess()
{
    if (stdin_fd >= 0)
        close(stdin_fd);
    if (stdout_fd >= 0 && stdout_fd != stdin_fd)
        close(stdout_fd);
    if (stderr_fd >= 0 &&
        stderr_fd != stdout_fd && stderr_fd != stdin_fd)
        close(stderr_fd);
    if (control_fd >= 0)
        close(control_fd);
}

bool
PreparedChildProcess::InsertWrapper(ConstBuffer<const char *> w)
{
    if (args.size() + w.size >= args.capacity())
        return false;

    args.insert(0, w.begin(), w.end());
    return true;
}

bool
PreparedChildProcess::SetEnv(const char *name, const char *value)
{
    assert(name != nullptr);
    assert(value != nullptr);

    strings.emplace_front(name);
    auto &s = strings.front();
    s.push_back('=');
    s.append(value);
    return PutEnv(s.c_str());
}

void
PreparedChildProcess::SetStdin(int fd)
{
    assert(fd != stdin_fd);

    if (stdin_fd >= 0)
        close(stdin_fd);
    stdin_fd = fd;
}

void
PreparedChildProcess::SetStdout(int fd)
{
    assert(fd != stdout_fd);

    if (stdout_fd >= 0)
        close(stdout_fd);
    stdout_fd = fd;
}

void
PreparedChildProcess::SetStderr(int fd)
{
    assert(fd != stderr_fd);

    if (stderr_fd >= 0)
        close(stderr_fd);
    stderr_fd = fd;
}

void
PreparedChildProcess::SetControl(int fd)
{
    assert(fd != control_fd);

    if (control_fd >= 0)
        close(control_fd);
    control_fd = fd;
}

void
PreparedChildProcess::SetStdin(UniqueFileDescriptor &&fd)
{
    SetStdin(fd.Steal());
}

void
PreparedChildProcess::SetStdout(UniqueFileDescriptor &&fd)
{
    SetStdout(fd.Steal());
}

void
PreparedChildProcess::SetStderr(UniqueFileDescriptor &&fd)
{
    SetStderr(fd.Steal());
}

void
PreparedChildProcess::SetControl(UniqueFileDescriptor &&fd)
{
    SetControl(fd.Steal());
}

const char *
PreparedChildProcess::Finish()
{
    assert(!args.empty());
    assert(!args.full());
    assert(!env.full());

    const char *path = args.front();
    const char *slash = strrchr(path, '/');
    if (slash != nullptr && slash[1] != 0)
        args.front() = slash + 1;

    args.push_back(nullptr);
    env.push_back(nullptr);

    return path;
}
