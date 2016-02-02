/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Prepared.hxx"

#include <string.h>

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
