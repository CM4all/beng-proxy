/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

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
