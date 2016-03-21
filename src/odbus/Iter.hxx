// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_ITER_HXX
#define ODBUS_ITER_HXX

namespace ODBus {
	class MessageIter {
	protected:
		DBusMessageIter iter;

		MessageIter() = default;

	public:
		MessageIter(const MessageIter &) = delete;
		MessageIter &operator=(const MessageIter &) = delete;
	};
}

#endif
