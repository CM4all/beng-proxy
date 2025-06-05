// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SetupProcess.hxx"

#include <sys/signal.h>
#include <pthread.h>

void
SetupProcess()
{
	signal(SIGPIPE, SIG_IGN);

	/* reduce glibc's thread cancellation overhead */
	pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, nullptr);
}
