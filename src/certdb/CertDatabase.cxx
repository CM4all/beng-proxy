/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "CertDatabase.hxx"
#include "Config.hxx"

#include <sys/poll.h>

CertDatabase::CertDatabase(const CertDatabaseConfig &_config)
    :conn(_config.connect.c_str()), schema(_config.schema)
{
    if (!schema.empty() && !conn.SetSchema(schema.c_str()))
        throw std::runtime_error("Failed to set schema '" + schema + ": " +
                                 conn.GetErrorMessage());
}

bool
CertDatabase::CheckConnected()
{
    if (GetStatus() != CONNECTION_OK)
        return false;

    struct pollfd pfd = {
        .fd = GetSocket(),
        .events = POLLIN,
    };

    if (poll(&pfd, 1, 0) == 0)
        return true;

    conn.ConsumeInput();
    if (GetStatus() != CONNECTION_OK)
        return false;

    /* try again, just in case the previous PQconsumeInput() call has
       read a final message from the socket */

    if (poll(&pfd, 1, 0) == 0)
        return true;

    conn.ConsumeInput();
    return GetStatus() == CONNECTION_OK;
}

void
CertDatabase::EnsureConnected()
{
    if (!CheckConnected())
        conn.Reconnect();
}

PgResult
CertDatabase::ListenModified()
{
    std::string sql("LISTEN \"");
    if (!schema.empty() && schema != "public") {
        /* prefix the notify name unless we're in the default
           schema */
        sql += schema;
        sql += ':';
    }

    sql += "modified\"";

    return conn.Execute(sql.c_str());
}

PgResult
CertDatabase::NotifyModified()
{
    std::string sql("NOTIFY \"");
    if (!schema.empty() && schema != "public") {
        /* prefix the notify name unless we're in the default
           schema */
        sql += schema;
        sql += ':';
    }

    sql += "modified\"";

    return conn.Execute(sql.c_str());
}
