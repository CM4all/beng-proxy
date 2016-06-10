/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "penv.hxx"
#include "session.hxx"

SessionLease
processor_env::GetSession() const
{
    return SessionLease(session_id);
}
