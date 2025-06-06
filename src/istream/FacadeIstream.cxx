// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FacadeIstream.hxx"

void
FacadeIstream::FillBucketListFromInput(IstreamBucketList &list)
{
	assert(HasInput());

	try {
		return input.FillBucketList(list);
	} catch (...) {
		Destroy();
		throw;
	}
}
