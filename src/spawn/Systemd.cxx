/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Systemd.hxx"
#include "odbus/Message.hxx"
#include "odbus/Iter.hxx"
#include "odbus/PendingCall.hxx"
#include "util/Macros.hxx"

#include <systemd/sd-daemon.h>

#include <unistd.h>

void
CreateSystemdScope(const char *name, const char *description, bool delegate)
{
    if (!sd_booted())
        return;

    DBusError err;
    dbus_error_init(&err);

    auto *connection = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus connection error: %s\n", err.message);
        dbus_error_free(&err);
        return;
    }

    using namespace ODBus;

    auto msg = Message::NewMethodCall("org.freedesktop.systemd1",
                                      "/org/freedesktop/systemd1",
                                      "org.freedesktop.systemd1.Manager",
                                      "StartTransientUnit");

    MessageIter args(*msg.Get());
    args.Append(name).Append("replace");

    using PropTypeTraits = StructTypeTraits<StringTypeTraits,
                                            VariantTypeTraits>;

    const uint32_t pids_value[] = { uint32_t(getpid()) };

    MessageIter(args, DBUS_TYPE_ARRAY, PropTypeTraits::TypeAsString::value)
        .Append(Struct(String("Description"),
                       Variant(String(description))))
        .Append(Struct(String("PIDs"),
                       Variant(FixedArray(pids_value, ARRAY_SIZE(pids_value)))))
        .Append(Struct(String("Delegate"),
                       Variant(Boolean(delegate))))
        .CloseContainer(args);

    using AuxTypeTraits = StructTypeTraits<StringTypeTraits,
                                           ArrayTypeTraits<StructTypeTraits<StringTypeTraits,
                                                                            VariantTypeTraits>>>;
    args.AppendEmptyArray<AuxTypeTraits>();

    auto pending = PendingCall::SendWithReply(connection, msg.Get());

    dbus_connection_flush(connection);

    pending.Block();

    Message::StealReply(*pending.Get()).CheckThrowError();
}
