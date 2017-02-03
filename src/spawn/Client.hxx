/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SPAWN_CLIENT_HXX
#define BENG_PROXY_SPAWN_CLIENT_HXX

#include "Interface.hxx"
#include "Config.hxx"
#include "event/SocketEvent.hxx"

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

    const SpawnConfig config;

    int fd;

    unsigned last_pid = 0;

    std::map<int, ChildProcess> processes;

    SocketEvent read_event;

    bool shutting_down = false;

public:
    explicit SpawnServerClient(EventLoop &event_loop,
                               const SpawnConfig &_config, int _fd);
    ~SpawnServerClient();

    void ReplaceSocket(int new_fd);

    void Shutdown();

    int Connect();

private:
    int MakePid() {
        ++last_pid;
        if (last_pid >= 0x40000000)
            last_pid = 1;
        return last_pid;
    }

    void Close();

    /**
     * Check if the spawner is alive, and if not, commit suicide, and
     * hope this daemon gets restarted automatically with a fresh
     * spawner; there's not much else we can do without a spawner.
     * Failing hard and awaiting a restart is better than failing
     * softly over and over.
     */
    void CheckOrAbort();

    void Send(ConstBuffer<void> payload, ConstBuffer<int> fds);
    void Send(const SpawnSerializer &s);

    void HandleExitMessage(SpawnPayload payload);
    void HandleMessage(ConstBuffer<uint8_t> payload);
    void OnSocketEvent(unsigned events);

public:
    /* virtual methods from class SpawnService */
    int SpawnChildProcess(const char *name, PreparedChildProcess &&params,
                          ExitListener *listener) override;

    void SetExitListener(int pid, ExitListener *listener) override;

    void KillChildProcess(int pid, int signo) override;
};

#endif
