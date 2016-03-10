// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_PENDING_CALL_HXX
#define ODBUS_PENDING_CALL_HXX

#include <dbus/dbus.h>

#include <algorithm>
#include <stdexcept>

namespace ODBus {
	class PendingCall {
		DBusPendingCall *pending = nullptr;

		explicit PendingCall(DBusPendingCall *_pending)
			:pending(_pending) {}

	public:
		PendingCall() = default;

		PendingCall(PendingCall &&src)
			:pending(src.pending) {
			src.pending = nullptr;
		}

		~PendingCall() {
			if (pending != nullptr)
				dbus_pending_call_unref(pending);
		}

		DBusPendingCall *Get() {
			return pending;
		}

		PendingCall &operator=(PendingCall &&src) {
			std::swap(pending, src.pending);
			return *this;
		}

		static PendingCall SendWithReply(DBusConnection *connection,
						 DBusMessage *message,
						 int timeout_milliseconds=-1) {
			DBusPendingCall *pending;
			if (!dbus_connection_send_with_reply(connection,
							     message,
							     &pending,
							     timeout_milliseconds))
				throw std::runtime_error("dbus_connection_send_with_reply() failed");

			if (pending == nullptr)
				throw std::runtime_error("dbus_connection_send_with_reply() failed with pending=NULL");

			return PendingCall(pending);
		}

		void Block() {
			dbus_pending_call_block(pending);
		}
	};
}

#endif
