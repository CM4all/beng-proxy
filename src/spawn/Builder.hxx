/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_BUILDER_HXX
#define BENG_PROXY_SPAWN_BUILDER_HXX

#include "Protocol.hxx"
#include "system/Error.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StaticArray.hxx"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>

class SpawnPayloadTooLargeError {};

class SpawnSerializer {
    static constexpr size_t capacity = 65536;

    size_t size = 0;

    uint8_t buffer[capacity];

    StaticArray<int, 8> fds;

public:
    explicit SpawnSerializer(SpawnRequestCommand cmd) {
        buffer[size++] = (uint8_t)cmd;
    }

    explicit SpawnSerializer(SpawnResponseCommand cmd) {
        buffer[size++] = (uint8_t)cmd;
    }

    void WriteByte(uint8_t value) {
        if (size >= capacity)
            throw SpawnPayloadTooLargeError();

        buffer[size++] = value;
    }

    void Write(SpawnExecCommand cmd) {
        WriteByte((uint8_t)cmd);
    }

    void WriteOptional(SpawnExecCommand cmd, bool value) {
        if (value)
            Write(cmd);
    }

    void Write(ConstBuffer<void> value) {
        if (size + value.size > capacity)
            throw SpawnPayloadTooLargeError();

        memcpy(buffer + size, value.data, value.size);
        size += value.size;
    }

    template<typename T>
    void WriteT(const T &value) {
        Write(ConstBuffer<void>(&value, sizeof(value)));
    }

    void WriteInt(int value) {
        WriteT(value);
    }

    void WriteString(const char *value) {
        assert(value != nullptr);

        Write(ConstBuffer<void>(value, strlen(value) + 1));
    }

    void WriteString(SpawnExecCommand cmd, const char *value) {
        Write(cmd);
        WriteString(value);
    }

    void WriteOptionalString(SpawnExecCommand cmd, const char *value) {
        if (value != nullptr)
            WriteString(cmd, value);
    }

    void WriteFd(SpawnExecCommand cmd, int fd) {
        assert(fd >= 0);

        if (fds.full())
            throw SpawnPayloadTooLargeError();

        Write(cmd);
        fds.push_back(fd);
    }

    void CheckWriteFd(SpawnExecCommand cmd, int fd) {
        if (fd >= 0)
            WriteFd(cmd, fd);
    }

    ConstBuffer<void> GetPayload() const {
        return {buffer, size};
    }

    ConstBuffer<int> GetFds() const {
        return {fds.begin(), fds.size()};
    }
};

template<size_t MAX_FDS>
class ScmRightsBuilder {
    static constexpr size_t size = CMSG_SPACE(MAX_FDS * sizeof(int));
    static constexpr size_t n_longs = (size + sizeof(long) - 1) / sizeof(long);

    size_t n = 0;
    long buffer[n_longs];

    int *data;

public:
    explicit ScmRightsBuilder(struct msghdr &msg) {
        msg.msg_control = buffer;
        msg.msg_controllen = sizeof(buffer);

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        data = (int *)(void *)CMSG_DATA(cmsg);
    }

    void push_back(int fd) {
        assert(n < MAX_FDS);

        data[n++] = fd;
    }

    void Finish(struct msghdr &msg) {
        msg.msg_controllen = CMSG_SPACE(n * sizeof(int));

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(n * sizeof(int));
    }
};

template<size_t MAX_FDS>
static void
Send(int fd, ConstBuffer<void> payload, ConstBuffer<int> fds)
{
    assert(fd >= 0);

    struct iovec vec = {
        .iov_base = const_cast<void *>(payload.data),
        .iov_len = payload.size,
    };

    struct msghdr msg = {
        .msg_name = nullptr,
        .msg_namelen = 0,
        .msg_iov = &vec,
        .msg_iovlen = 1,
        .msg_control = nullptr,
        .msg_controllen = 0,
        .msg_flags = 0,
    };

    ScmRightsBuilder<MAX_FDS> b(msg);
    for (int i : fds)
        b.push_back(i);
    b.Finish(msg);

    if (sendmsg(fd, &msg, MSG_NOSIGNAL) < 0)
        throw MakeErrno("send() failed");
}

template<size_t MAX_FDS>
static void
Send(int fd, const SpawnSerializer &s)
{
    return Send<MAX_FDS>(fd, s.GetPayload(), s.GetFds());
}

#endif
