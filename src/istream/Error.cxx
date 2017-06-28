/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.hxx"
#include "GException.hxx"

void
Istream::InvokeError(std::exception_ptr ep)
{
    InvokeError(ToGError(ep));
}
