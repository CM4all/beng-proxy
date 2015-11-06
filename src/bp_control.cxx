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
#include "udp_distribute.hxx"
#include "bp_instance.hxx"
#include "tcache.hxx"
#include "translate_request.hxx"
#include "tpool.hxx"
#include "beng-proxy/translation.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"
#include "util/ByteOrder.hxx"
#include "util/Error.hxx"
#include "util/Macros.hxx"

#include <glib.h>

#include <daemon/log.h>

#include <assert.h>
#include <arpa/inet.h>
#include <string.h>

static bool
apply_translation_packet(TranslateRequest *request,
                         enum beng_translation_command command,
                         const char *payload, size_t payload_length)
{
    switch (command) {
    case TRANSLATE_URI:
        request->uri = payload;
        break;

    case TRANSLATE_SESSION:
        request->session = { payload, payload_length };
        break;

        /* XXX
    case TRANSLATE_LOCAL_ADDRESS:
        request->local_address = payload;
        break;
        */

    case TRANSLATE_REMOTE_HOST:
        request->remote_host = payload;
        break;

    case TRANSLATE_HOST:
        request->host = payload;
        break;

    case TRANSLATE_LANGUAGE:
        request->accept_language = payload;
        break;

    case TRANSLATE_USER_AGENT:
        request->user_agent = payload;
        break;

    case TRANSLATE_UA_CLASS:
        request->ua_class = payload;
        break;

    case TRANSLATE_QUERY_STRING:
        request->query_string = payload;
        break;

    default:
        /* unsupported */
        return false;
    }

    return true;
}

static unsigned
decode_translation_packets(struct pool *pool, TranslateRequest *request,
                           uint16_t *cmds, unsigned max_cmds,
                           const void *data, size_t length,
                           const char **site_r)
{
    *site_r = NULL;

    unsigned num_cmds = 0;

    if (length % 4 != 0)
        /* must be padded */
        return 0;

    while (length > 0) {
        const beng_translation_header *header =
            (const beng_translation_header *)data;
        if (length < sizeof(*header))
            return 0;

        size_t payload_length = FromBE16(header->length);
        beng_translation_command command =
            beng_translation_command(FromBE16(header->command));

        data = header + 1;
        length -= sizeof(*header);

        if (length < payload_length)
            return 0;

        char *payload = payload_length > 0
            ? p_strndup(pool, (const char *)data, payload_length)
            : NULL;
        if (command == TRANSLATE_SITE)
            *site_r = payload;
        else if (apply_translation_packet(request, command, payload,
                                          payload_length)) {
            if (num_cmds >= max_cmds)
                return 0;

            cmds[num_cmds++] = (uint16_t)command;
        } else
            return 0;

        payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

        data = (const char *)data + payload_length;
        length -= payload_length;
    }

    return num_cmds;
}

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
    uint16_t cmds[32];
    unsigned num_cmds =
        decode_translation_packets(tpool, &request, cmds, ARRAY_SIZE(cmds),
                                   payload, payload_length, &site);
    if (num_cmds == 0 && site == NULL) {
        daemon_log(2, "malformed TCACHE_INVALIDATE control packet\n");
        return;
    }

    translate_cache_invalidate(*instance->translate_cache, request,
                               ConstBuffer<uint16_t>(cmds, num_cmds),
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

    const AutoRewindPool auto_rewind(*tpool);

    Error error;
    if (!server->Reply(tpool,
                       address,
                       CONTROL_STATS, &stats, sizeof(stats),
                       error))
        daemon_log(3, "%s\n", error.GetMessage());
}

static void
handle_control_packet(BpInstance *instance, ControlServer *server,
                      enum beng_control_command command,
                      const void *payload, size_t payload_length,
                      SocketAddress address)
{
    daemon_log(5, "control command=%d payload_length=%zu\n",
               command, payload_length);

    switch (command) {
    case CONTROL_NOP:
        /* duh! */
        break;

    case CONTROL_TCACHE_INVALIDATE:
        control_tcache_invalidate(instance, payload, payload_length);
        break;

    case CONTROL_DUMP_POOLS:
        pool_dump_tree(instance->pool);
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
        if (payload_length == 1)
            daemon_log_config.verbose = *(const uint8_t *)payload;
        break;

    case CONTROL_FADE_CHILDREN:
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
BpInstance::OnControlError(Error &&error)
{
    daemon_log(2, "%s\n", error.GetMessage());
}

bool
global_control_handler_init(BpInstance *instance)
{
    if (instance->config.control_listen == NULL)
        return true;

    struct in_addr group_buffer;
    const struct in_addr *group = NULL;
    if (instance->config.multicast_group != NULL) {
        group_buffer.s_addr = inet_addr(instance->config.multicast_group);
        group = &group_buffer;
    }

    instance->control_distribute = new ControlDistribute(*instance);

    Error error;
    instance->control_server =
        new ControlServer(*instance->control_distribute);
    if (!instance->control_server->OpenPort(instance->config.control_listen,
                                            5478, group,
                                            error)) {
        daemon_log(1, "%s\n", error.GetMessage());
        return false;
    }

    return true;
}

void
global_control_handler_deinit(BpInstance *instance)
{
    delete instance->control_server;
    delete instance->control_distribute;
}

void
global_control_handler_enable(BpInstance &instance)
{
    if (instance.control_server != nullptr)
        instance.control_server->Enable();
}

void
global_control_handler_disable(BpInstance &instance)
{
    if (instance.control_server != nullptr)
        instance.control_server->Disable();
}

int
global_control_handler_add_fd(BpInstance *instance)
{
    assert(instance->control_server != NULL);
    assert(instance->control_distribute != nullptr);

    return instance->control_distribute->Add();
}

void
global_control_handler_set_fd(BpInstance *instance, int fd)
{
    assert(instance->control_server != NULL);
    assert(instance->control_distribute != nullptr);

    instance->control_distribute->Clear();
    instance->control_server->SetFd(fd);
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

bool
local_control_handler_open(BpInstance *instance)
{
    Error error;
    if (!control_local_open(instance->local_control_server, error)) {
        daemon_log(1, "%s\n", error.GetMessage());
        return false;
    }

    return true;
}
