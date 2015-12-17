/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_config.hxx"
#include "address_resolver.hxx"
#include "stopwatch.hxx"
#include "pool.hxx"
#include "ua_classification.hxx"
#include "util/Error.hxx"

#include <daemon/daemonize.h>
#include <daemon/log.h>
#include <socket/resolver.h>

#include <glib.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>

#ifndef NDEBUG
/* hack: variable from args.c; to be removed after all widgets have
   been fixed */
extern char ARGS_ESCAPE_CHAR;
#endif

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
         " --no-daemon\n"
#endif
         " -D             don't detach (daemonize)\n"
#ifdef __GLIBC__
         " --pidfile file\n"
#endif
         " -P file        create a pid file\n"
#ifdef __GLIBC__
         " --user name\n"
#endif
         " -u name        switch to another user id\n"
#ifdef __GLIBC__
         " --group name\n"
#endif
         " -g name        switch to another group id\n"
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

static ListenerConfig
ParseListenerConfig(const char *argv0, const char *s)
{
    ListenerConfig config;

    const char *equals = strchr(s, '=');
    if (equals != nullptr) {
        config.tag.assign(s, equals);
        s = equals + 1;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    int result = socket_resolve_host_port(s,
                                          debug_mode ? 8080 : 80,
                                          &hints,
                                          &config.address);
    if (result != 0)
        arg_error(argv0, "failed to resolve %s", s);

    return config;
}

static bool http_cache_size_set = false;

static void
handle_set2(BpConfig *config, struct pool *pool, const char *argv0,
            const char *name, size_t name_length, const char *value)
{
    static const char session_cookie[] = "session_cookie";
    static const char dynamic_session_cookie[] = "dynamic_session_cookie";
    static const char session_idle_timeout[] = "session_idle_timeout";
    static const char session_save_path[] = "session_save_path";
    static const char max_connections[] = "max_connections";
    static const char tcp_stock_limit[] = "tcp_stock_limit";
    static const char fcgi_stock_limit[] = "fastcgi_stock_limit";
    static const char fcgi_stock_max_idle[] = "fastcgi_stock_max_idle";
    static const char was_stock_limit[] = "was_stock_limit";
    static const char was_stock_max_idle[] = "was_stock_max_idle";
    static const char http_cache_size[] = "http_cache_size";
    static const char filter_cache_size[] = "filter_cache_size";
#ifdef HAVE_LIBNFS
    static const char nfs_cache_size[] = "nfs_cache_size";
#endif
    static const char translate_cache_size[] = "translate_cache_size";
    static const char translate_stock_limit[] = "translate_stock_limit";
    static const char stopwatch[] = "stopwatch";
    static const char dump_widget_tree[] = "dump_widget_tree";
    static const char verbose_response[] = "verbose_response";
#ifndef NDEBUG
    static const char args_escape_char[] = "args_escape_char";
#endif
    char *endptr;
    long l;

    if (name_length == sizeof(max_connections) - 1 &&
        memcmp(name, max_connections, sizeof(max_connections) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l <= 0 || l >= 1024 * 1024)
            arg_error(argv0, "Invalid value for max_connections");

        config->max_connections = l;
    } else if (name_length == sizeof(tcp_stock_limit) - 1 &&
               memcmp(name, tcp_stock_limit,
                      sizeof(tcp_stock_limit) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for tcp_stock_limit");

        config->tcp_stock_limit = l;
    } else if (name_length == sizeof(fcgi_stock_limit) - 1 &&
               memcmp(name, fcgi_stock_limit,
                      sizeof(fcgi_stock_limit) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for fastcgi_stock_limit");

        config->fcgi_stock_limit = l;
    } else if (name_length == sizeof(fcgi_stock_max_idle) - 1 &&
               memcmp(name, fcgi_stock_max_idle,
                      sizeof(fcgi_stock_max_idle) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for fastcgi_stock_max_idle");

        config->fcgi_stock_max_idle = l;
    } else if (name_length == sizeof(was_stock_limit) - 1 &&
               memcmp(name, was_stock_limit,
                      sizeof(was_stock_limit) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for was_stock_limit");

        config->was_stock_limit = l;
    } else if (name_length == sizeof(was_stock_max_idle) - 1 &&
               memcmp(name, was_stock_max_idle,
                      sizeof(was_stock_max_idle) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for was_stock_max_idle");

        config->was_stock_max_idle = l;
    } else if (name_length == sizeof(http_cache_size) - 1 &&
               memcmp(name, http_cache_size,
                      sizeof(http_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for http_cache_size");

        config->http_cache_size = l;
        http_cache_size_set = true;
    } else if (name_length == sizeof(filter_cache_size) - 1 &&
               memcmp(name, filter_cache_size,
                      sizeof(filter_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for filter_cache_size");

        config->filter_cache_size = l;
#ifdef HAVE_LIBNFS
    } else if (name_length == sizeof(nfs_cache_size) - 1 &&
               memcmp(name, nfs_cache_size,
                      sizeof(nfs_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for nfs_cache_size");

        config->nfs_cache_size = l;
#endif
    } else if (name_length == sizeof(translate_cache_size) - 1 &&
               memcmp(name, translate_cache_size,
                      sizeof(translate_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for translate_cache_size");

        config->translate_cache_size = l;
    } else if (name_length == sizeof(translate_stock_limit) - 1 &&
               memcmp(name, translate_stock_limit,
                      sizeof(translate_stock_limit) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for translate_stock_limit");

        config->translate_stock_limit = l;
    } else if (name_length == sizeof(stopwatch) - 1 &&
               memcmp(name, stopwatch, sizeof(stopwatch) - 1) == 0) {
        if (strcmp(value, "yes") == 0)
            stopwatch_enable();
        else if (strcmp(value, "no") != 0)
            arg_error(argv0, "Invalid value for stopwatch");
    } else if (name_length == sizeof(dump_widget_tree) - 1 &&
               memcmp(name, dump_widget_tree,
                      sizeof(dump_widget_tree) - 1) == 0) {
        if (strcmp(value, "yes") == 0)
            config->dump_widget_tree = true;
        else if (strcmp(value, "no") != 0)
            arg_error(argv0, "Invalid value for dump_widget_tree");
    } else if (name_length == sizeof(verbose_response) - 1 &&
               memcmp(name, verbose_response,
                      sizeof(verbose_response) - 1) == 0) {
        if (strcmp(value, "yes") == 0)
            config->verbose_response = true;
        else if (strcmp(value, "no") != 0)
            arg_error(argv0, "Invalid value for verbose_response");
#ifndef NDEBUG
    } else if (name_length == sizeof(args_escape_char) - 1 &&
               memcmp(name, args_escape_char,
                      sizeof(args_escape_char) - 1) == 0) {
        if (value[0] != 0 && value[1] == 0)
            ARGS_ESCAPE_CHAR = value[0];
        else
            arg_error(argv0, "Invalid value for args_escape_char");
#endif
    } else if (name_length == sizeof(session_cookie) - 1 &&
               memcmp(name, session_cookie,
                      sizeof(session_cookie) - 1) == 0) {
        if (*value == 0)
            arg_error(argv0, "Invalid value for session_cookie");

        config->session_cookie = p_strdup(pool, value);
    } else if (name_length == sizeof(dynamic_session_cookie) - 1 &&
               memcmp(name, dynamic_session_cookie, sizeof(dynamic_session_cookie) - 1) == 0) {
        if (strcmp(value, "yes") == 0)
            config->dynamic_session_cookie = true;
        else if (strcmp(value, "no") != 0)
            arg_error(argv0, "Invalid value for dynamic_session_cookie");
    } else if (name_length == sizeof(session_idle_timeout) - 1 &&
               memcmp(name, session_idle_timeout,
                      sizeof(session_idle_timeout) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l <= 0)
            arg_error(argv0, "Invalid value for session_idle_timeout");

        config->session_idle_timeout = l;
    } else if (name_length == sizeof(session_save_path) - 1 &&
               memcmp(name, session_save_path,
                      sizeof(session_save_path) - 1) == 0) {
        config->session_save_path = value;
    } else
        arg_error(argv0, "Unknown variable: %.*s", (int)name_length, name);
}

static void
handle_set(BpConfig *config, struct pool *pool,
           const char *argv0, const char *p)
{
    const char *eq;

    eq = strchr(p, '=');
    if (eq == NULL)
        arg_error(argv0, "No '=' found in --set argument");

    if (eq == p)
        arg_error(argv0, "No name found in --set argument");

    handle_set2(config, pool, argv0, p, eq - p, eq + 1);
}

/** read configuration options from the command line */
void
parse_cmdline(BpConfig *config, struct pool *pool, int argc, char **argv)
{
    int ret;
    char *endptr;
#ifdef __GLIBC__
    static const struct option long_options[] = {
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
        {"verbose", 0, NULL, 'v'},
        {"quiet", 0, NULL, 'q'},
        {"logger", 1, NULL, 'l'},
        {"access-logger", 1, NULL, 'A'},
        {"no-daemon", 0, NULL, 'D'},
        {"pidfile", 1, NULL, 'P'},
        {"user", 1, NULL, 'u'},
        {"group", 1, NULL, 'g'},
        {"logger-user", 1, NULL, 'U'},
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
    GError *error = NULL;
    Error error2;

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv,
                          "hVvqDP:l:A:u:g:U:p:L:c:m:w:r:t:M:B:C:N:s:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv,
                     "hVvqDP:l:A:u:g:U:p:L:c:m:w:r:t:M:B:C:N:s:");
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
            daemon_config.detach = 0;
            break;

        case 'P':
            daemon_config.pidfile = optarg;
            break;

        case 'l':
            daemon_config.logger = *optarg == 0 ? nullptr : optarg;
            break;

        case 'A':
            config->access_logger = *optarg == 0
                ? NULL : optarg;
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

        case 'U':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a user in debug mode");

            daemon_user_by_name(&daemon_config.logger_user, optarg, NULL);
            break;

        case 'p':
            if (config->ports.full())
                arg_error(argv[0], "too many listener ports");
            ret = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --port");
            if (ret <= 0 || ret > 0xffff)
                arg_error(argv[0], "invalid port after --port");
            config->ports.push_back(ret);
            break;

        case 'L':
            config->listen.push_front(ParseListenerConfig(argv[0], optarg));
            break;

        case 'c':
            config->control_listen = optarg;
            break;

        case 'm':
            config->multicast_group = optarg;
            break;

        case 'w':
            config->num_workers = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --workers");
            if (config->num_workers > 1024)
                arg_error(argv[0], "too many workers configured");
            break;

        case 'r':
            config->document_root = optarg;
            break;

        case 't':
            config->translation_socket = optarg;
            break;

        case 'M':
            if (config->memcached_server != NULL)
                arg_error(argv[0], "duplicate memcached-server option");

            memset(&hints, 0, sizeof(hints));
            hints.ai_socktype = SOCK_STREAM;

            config->memcached_server =
                address_list_resolve_new(pool, optarg, 11211, &hints, &error);
            if (config->memcached_server == NULL)
                arg_error(argv[0], "%s", error->message);

            break;

        case 'B':
            config->bulldog_path = optarg;
            break;

        case 'C':
            config->cluster_size = strtoul(optarg, &endptr, 10);
            if (endptr == optarg || *endptr != 0 ||
                config->cluster_size > 1024)
                arg_error(argv[0], "Invalid cluster size number");

            if (config->cluster_node >= config->cluster_size)
                config->cluster_node = 0;
            break;

        case 'N':
            config->cluster_node = strtoul(optarg, &endptr, 10);
            if (endptr == optarg || *endptr != 0)
                arg_error(argv[0], "Invalid cluster size number");

            if ((config->cluster_node != 0 || config->cluster_size != 0) &&
                config->cluster_node >= config->cluster_size)
                arg_error(argv[0], "Cluster node too large");
            break;

        case 'a':
            if (!ua_classification_init(optarg, error2)) {
                fprintf(stderr, "%s\n", error2.GetMessage());
                exit(EXIT_FAILURE);
            }

            break;

        case 's':
            handle_set(config, pool, argv[0], optarg);
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
        daemon_user_by_name(&config->user, user_name, group_name);
        if (!daemon_user_defined(&config->user))
            arg_error(argv[0], "refusing to run as root");
    } else if (group_name != NULL)
        arg_error(argv[0], "cannot set --group without --user");
    else if (!debug_mode)
        arg_error(argv[0], "no user name specified (-u)");

    if (config->memcached_server != NULL && http_cache_size_set)
        arg_error(argv[0], "can't specify both --memcached-server and http_cache_size");
}
