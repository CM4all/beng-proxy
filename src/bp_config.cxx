/*
 * Configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_config.hxx"

#include <string.h>

BpConfig::BpConfig()
{
    memset(&user, 0, sizeof(user));
}
