// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/* These functions can be used both synchronously and in a coroutine
   via Pg::CoQuery() */

template<typename Q>
auto
FindServerCertificateKeyByName(Q &&query,
			       const char *common_name,
			       const char *special)
{
	return query(true,
		     "SELECT certificate_der, key_der, key_wrap_name "
		     "FROM server_certificate "
		     "WHERE NOT deleted AND "
		     " special IS NOT DISTINCT FROM $2 AND"
		     " common_name=$1 "
		     "ORDER BY"
		     /* prefer certificates which expire later */
		     " not_after DESC "
		     "LIMIT 1",
		     common_name, special);
}

template<typename Q>
auto
FindServerCertificateKeyByAltName(Q &&query,
				  const char *common_name,
				  const char *special)
{
	return query(true,
		     "SELECT certificate_der, key_der, key_wrap_name "
		     "FROM server_certificate "
		     "WHERE NOT deleted AND "
		     " special IS NOT DISTINCT FROM $2 AND"
		     " EXISTS("
		     "SELECT id FROM server_certificate_alt_name"
		     " WHERE server_certificate_id=server_certificate.id"
		     " AND name=$1) "
		     "ORDER BY"
		     /* prefer certificates which expire later */
		     " not_after DESC "
		     "LIMIT 1",
		     common_name, special);
}
