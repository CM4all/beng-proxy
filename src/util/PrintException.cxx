/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "PrintException.hxx"

void
PrintException(const std::exception &e)
{
    fprintf(stderr, "%s\n", e.what());
    try {
        std::rethrow_if_nested(e);
    } catch (const std::exception &nested) {
        PrintException(nested);
    } catch (...) {
        fprintf(stderr, "Unrecognized nested exception\n");
    }
}
