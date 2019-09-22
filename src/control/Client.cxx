/*
 * Copyright 2007-2019 Content Management AG
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

#include "beng-proxy/Control.hxx"
#include "translation/Protocol.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "net/SendMessage.hxx"
#include "net/ScmRightsBuilder.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "util/ByteOrder.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringCompare.hxx"

#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>

struct Usage {
    const char *msg = nullptr;
};

class BengControlClient {
    UniqueSocketDescriptor socket;

public:
    explicit BengControlClient(UniqueSocketDescriptor _socket) noexcept
        :socket(std::move(_socket)) {}

    explicit BengControlClient(const char *host_and_port)
        :BengControlClient(ResolveConnectDatagramSocket(host_and_port,
                                                        5478)) {}

    void AutoBind() noexcept {
        socket.AutoBind();
    }

    void Send(BengProxy::ControlCommand cmd, ConstBuffer<void> payload=nullptr,
              ConstBuffer<FileDescriptor> fds=nullptr);

    std::pair<BengProxy::ControlCommand, std::string> Receive();
};

static constexpr size_t
PaddingSize(size_t size) noexcept
{
    return (3 - ((size - 1) & 0x3));
}

void
BengControlClient::Send(BengProxy::ControlCommand cmd,
                        ConstBuffer<void> payload,
                        ConstBuffer<FileDescriptor> fds)
{
    static constexpr uint32_t magic = ToBE32(BengProxy::control_magic);
    const BengProxy::ControlHeader header{ToBE16(payload.size), ToBE16(uint16_t(cmd))};

    static constexpr uint8_t padding[3] = {0, 0, 0};

    struct iovec v[] = {
        { const_cast<uint32_t *>(&magic), sizeof(magic) },
        { const_cast<BengProxy::ControlHeader *>(&header), sizeof(header) },
        { const_cast<void *>(payload.data), payload.size },
        { const_cast<uint8_t *>(padding), PaddingSize(payload.size) },
    };

    MessageHeader msg = ConstBuffer<struct iovec>(v);

    ScmRightsBuilder<1> b(msg);
    for (const auto &i : fds)
        b.push_back(i.Get());
    b.Finish(msg);

    SendMessage(socket, msg, 0);
}

std::pair<BengProxy::ControlCommand, std::string>
BengControlClient::Receive()
{
    int result = socket.WaitReadable(10000);
    if (result < 0)
        throw MakeErrno("poll() failed");

    if (result == 0)
        throw std::runtime_error("Timeout");

    BengProxy::ControlHeader header;
    char payload[4096];

    struct iovec v[] = {
        { &header, sizeof(header) },
        { payload, sizeof(payload) },
    };

    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = v,
        .msg_iovlen = std::size(v),
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    auto nbytes = recvmsg(socket.Get(), &msg, 0);
    if (nbytes < 0)
        throw MakeErrno("recvmsg() failed");

    if (size_t(nbytes) < sizeof(header))
        throw std::runtime_error("Short receive");

    size_t payload_length = FromBE16(header.length);
    if (sizeof(header) + payload_length > size_t(nbytes))
        throw std::runtime_error("Truncated datagram");

    return std::make_pair(BengProxy::ControlCommand(FromBE16(header.command)),
                          std::string(payload, payload_length));
}

static void
SimpleCommand(const char *server, ConstBuffer<const char *> args,
              BengProxy::ControlCommand cmd)
{
    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.Send(cmd);
}

static void
Nop(const char *server, ConstBuffer<const char *> args)
{
    SimpleCommand(server, args, BengProxy::ControlCommand::NOP);
}

static std::string
MakeTcacheInvalidate(TranslationCommand cmd, ConstBuffer<void> payload)
{
    TranslationHeader h;
    h.length = ToBE16(payload.size);
    h.command = TranslationCommand(ToBE16(uint16_t(cmd)));

    std::string result;
    result.append((const char *)&h, sizeof(h));
    if (!payload.empty()) {
        result.append((const char *)payload.data, payload.size);
        result.append(PaddingSize(payload.size), '\0');
    }

    return result;
}

static std::string
MakeTcacheInvalidate(TranslationCommand cmd, const char *value)
{
    return MakeTcacheInvalidate(cmd,
                                ConstBuffer<void>(value, strlen(value) + 1));
}

static constexpr struct {
    const char *name;
    TranslationCommand cmd;
} tcache_invalidate_strings[] = {
    { "URI", TranslationCommand::URI },
    { "PARAM", TranslationCommand::PARAM },
    { "LISTENER_TAG", TranslationCommand::LISTENER_TAG },
    { "REMOTE_HOST", TranslationCommand::REMOTE_HOST },
    { "HOST", TranslationCommand::HOST },
    { "LANGUAGE", TranslationCommand::LANGUAGE },
    { "USER_AGENT", TranslationCommand::USER_AGENT },
    { "QUERY_STRING", TranslationCommand::QUERY_STRING },
    { "SITE", TranslationCommand::SITE },
    { "INTERNAL_REDIRECT", TranslationCommand::INTERNAL_REDIRECT },
    { "ENOTDIR", TranslationCommand::ENOTDIR_ },
    { "USER", TranslationCommand::USER },
};

static std::string
ParseTcacheInvalidate(StringView name, const char *value)
{
    for (const auto &i : tcache_invalidate_strings)
        if (name.Equals(i.name))
            return MakeTcacheInvalidate(i.cmd, value);

    throw FormatRuntimeError("Unrecognized key: '%.*s'",
                             int(name.size), name.data);
}

static std::string
ParseTcacheInvalidate(const char *s)
{
    const char *eq = strchr(s, '=');
    if (eq == nullptr)
        throw FormatRuntimeError("Missing '=': %s", s);

    if (eq == s)
        throw FormatRuntimeError("Missing name: %s", s);

    return ParseTcacheInvalidate({s, eq}, eq + 1);
}

static void
TcacheInvalidate(const char *server, ConstBuffer<const char *> args)
{
    std::string payload;

    for (const char *s : args)
        payload += ParseTcacheInvalidate(s);

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::TCACHE_INVALIDATE,
                {payload.data(), payload.size()});
}

static void
Verbose(const char *server, ConstBuffer<const char *> args)
{
    if (args.empty())
        throw Usage{"Log level missing"};

    const char *s = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    uint8_t log_level = atoi(s);

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::VERBOSE,
                {&log_level, sizeof(log_level)});
}

static void
EnableNode(const char *server, ConstBuffer<const char *> args)
{
    if (args.empty())
        throw Usage{"Node name missing"};

    const StringView name = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::ENABLE_NODE, name.ToVoid());
}

static void
FadeNode(const char *server, ConstBuffer<const char *> args)
{
    if (args.empty())
        throw Usage{"Node name missing"};

    const StringView name = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::FADE_NODE, name.ToVoid());
}

static void
NodeStatus(const char *server, ConstBuffer<const char *> args)
{
    if (args.empty())
        throw Usage{"Node name missing"};

    const StringView name = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.AutoBind();
    client.Send(BengProxy::ControlCommand::NODE_STATUS, name.ToVoid());

    const auto response = client.Receive();
    if (response.first != BengProxy::ControlCommand::NODE_STATUS)
        throw std::runtime_error("Wrong response command");

    const auto nul = response.second.find('\0');
    if (nul == response.second.npos)
        throw std::runtime_error("Malformed response payload");

    printf("%s\n", response.second.c_str() + nul + 1);
}

static void
PrintStatsAttribute(const char *name, const uint32_t &value) noexcept
{
    if (value != 0)
        printf("%s %" PRIu32 "\n", name, FromBE32(value));
}

static void
PrintStatsAttribute(const char *name, const uint64_t &value) noexcept
{
    if (value != 0)
        printf("%s %" PRIu64 "\n", name, FromBE64(value));
}

static void
Stats(const char *server, ConstBuffer<const char *> args)
{
    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.AutoBind();
    client.Send(BengProxy::ControlCommand::STATS);

    const auto response = client.Receive();
    if (response.first != BengProxy::ControlCommand::STATS)
        throw std::runtime_error("Wrong response command");

    BengProxy::ControlStats stats;
    memset(&stats, 0, sizeof(stats));
    memcpy(&stats, response.second.data(),
           std::min(sizeof(stats), response.second.size()));

    PrintStatsAttribute("incoming_connections", stats.incoming_connections);
    PrintStatsAttribute("outgoing_connections", stats.outgoing_connections);
    PrintStatsAttribute("children", stats.children);
    PrintStatsAttribute("sessions", stats.sessions);
    PrintStatsAttribute("http_requests", stats.http_requests);
    PrintStatsAttribute("translation_cache_size", stats.translation_cache_size);
    PrintStatsAttribute("http_cache_size", stats.http_cache_size);
    PrintStatsAttribute("filter_cache_size", stats.filter_cache_size);
    PrintStatsAttribute("translation_cache_brutto_size", stats.translation_cache_brutto_size);
    PrintStatsAttribute("http_cache_brutto_size", stats.http_cache_brutto_size);
    PrintStatsAttribute("filter_cache_brutto_size", stats.filter_cache_brutto_size);
    PrintStatsAttribute("nfs_cache_size", stats.nfs_cache_size);
    PrintStatsAttribute("nfs_cache_brutto_size", stats.nfs_cache_brutto_size);
    PrintStatsAttribute("io_buffers_size", stats.io_buffers_size);
    PrintStatsAttribute("io_buffers_brutto_size", stats.io_buffers_brutto_size);
    PrintStatsAttribute("http_traffic_received", stats.http_traffic_received);
    PrintStatsAttribute("http_traffic_sent", stats.http_traffic_sent);
}

static void
FadeChildren(const char *server, ConstBuffer<const char *> args)
{
    StringView tag = nullptr;

    if (!args.empty())
        tag = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::FADE_CHILDREN, tag.ToVoid());
}

static void
FlushFilterCache(const char *server, ConstBuffer<const char *> args)
{
    StringView tag = nullptr;

    if (!args.empty())
        tag = args.shift();

    if (!args.empty())
        throw Usage{"Too many arguments"};

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::FLUSH_FILTER_CACHE, tag.ToVoid());
}

static void
Stopwatch(const char *server, ConstBuffer<const char *> args)
{
    if (!args.empty())
        throw Usage{"Too many arguments"};

    UniqueFileDescriptor r, w;
    if (!UniqueFileDescriptor::CreatePipe(r, w))
        throw MakeErrno("pipe() failed");

    FileDescriptor fds[] = { w };

    BengControlClient client(server);
    client.Send(BengProxy::ControlCommand::STOPWATCH, nullptr, fds);

    w.Close();

    while (true) {
        char buffer[8192];
        ssize_t nbytes = r.Read(buffer, sizeof(buffer));
        if (nbytes <= 0)
            break;

        if (write(STDOUT_FILENO, buffer, nbytes) < 0)
            break;
    }
}

int
main(int argc, char **argv)
try {
    ConstBuffer<const char *> args(argv + 1, argc - 1);

    const char *server = "@bp-control";

    while (!args.empty() && args.front()[0] == '-') {
        const char *option = args.shift();
        if (const char *new_server = StringAfterPrefix(option, "--server=")) {
            server = new_server;
        } else
            throw Usage{"Unknown option"};
    }

    if (args.empty())
        throw Usage();

    const char *const command = args.shift();

    if (StringIsEqual(command, "nop")) {
        Nop(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "tcache-invalidate")) {
        TcacheInvalidate(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "enable-node")) {
        EnableNode(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "fade-node")) {
        FadeNode(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "node-status")) {
        NodeStatus(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "dump-pools")) {
        SimpleCommand(server, args,
                      BengProxy::ControlCommand::DUMP_POOLS);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "stats")) {
        Stats(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "verbose")) {
        Verbose(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "fade-children")) {
        FadeChildren(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "disable-zeroconf")) {
        SimpleCommand(server, args,
                      BengProxy::ControlCommand::DISABLE_ZEROCONF);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "enable-zeroconf")) {
        SimpleCommand(server, args,
                      BengProxy::ControlCommand::ENABLE_ZEROCONF);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "flush-nfs-cache")) {
        SimpleCommand(server, args, BengProxy::ControlCommand::FLUSH_NFS_CACHE);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "flush-filter-cache")) {
        FlushFilterCache(server, args);
        return EXIT_SUCCESS;
    } else if (StringIsEqual(command, "stopwatch")) {
        Stopwatch(server, args);
        return EXIT_SUCCESS;
    } else
        throw Usage{"Unknown command"};
 } catch (const Usage &u) {
    if (u.msg)
        fprintf(stderr, "%s\n\n", u.msg);

    fprintf(stderr, "Usage: %s [--server=SERVER[:PORT]] COMMAND ...\n"
            "\n"
            "Commands:\n"
            "  nop\n"
            "  tcache-invalidate [KEY=VALUE...]\n"
            "  enable-node NAME:PORT\n"
            "  fade-node NAME:PORT\n"
            "  dump-pools\n"
            "  verbose LEVEL\n"
            "  fade-children [TAG]\n"
            "  disable-zeroconf\n"
            "  enable-zeroconf\n"
            "  flush-nfs-cache\n"
            "  flush-filter-cache [TAG]\n"
            "  stopwatch\n"
            "\n"
            "Names for tcache-invalidate:\n",
            argv[0]);

    for (const auto &i : tcache_invalidate_strings)
        fprintf(stderr, "  %s\n", i.name);

    return EXIT_FAILURE;
 } catch (...) {
    PrintException(std::current_exception());
    return EXIT_FAILURE;
}
