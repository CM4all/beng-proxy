// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct CertDatabaseConfig;
class WorkshopProgress;

struct Usage {
	const char *text;

	explicit Usage(const char *_text):text(_text) {}
};

extern WorkshopProgress root_progress;

/**
 * Load the "cert_db" section from "/etc/cm4all/beng/lb.conf", and
 * allow overriding the "connect" value from
 * "/etc/cm4all/beng/certdb.connect".
 */
CertDatabaseConfig
LoadPatchCertDatabaseConfig();
