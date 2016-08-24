/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_cmdline.hxx"
#include "bp_config.hxx"
#include "net/AddressInfo.hxx"
#include "net/Resolver.hxx"
#include "pool.hxx"
#include "ua_classification.hxx"
#include "util/StringView.hxx"
#include "util/Error.hxx"
#include "util/IterableSplitString.hxx"

#include <daemon/log.h>

#include <systemd/sd-daemon.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>
#include <pwd.h>
#include <grp.h>

enum Options {
    START = 0x100,
    ALLOW_USER,
    ALLOW_GROUP,
    SPAWN_USER,
};

BpCmdLine::BpCmdLine()
{
    memset(&user, 0, sizeof(user));
    memset(&logger_user, 0, sizeof(logger_user));
}

static void usage(void) {
    puts("usage: cm4all-beng-proxy [options]\n\n"
         "valid options:\n"
#ifdef __GLIBC__
         " --help\n"
#endif
         " -h             help (this text)\n"
#ifdef __GLIBC__
         " --version\n"
#endif
         " -V             show cm4all-beng-proxy version\n"
#ifdef __GLIBC__
         " --verbose\n"
#endif
         " -v             be more verbose\n"
#ifdef __GLIBC__
         " --quiet\n"
#endif
         " -q             be quiet\n"
#ifdef __GLIBC__
         " --logger program\n"
#endif
         " -l program     specifies an error logger program (executed by /bin/sh)\n"
#ifdef __GLIBC__
         " --access-logger program\n"
#endif
         " -A program     specifies an access logger program (executed by /bin/sh)\n"
         "                \"internal\" logs into the error log\n"
         "                \"null\" disables the access logger\n"
#ifdef __GLIBC__
         " --config-file file\n"
#endif
         " -f file        load this configuration file\n"
#ifdef __GLIBC__
         " --user name\n"
#endif
         " -u name        switch to another user id\n"
#ifdef __GLIBC__
         " --group name\n"
#endif
         " -g name        switch to another group id\n"
         " --allow-user NAME,NAME,...\n"
         "                allow spawning child processes as the given users\n"
         " --allow-group NAME,NAME,...\n"
         "                allow spawning child processes as the given groups\n"
         " --spawn-user USER[:GROUP]\n"
         "                spawn child processes as this user/group by default\n"
#ifdef __GLIBC__
         " --logger-user name\n"
#endif
         " -U name        execute the error logger program with this user id\n"
#ifdef __GLIBC__
         " --port PORT\n"
#endif
         " -p PORT        the TCP port beng-proxy listens on\n"
#ifdef __GLIBC__
         " --listen [TAG=]IP:PORT\n"
#endif
         " -L IP:PORT     listen on this IP address\n"
#ifdef __GLIBC__
         " --control-listen IP:PORT\n"
#endif
         " -c IP:PORT     listen on this UDP port for control commands\n"
#ifdef __GLIBC__
         " --multicast-group IP\n"
#endif
         " -m IP          join this multicast group\n"
#ifdef __GLIBC__
         " --workers COUNT\n"
#endif
         " -w COUNT       set the number of worker processes; 0=don't fork\n"
#ifdef __GLIBC__
         " --document-root DIR\n"
#endif
         " -r DIR         set the document root\n"
#ifdef __GLIBC__
         " --translation-socket PATH\n"
#endif
         " -t PATH        set the path to the translation server socket\n"
#ifdef __GLIBC__
         " --memcached-server IP:PORT\n"
#endif
         " -M IP:PORT     use this memcached server\n"
#ifdef __GLIBC__
         " --bulldog-path PATH\n"
#endif
         " -B PATH        obtain worker status information from the Bulldog-Tyke path\n"
#ifdef __GLIBC__
         " --cluster-size N\n"
#endif
         " -C N           set the size of the beng-lb cluster\n"
#ifdef __GLIBC__
         " --cluster-node N\n"
#endif
         " -N N           set the index of this node in the beng-lb cluster\n"
#ifdef __GLIBC__
         " --ua-classes PATH\n"
#endif
         " -a PATH        load the User-Agent classification rules from this file\n"
#ifdef __GLIBC__
         " --set NAME=VALUE  tweak an internal variable, see manual for details\n"
#endif
         " -s NAME=VALUE  \n"
         "\n"
         );
}

static void arg_error(const char *argv0, const char *fmt, ...)
     __attribute__ ((noreturn))
     __attribute__((format(printf,2,3)));
static void arg_error(const char *argv0, const char *fmt, ...) {
    if (fmt != NULL) {
        va_list ap;

        fputs(argv0, stderr);
        fputs(": ", stderr);

        va_start(ap, fmt);
        vfprintf(stderr, fmt, ap);
        va_end(ap);

        putc('\n', stderr);
    }

    fprintf(stderr, "Try '%s --help' for more information.\n",
            argv0);
    exit(1);
}

static void
ParseListenerConfig(const char *s,
                    std::forward_list<BpConfig::Listener> &list)
{
    std::string tag;

    const char *equals = strchr(s, '=');
    if (equals != nullptr) {
        tag.assign(s, equals);
        s = equals + 1;
    }

    if (*s == '/' || *s == '@') {
        AllocatedSocketAddress address;
        address.SetLocal(s);
        list.emplace_front(std::move(address), tag);
        return;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = AI_ADDRCONFIG|AI_PASSIVE;
    hints.ai_socktype = SOCK_STREAM;

    for (const auto &i : Resolve(s, debug_mode ? 8080 : 80, &hints))
        list.emplace_front(i, tag);
}

template<typename F>
static void
SplitForEach(const char *p, char separator, F &&f)
{
    for (auto value : IterableSplitString(p, separator))
        if (!value.IsEmpty())
            f(std::string(value.data, value.size).c_str());
}

static void
ParseAllowUser(SpawnConfig &config, const char *arg)
{
    SplitForEach(arg, ',', [&config](const char *name){
            struct passwd *pw = getpwnam(name);
            if (pw == nullptr) {
                fprintf(stderr, "No such user: %s\n", name);
                exit(EXIT_FAILURE);
            }

            config.allowed_uids.insert(pw->pw_uid);
        });
}

static void
ParseAllowGroup(SpawnConfig &config, const char *arg)
{
    SplitForEach(arg, ',', [&config](const char *name){
            struct group *gr = getgrnam(name);
            if (gr == nullptr) {
                fprintf(stderr, "No such group: %s\n", name);
                exit(EXIT_FAILURE);
            }

            config.allowed_gids.insert(gr->gr_gid);
        });
}

static void
handle_set(BpConfig &config,
           const char *argv0, const char *p)
{
    const char *eq;

    eq = strchr(p, '=');
    if (eq == NULL)
        arg_error(argv0, "No '=' found in --set argument");

    if (eq == p)
        arg_error(argv0, "No name found in --set argument");

    const StringView name(p, eq - p);
    const char *const value = eq + 1;

    try {
        config.HandleSet(name, value);
    } catch (const std::runtime_error &e) {
        arg_error(argv0, "Error while parsing \"--set %.*s\": %s",
                  (int)name.size, name.data, e.what());
    }
}

static void
Copy(UidGid &dest, const struct daemon_user &src)
{
    dest.uid = src.uid;
    dest.gid = src.gid;

    size_t n_groups = std::min(src.num_groups, dest.groups.max_size());
    std::copy_n(src.groups, n_groups, dest.groups.begin());
    if (n_groups < dest.groups.max_size())
        dest.groups[n_groups] = 0;
}

/** read configuration options from the command line */
void
parse_cmdline(BpCmdLine &cmdline, BpConfig &config, int argc, char **argv)
{
    int ret;
    char *endptr;
#ifdef __GLIBC__
    static const struct option long_options[] = {
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
        {"verbose", 0, NULL, 'v'},
        {"quiet", 0, NULL, 'q'},
        {"logger", 1, NULL, 'l'}, /* obsolete */
        {"access-logger", 1, NULL, 'A'},
        {"no-daemon", 0, NULL, 'D'}, /* obsolete */
        {"pidfile", 1, NULL, 'P'}, /* obsolete */
        {"config-file", 1, nullptr, 'f'},
        {"user", 1, NULL, 'u'},
        {"group", 1, NULL, 'g'},
        {"logger-user", 1, NULL, 'U'},
        {"allow-user", 1, NULL, ALLOW_USER},
        {"allow-group", 1, NULL, ALLOW_GROUP},
        {"spawn-user", 1, nullptr, SPAWN_USER},
        {"port", 1, NULL, 'p'},
        {"listen", 1, NULL, 'L'},
        {"control-listen", 1, NULL, 'c'},
        {"multicast-group", 1, NULL, 'm'},
        {"workers", 1, NULL, 'w'},
        {"document-root", 1, NULL, 'r'},
        {"translation-socket", 1, NULL, 't'},
        {"memcached-server", 1, NULL, 'M'},
        {"bulldog-path", 1, NULL, 'B'},
        {"cluster-size", 1, NULL, 'C'},
        {"cluster-node", 1, NULL, 'N'},
        {"ua-classes", 1, NULL, 'a'},
        {"set", 1, NULL, 's'},
        {NULL,0,NULL,0}
    };
#endif
    struct addrinfo hints;
    const char *user_name = NULL, *group_name = NULL;
    const char *spawn_user = nullptr;
    Error error2;

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv,
                          "hVvqDP:l:A:f:u:g:U:p:L:c:m:w:r:t:M:B:C:N:s:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv,
                     "hVvqDP:l:A:f:u:g:U:p:L:c:m:w:r:t:M:B:C:N:s:");
#endif
        if (ret == -1)
            break;

        switch (ret) {
        case 'h':
            usage();
            exit(0);

        case 'V':
            printf("cm4all-beng-proxy v%s\n", VERSION);
            exit(0);

        case 'v':
            ++daemon_log_config.verbose;
            break;

        case 'q':
            daemon_log_config.verbose = 0;
            break;

        case 'D':
        case 'P':
        case 'l':
            /* obsolete */
            break;

        case 'A':
            cmdline.access_logger = *optarg == 0
                ? NULL : optarg;
            break;

        case 'f':
            cmdline.config_file = optarg;
            break;

        case 'u':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a user in debug mode");

            user_name = optarg;
            break;

        case 'g':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a group in debug mode");

            group_name = optarg;
            break;

        case ALLOW_USER:
            ParseAllowUser(config.spawn, optarg);
            break;

        case ALLOW_GROUP:
            ParseAllowGroup(config.spawn, optarg);
            break;

        case SPAWN_USER:
            if (*optarg != 0)
                spawn_user = optarg;
            break;

        case 'U':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a user in debug mode");

            daemon_user_by_name(&cmdline.logger_user, optarg, NULL);
            break;

        case 'p':
            if (config.ports.full())
                arg_error(argv[0], "too many listener ports");
            ret = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --port");
            if (ret <= 0 || ret > 0xffff)
                arg_error(argv[0], "invalid port after --port");
            config.ports.push_back(ret);
            break;

        case 'L':
            ParseListenerConfig(optarg, config.listen);
            break;

        case 'c':
            config.control_listen = optarg;
            break;

        case 'm':
            config.multicast_group = optarg;
            break;

        case 'w':
            config.num_workers = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --workers");
            if (config.num_workers > 1024)
                arg_error(argv[0], "too many workers configured");

            if (config.num_workers == 1 && sd_booted())
                /* we don't need a watchdog process if systemd watches
                   on us */
                config.num_workers = 0;

            break;

        case 'r':
            config.document_root = optarg;
            break;

        case 't':
            config.translation_socket = optarg;
            break;

        case 'M':
            if (!config.memcached_server.empty())
                arg_error(argv[0], "duplicate memcached-server option");

            memset(&hints, 0, sizeof(hints));
            hints.ai_flags = AI_ADDRCONFIG;
            hints.ai_socktype = SOCK_STREAM;

            config.memcached_server = Resolve(optarg, 11211, &hints);
            break;

        case 'B':
            config.bulldog_path = optarg;
            break;

        case 'C':
            config.cluster_size = strtoul(optarg, &endptr, 10);
            if (endptr == optarg || *endptr != 0 ||
                config.cluster_size > 1024)
                arg_error(argv[0], "Invalid cluster size number");

            if (config.cluster_node >= config.cluster_size)
                config.cluster_node = 0;
            break;

        case 'N':
            config.cluster_node = strtoul(optarg, &endptr, 10);
            if (endptr == optarg || *endptr != 0)
                arg_error(argv[0], "Invalid cluster size number");

            if ((config.cluster_node != 0 || config.cluster_size != 0) &&
                config.cluster_node >= config.cluster_size)
                arg_error(argv[0], "Cluster node too large");
            break;

        case 'a':
            if (!ua_classification_init(optarg, error2)) {
                fprintf(stderr, "%s\n", error2.GetMessage());
                exit(EXIT_FAILURE);
            }

            break;

        case 's':
            handle_set(config, argv[0], optarg);
            break;

        case '?':
            arg_error(argv[0], NULL);

        default:
            exit(1);
        }
    }

    /* check non-option arguments */

    if (optind < argc)
        arg_error(argv[0], "unrecognized argument: %s", argv[optind]);

    /* check completeness */

    if (user_name != NULL) {
        daemon_user_by_name(&cmdline.user, user_name, group_name);
        if (!daemon_user_defined(&cmdline.user))
            arg_error(argv[0], "refusing to run as root");
    } else if (group_name != NULL)
        arg_error(argv[0], "cannot set --group without --user");
    else if (!debug_mode)
        arg_error(argv[0], "no user name specified (-u)");

    if (!config.memcached_server.empty() && config.http_cache_size_set)
        arg_error(argv[0], "can't specify both --memcached-server and http_cache_size");

    if (debug_mode) {
        if (spawn_user != nullptr)
            arg_error(argv[0], "cannot set --spawn-user in debug mode");

        config.spawn.default_uid_gid.LoadEffective();
    } else if (spawn_user != nullptr) {
        struct daemon_user u;
        if (daemon_user_by_name(&u, spawn_user, nullptr) < 0)
            arg_error(argv[0], "Failed to look up user '%s'", spawn_user);

        if (!daemon_user_defined(&cmdline.user))
            arg_error(argv[0], "refusing to spawn child processes as root");

        Copy(config.spawn.default_uid_gid, u);
        config.spawn.ignore_userns = true;

        config.spawn.allowed_uids.insert(u.uid);
        config.spawn.allowed_gids.insert(u.gid);
        for (size_t i = 0; i < u.num_groups; ++i)
            config.spawn.allowed_gids.insert(u.groups[i]);
    } else {
        Copy(config.spawn.default_uid_gid, cmdline.user);
        config.spawn.ignore_userns = true;
    }

    assert(config.spawn.default_uid_gid.IsComplete());
}
