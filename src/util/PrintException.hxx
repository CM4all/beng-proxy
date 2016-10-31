/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PRINT_EXCEPTION_HXX
#define PRINT_EXCEPTION_HXX

#include <stdexcept>

/**
 * Print this exception (and its nested exceptions, if any) to stderr.
 */
void
PrintException(const std::exception &e);

void
PrintException(const std::exception_ptr &ep);

#endif
