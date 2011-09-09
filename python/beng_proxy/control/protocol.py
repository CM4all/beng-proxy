#
# Definitions for the beng-proxy remote control protocol.
#
# Author: Max Kellermann <mk@cm4all.com>
#

control_magic = 0x63046101

CONTROL_NOP = 0
CONTROL_TCACHE_INVALIDATE = 1
CONTROL_ENABLE_NODE = 2
CONTROL_FADE_NODE = 3
