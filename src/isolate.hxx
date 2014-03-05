/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ISOLATE_HXX
#define ISOLATE_HXX

/**
 * Create a new mount namespace and change to an empty file system,
 * discarding access to all other file systems.
 */
void
isolate_from_filesystem();

#endif
