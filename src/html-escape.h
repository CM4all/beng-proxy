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
 * @return the number of bytes consumed from the input or 0 if there were no entities
 */
size_t
html_unescape(struct strref *s);

/**
 * Resolve character entity references.
 *
 * @param p the string to be unescaped
 * @param length the length of the input string
 * @return the new length
 */
size_t
html_unescape_inplace(char *p, size_t length);

/**
 * Escape special characters as HTML entities.
 *
 * @param s the buffer to be escaped
 * @return the number of bytes consumed from the input or 0 if there were no entities
 */
size_t
html_escape(struct strref *s);

#endif
