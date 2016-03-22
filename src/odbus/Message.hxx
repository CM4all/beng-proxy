// -*- mode: c++; indent-tabs-mode: t; c-basic-offset: 8; -*-
/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ODBUS_MESSAGE_HXX
#define ODBUS_MESSAGE_HXX

#include <dbus/dbus.h>

#include <algorithm>
#include <stdexcept>

namespace ODBus {
	class Message {
		DBusMessage *msg = nullptr;

		explicit Message(DBusMessage *_msg)
			:msg(_msg) {}

	public:
		Message() = default;

		Message(Message &&src)
			:msg(src.msg) {
			src.msg = nullptr;
		}

		~Message() {
			if (msg != nullptr)
				dbus_message_unref(msg);
		}

		DBusMessage *Get() {
			return msg;
		}

		Message &operator=(Message &&src) {
			std::swap(msg, src.msg);
			return *this;
		}

		static Message NewMethodCall(const char *destination,
					     const char *path,
					     const char *iface,
					     const char *method);

		static Message StealReply(DBusPendingCall &pending);

		static Message Pop(DBusConnection &connection);

		bool IsDefined() const {
			return msg != nullptr;
		}

		int GetType() {
			return dbus_message_get_type(msg);
		}

		const char *GetPath() {
			return dbus_message_get_path(msg);
		}

		bool HasPath(const char *object_path) {
			return dbus_message_has_path(msg, object_path);
		}

		const char *GetInterface() {
			return dbus_message_get_interface(msg);
		}

		bool HasInterface(const char *iface) {
			return dbus_message_has_interface(msg, iface);
		}

		const char *GetMember() {
			return dbus_message_get_member(msg);
		}

		bool HasMember(const char *member) {
			return dbus_message_has_member(msg, member);
		}

		bool IsError(const char *error_name) const {
			return dbus_message_is_error(msg, error_name);
		}

		const char *GetErrorName() const {
			return dbus_message_get_error_name(msg);
		}

		const char *GetDestination() const {
			return dbus_message_get_destination(msg);
		}

		const char *GetSender() const {
			return dbus_message_get_sender(msg);
		}

		const char *GetSignature() const {
			return dbus_message_get_signature(msg);
		}

		bool GetNoReply() const {
			return dbus_message_get_no_reply(msg);
		}

		bool IsMethodCall(const char *iface,
				  const char *method) const {
			return dbus_message_is_method_call(msg, iface, method);
		}

		bool IsSignal(const char *iface,
			      const char *signal_name) const {
			return dbus_message_is_signal(msg, iface, signal_name);
		}

		void CheckThrowError();

		template<typename... Args>
		bool GetArgs(DBusError &error, Args... args) {
			return dbus_message_get_args(msg, &error,
						     std::forward<Args>(args)...,
						     DBUS_TYPE_INVALID);
		}
	};
}

#endif
