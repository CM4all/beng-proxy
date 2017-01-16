/*
 * Small utilities for PostgreSQL clients.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef PG_ARRAY_HXX
#define PG_ARRAY_HXX

#include <list>
#include <string>

/**
 * Throws std::invalid_argument on syntax error.
 */
std::list<std::string>
pg_decode_array(const char *p);

template<typename L>
std::string
pg_encode_array(const L &src)
{
    if (src.empty())
        return "{}";

    std::string dest("{");

    bool first = true;
    for (const auto &i : src) {
        if (first)
            first = false;
        else
            dest.push_back(',');

        dest.push_back('"');

        for (const auto ch : i) {
            if (ch == '\\' || ch == '"')
                dest.push_back('\\');
            dest.push_back(ch);
        }

        dest.push_back('"');
    }

    dest.push_back('}');
    return dest;
}

#endif
