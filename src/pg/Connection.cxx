/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Connection.hxx"

#include <string.h>

void
PgConnection::Connect(const char *conninfo)
{
    assert(!IsDefined());

    conn = ::PQconnectdb(conninfo);
    if (conn == nullptr)
        throw std::bad_alloc();

    if (GetStatus() != CONNECTION_OK)
        throw std::runtime_error(GetErrorMessage());
}

bool
PgConnection::SetSchema(const char *schema)
{
    std::string sql = "SET SCHEMA '" + Escape(schema) + "'";
    return Execute(sql.c_str()).IsCommandSuccessful();
}

void
PgConnection::SendQuery(const char *query)
{
    assert(IsDefined());
    assert(query != nullptr);

    if (::PQsendQuery(conn, query) == 0)
        throw std::runtime_error(GetErrorMessage());
}

std::string
PgConnection::Escape(const char *p, size_t length) const
{
    assert(p != nullptr || length == 0);

    char *buffer = new char[length * 2 + 1];

    ::PQescapeStringConn(conn, buffer, p, length, nullptr);
    std::string result(buffer, length);
    delete[] buffer;
    return result;
}

std::string
PgConnection::Escape(const char *p) const
{
    assert(p != nullptr);

    return Escape(p, strlen(p));
}
