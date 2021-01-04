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

#include "capabilities.hxx"
#include "system/CapabilityState.hxx"

#include <sys/prctl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

void
capabilities_pre_setuid() noexcept
{
	/* we want to keep all capabilities after switching to an
	   unprivileged uid */

	if (prctl(PR_SET_KEEPCAPS, 1) < 0) {
		fprintf(stderr, "prctl(PR_SET_KEEPCAPS) failed: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}
}

void
capabilities_post_setuid(const cap_value_t *keep_list, unsigned n) noexcept
{
	/* restore the KEEPCAPS flag */

	if (prctl(PR_SET_KEEPCAPS, 0) < 0) {
		fprintf(stderr, "prctl(PR_SET_KEEPCAPS) failed: %s\n",
			strerror(errno));
		exit(EXIT_FAILURE);
	}

	/* now drop all capabilities but the ones we want */

	CapabilityState state = CapabilityState::Empty();
	state.SetFlag(CAP_EFFECTIVE, {keep_list, n}, CAP_SET);
	state.SetFlag(CAP_PERMITTED, {keep_list, n}, CAP_SET);
	state.Install();
}
