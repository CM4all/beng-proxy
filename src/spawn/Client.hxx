/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_CLIENT_HXX
#define BENG_PROXY_SPAWN_CLIENT_HXX

#include "Interface.hxx"
#include "event/Event.hxx"

#include <map>

template<typename T> struct ConstBuffer;
struct PreparedChildProcess;
class SpawnPayload;
class SpawnSerializer;

class SpawnServerClient final : public SpawnService {
    struct ChildProcess {
        ExitListener *listener;

        explicit ChildProcess(ExitListener *_listener)
            :listener(_listener) {}
    };

    int fd;

    unsigned last_pid = 0;

    std::map<int, ChildProcess> processes;

    Event read_event;

public:
    explicit SpawnServerClient(int _fd);
    ~SpawnServerClient();

    void ReplaceSocket(int new_fd);

    void Disable() {
        read_event.Delete();
    }

    int Connect();

private:
    int MakePid() {
        ++last_pid;
        if (last_pid >= 0x40000000)
            last_pid = 1;
        return last_pid;
    }

    void Send(ConstBuffer<void> payload, ConstBuffer<int> fds);
    void Send(const SpawnSerializer &s);

    void HandleExitMessage(SpawnPayload payload);
    void HandleMessage(ConstBuffer<uint8_t> payload);
    void ReadEventCallback();

public:
    /* virtual methods from class SpawnService */
    int SpawnChildProcess(const char *name, PreparedChildProcess &&params,
                          ExitListener *listener,
                          GError **error_r) override;

    void SetExitListener(int pid, ExitListener *listener) override;

    void KillChildProcess(int pid, int signo) override;
};

#endif
