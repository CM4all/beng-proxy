/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_control.hxx"
#include "bp_stats.hxx"
#include "control_distribute.hxx"
#include "control_server.hxx"
#include "control_local.hxx"
#include "bp_instance.hxx"
#include "translation/Request.hxx"
#include "translation/Cache.hxx"
#include "translation/Protocol.hxx"
#include "translation/InvalidateParser.hxx"
#include "tpool.hxx"
#include "pool.hxx"
#include "net/UdpDistribute.hxx"
#include "net/SocketAddress.hxx"
#include "net/IPv4Address.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Exception.hxx"
#include "util/Macros.hxx"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

static void
control_tcache_invalidate(BpInstance *instance,
                          const void *payload, size_t payload_length)
{
    if (payload_length == 0) {
        /* flush the translation cache if the payload is empty */
        translate_cache_flush(*instance->translate_cache);
        return;
    }

    const AutoRewindPool auto_rewind(*tpool);

    TranslateRequest request;
    request.Clear();

    const char *site;
    TranslationCommand cmds[32];
    unsigned num_cmds;

    try {
        num_cmds = decode_translation_packets(*tpool, request,
                                              cmds, ARRAY_SIZE(cmds),
                                              payload, payload_length, &site);
    } catch (...) {
        daemon_log(2, "malformed TCACHE_INVALIDATE control packet: %s\n",
                   GetFullMessage(std::current_exception()).c_str());
        return;
    }

    translate_cache_invalidate(*instance->translate_cache, request,
                               ConstBuffer<TranslationCommand>(cmds, num_cmds),
                               site);
}

static void
query_stats(BpInstance *instance, ControlServer *server,
            SocketAddress address)
{
    if (address.GetSize() == 0)
        /* TODO: this packet was forwarded by the master process, and
           has no source address; however, the master process must get
           statistics from all worker processes (even those that have
           exited already) */
        return;

    struct beng_control_stats stats;
    bp_get_stats(instance, &stats);

    try {
        server->Reply(address,
                      CONTROL_STATS, &stats, sizeof(stats));
    } catch (const std::runtime_error &e) {
        daemon_log(3, "%s\n", e.what());
    }
}

static void
handle_control_packet(BpInstance *instance, ControlServer *server,
                      enum beng_control_command command,
                      const void *payload, size_t payload_length,
                      SocketAddress address)
{
    daemon_log(5, "control command=%d payload_length=%zu\n",
               command, payload_length);

    /* only local clients are allowed to use most commands */
    const bool is_privileged = address.GetFamily() == AF_LOCAL;

    switch (command) {
    case CONTROL_NOP:
        /* duh! */
        break;

    case CONTROL_TCACHE_INVALIDATE:
        control_tcache_invalidate(instance, payload, payload_length);
        break;

    case CONTROL_DUMP_POOLS:
        if (is_privileged)
            pool_dump_tree(instance->root_pool);
        break;

    case CONTROL_ENABLE_NODE:
    case CONTROL_FADE_NODE:
    case CONTROL_NODE_STATUS:
        /* only for beng-lb */
        break;

    case CONTROL_STATS:
        query_stats(instance, server, address);
        break;

    case CONTROL_VERBOSE:
        if (is_privileged && payload_length == 1)
            daemon_log_config.verbose = *(const uint8_t *)payload;
        break;

    case CONTROL_FADE_CHILDREN:
        if (is_privileged)
            instance->FadeChildren();
        break;
    }
}

void
BpInstance::OnControlPacket(ControlServer &_control_server,
                            enum beng_control_command command,
                            const void *payload, size_t payload_length,
                            SocketAddress address)
{
    handle_control_packet(this, &_control_server,
                          command, payload, payload_length,
                          address);
}

void
BpInstance::OnControlError(std::exception_ptr ep)
{
    daemon_log(2, "%s\n", GetFullMessage(ep).c_str());
}

void
global_control_handler_init(BpInstance *instance)
{
    if (instance->config.control_listen.empty())
        return;

    instance->control_distribute = new ControlDistribute(instance->event_loop,
                                                         *instance);

    for (const auto &control_listen : instance->config.control_listen) {
        instance->control_servers.emplace_front(instance->event_loop,
                                                *instance->control_distribute,
                                                control_listen);
    }
}

void
global_control_handler_deinit(BpInstance *instance)
{
    instance->control_servers.clear();
    delete instance->control_distribute;
}

void
global_control_handler_enable(BpInstance &instance)
{
    for (auto &c : instance.control_servers)
        c.Enable();
}

void
global_control_handler_disable(BpInstance &instance)
{
    for (auto &c : instance.control_servers)
        c.Disable();
}

UniqueSocketDescriptor
global_control_handler_add_fd(BpInstance *instance)
{
    assert(!instance->control_servers.empty());
    assert(instance->control_distribute != nullptr);

    return instance->control_distribute->Add();
}

void
global_control_handler_set_fd(BpInstance *instance,
                              UniqueSocketDescriptor &&fd)
{
    assert(!instance->control_servers.empty());
    assert(instance->control_distribute != nullptr);

    instance->control_distribute->Clear();

    /* erase all but one */
    instance->control_servers.erase_after(instance->control_servers.begin(),
                                          instance->control_servers.end());

    /* replace the one */
    instance->control_servers.front().SetFd(std::move(fd));
}

/*
 * local (implicit) control channel
 */

void
local_control_handler_init(BpInstance *instance)
{
    instance->local_control_server =
        control_local_new("beng_control:pid=", *instance);
}

void
local_control_handler_deinit(BpInstance *instance)
{
    control_local_free(instance->local_control_server);
}

void
local_control_handler_open(BpInstance *instance)
{
    control_local_open(instance->local_control_server,
                       instance->event_loop);
}
