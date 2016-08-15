/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "LineParser.hxx"

#include <string.h>

bool
LineParser::SkipWord(const char *word)
{
    char *q = p;

    do {
        if (*q++ != *word)
            return false;
    } while (*++word != 0);

    if (*q == 0) {
        p = q;
        return true;
    } else if (IsWhitespaceFast(*q)) {
        p = StripLeft(q + 1);
        return true;
    } else
        return false;
}

const char *
LineParser::NextWord()
{
    if (!IsWordChar(front()))
        return nullptr;

    const char *result = p;
    do {
        ++p;
    } while (IsWordChar(front()));

    if (IsWhitespaceNotNull(front())) {
        *p++ = 0;
        Strip();
    } else if (!IsEnd())
        return nullptr;

    return result;
}

inline char *
LineParser::NextUnquotedValue()
{
    if (!IsUnquotedChar(front()))
        return nullptr;

    char *result = p;
    do {
        ++p;
    } while (IsUnquotedChar(front()));

    if (IsWhitespaceNotNull(front())) {
        *p++ = 0;
        Strip();
    } else if (!IsEnd())
        return nullptr;

    return result;
}

inline char *
LineParser::NextQuotedValue(const char stop)
{
    char *const value = p;
    char *q = strchr(p, stop);
    if (q == nullptr)
        return nullptr;

    *q++ = 0;
    p = StripLeft(q);
    return value;
}

char *
LineParser::NextValue()
{
    const char ch = front();
    if (IsQuote(ch)) {
        ++p;
        return NextQuotedValue(ch);
    } else
        return NextUnquotedValue();
}

char *
LineParser::NextUnescape()
{
    const char stop = front();
    if (!IsQuote(stop))
        return nullptr;

    char *dest = ++p;
    char *const value = dest;

    while (true) {
        char ch = *p++;

        if (ch == 0)
            return nullptr;
        else if (ch == stop) {
            *dest = 0;
            Strip();
            return value;
        } else if (ch == '\\') {
            ch = *p++;

            switch (ch) {
            case 'r':
                *dest++ = '\r';
                break;

            case 'n':
                *dest++ = '\n';
                break;

            case '\\':
            case '\'':
            case '\"':
                *dest++ = ch;
                break;

            default:
                return nullptr;
            }
        } else
            *dest++ = ch;
    }
}

bool
LineParser::NextBool()
{
    const char *value = NextValue();
    if (value == nullptr)
        throw Error("yes/no expected");

    if (strcmp(value, "yes") == 0)
        return true;
    else if (strcmp(value, "no") == 0)
        return false;
    else
        throw Error("yes/no expected");
}

unsigned
LineParser::NextPositiveInteger()
{
    const char *string = NextValue();
    if (string == nullptr)
        return 0;

    char *endptr;
    unsigned long l = strtoul(string, &endptr, 10);
    if (endptr == string || *endptr != 0)
        return 0;

    return (unsigned)l;
}

char *
LineParser::ExpectValueAndEnd()
{
    char *value = NextValue();
    if (value == nullptr)
        throw Error("Value expected");

    ExpectEnd();

    return value;
}
