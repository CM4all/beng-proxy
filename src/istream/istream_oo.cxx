/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_oo.hxx"
#include "istream_class.hxx"

constexpr struct istream_class Istream::cls = {
    GetAvailable,
    Skip,
    Read,
    AsFd,
    Close,
};
