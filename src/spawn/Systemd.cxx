/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Systemd.hxx"
#include "CgroupState.hxx"
#include "odbus/Message.hxx"
#include "odbus/Iter.hxx"
#include "odbus/PendingCall.hxx"
#include "util/Macros.hxx"
#include "util/IterableSplitString.hxx"
#include "util/ScopeExit.hxx"

#include <systemd/sd-daemon.h>

#include <forward_list>

#include <unistd.h>
#include <stdio.h>

gcc_pure
static CgroupState
LoadSystemdDelegate()
{
    FILE *file = fopen("/proc/self/cgroup", "r");
    if (file == nullptr)
        return CgroupState();

    AtScopeExit(file) { fclose(file); };

    struct ControllerAssignment {
        std::string name;
        std::string path;

        ControllerAssignment(StringView _name, StringView _path)
            :name(_name.data, _name.size),
             path(_path.data, _path.size) {}
    };

    std::forward_list<ControllerAssignment> assignments;

    std::string systemd_path;

    char line[256];
    while (fgets(line, sizeof(line), file) != nullptr) {
        char *p = line, *endptr;

        strtoul(p, &endptr, 10);
        if (endptr == p || *endptr != ':')
            continue;

        char *const _name = endptr + 1;
        char *const colon = strchr(_name, ':');
        if (colon == nullptr || colon == _name ||
            colon[1] != '/' || colon[2] == '/')
            continue;

        StringView name(_name, colon);

        StringView path(colon + 1);
        if (path.back() == '\n')
            --path.size;

        if (name.Equals("name=systemd"))
            systemd_path = std::string(path.data, path.size);
        else
            assignments.emplace_front(name, path);
    }

    if (systemd_path.empty())
        /* no "systemd" controller found - disable the feature */
        return CgroupState();

    CgroupState state;

    for (auto &i : assignments)
        if (i.path == systemd_path)
            state.controllers.emplace_front(std::move(i.name));

    if (state.controllers.empty())
        /* no matching controllers found - disable the feature */
        return CgroupState();

    state.group_path = std::move(systemd_path);

    return state;
}

CgroupState
CreateSystemdScope(const char *name, const char *description, bool delegate)
{
    if (!sd_booted())
        return CgroupState();

    DBusError err;
    dbus_error_init(&err);

    auto *connection = dbus_bus_get(DBUS_BUS_SYSTEM, &err);
    if (dbus_error_is_set(&err)) {
        fprintf(stderr, "DBus connection error: %s\n", err.message);
        dbus_error_free(&err);
        return CgroupState();
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

    return delegate
        ? LoadSystemdDelegate()
        : CgroupState();
}
