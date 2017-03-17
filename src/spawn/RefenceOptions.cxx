/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "RefenceOptions.hxx"
#include "AllocatorPtr.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/djbhash.h"

#include <algorithm>

#include <stdio.h>
#include <fcntl.h>
#include <string.h>

RefenceOptions::RefenceOptions(AllocatorPtr alloc, const RefenceOptions &src)
    :data(alloc.Dup(src.data))
{
}

inline unsigned
RefenceOptions::GetHash() const
{
    return djb_hash(data.data, data.size);
}

char *
RefenceOptions::MakeId(char *p) const
{
    if (!IsEmpty()) {
        *p++ = ';';
        *p++ = 'r';
        *p++ = 'f';
        p += sprintf(p, "%08x", GetHash());
    }

    return p;
}

inline void
RefenceOptions::Apply(FileDescriptor fd) const
{
    // TODO: set name, script

    auto p = data.begin();
    const auto end = data.end();

    while (true) {
        const auto n = std::find(p, end, '\0');
        ssize_t nbytes = fd.Write(p, n - p);
        if (nbytes < 0) {
            perror("Failed to write to Refence");
            _exit(2);
        }

        if (n == end)
            break;

        p = n + 1;
    }
}

void
RefenceOptions::Apply() const
{
    if (IsEmpty())
        return;

    constexpr auto path = "/proc/cm4all/refence/self";
    UniqueFileDescriptor fd;
    if (!fd.Open(path, O_WRONLY)) {
        perror("Failed to open Refence");
        _exit(2);
    }

    Apply(fd.ToFileDescriptor());
}
