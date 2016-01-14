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

std::string
pg_encode_array(const std::list<std::string> &src);

#endif
