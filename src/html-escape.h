/*
 * Escape or unescape HTML entities.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_HTML_ESCAPE_H
#define __BENG_HTML_ESCAPE_H

#include <stddef.h>

struct strref;

/**
 * Resolve character entity references.
 *
 * @param s the buffer to be unescaped
 * @return the number of unescaped bytes or 0 if there were no entities
 */
size_t
html_unescape(struct strref *s);

#endif
