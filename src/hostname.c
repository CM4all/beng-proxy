#include "hostname.h"
#include "strutil.h"

static bool
valid_hostname_char(char ch)
{
    /* we are allowing ':' here because it is a valid character in the
       HTTP "Host" request header */

    return char_is_alphanumeric(ch) || ch == '.' || ch == ':';
}

bool
hostname_is_well_formed(const char *p)
{
    if (*p == 0)
        return false;

    do {
        if (!valid_hostname_char(*p))
            return false;

        ++p;
    } while (*p != 0);

    return true;
}
