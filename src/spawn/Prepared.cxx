/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Prepared.hxx"
#include "util/ConstBuffer.hxx"

#include <string.h>

bool
PreparedChildProcess::InsertWrapper(ConstBuffer<const char *> w)
{
    if (args.size() + w.size >= args.capacity())
        return false;

    args.insert(0, w.begin(), w.end());
    return true;
}

void
PreparedChildProcess::SetEnv(const char *name, const char *value)
{
    assert(name != nullptr);
    assert(value != nullptr);

    const size_t name_length = strlen(name);
    const size_t value_length = strlen(value);

    assert(name_length > 0);

    char *buffer = (char *)malloc(name_length + 1 + value_length + 1);
    memcpy(buffer, name, name_length);
    buffer[name_length] = '=';
    memcpy(buffer + name_length + 1, value, value_length);
    buffer[name_length + 1 + value_length] = 0;

    PutEnv(buffer);

    /* no need to free this allocation; this process will be replaced
       soon by execve() anyway */
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
