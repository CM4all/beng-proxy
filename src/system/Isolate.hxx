// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#ifndef ISOLATE_HXX
#define ISOLATE_HXX

/**
 * Create a new mount namespace and change to an empty file system,
 * discarding access to all other file systems.
 *
 * @param allow_dbus bind-mount /run/dbus into the new mount
 * namespace?
 *
 * @param allow_prometheus_exporters bind-mount
 * /run/cm4all/prometheus-exporters into the new mount namespace?
 */
void
isolate_from_filesystem(bool allow_dbus,
			bool allow_prometheus_exporters);

#endif
