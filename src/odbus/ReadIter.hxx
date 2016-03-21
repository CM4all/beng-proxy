// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_READ_ITER_HXX
#define ODBUS_READ_ITER_HXX

#include "Iter.hxx"

namespace ODBus {
	class ReadMessageIter : public MessageIter {
	public:
		explicit ReadMessageIter(DBusMessage &msg) {
			dbus_message_iter_init(&msg, &iter);
		}

		const char *GetSignature() {
			return dbus_message_iter_get_signature(&iter);
		}
	};
}

#endif
