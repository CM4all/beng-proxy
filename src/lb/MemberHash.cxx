// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "MemberHash.hxx"
#include "lib/sodium/GenericHash.hxx"
#include "net/SocketAddress.hxx"

sticky_hash_t
MemberAddressHash(SocketAddress address, std::size_t replica) noexcept
{
	/* use libsodium's "generichash" (BLAKE2b) which is secure
	   enough for class HashRing */
	union {
		std::array<std::byte, crypto_generichash_BYTES_MIN> hash;
		sticky_hash_t result;
	} u;

	static_assert(sizeof(u.hash) >= sizeof(u.result));

	GenericHashState state(sizeof(u.hash));
	state.Update(address.GetSteadyPart());
	state.UpdateT(replica);
	state.Final(u.hash);

	return u.result;
}
