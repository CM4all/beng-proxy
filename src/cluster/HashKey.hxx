// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "lib/sodium/HashKey.hxx"
#include "lib/sodium/GenericHash.hxx"

/**
 * Generates a collision-free hash which identifies the address list
 * in a hash table.
 */
template<typename List>
HashKey
GetHashKey(const List &list) noexcept
{
	GenericHashState state(sizeof(HashKey));

	for (const auto &i : list)
		state.Update(i.GetSteadyPart());

	return state.GetFinalT<HashKey>();
}
