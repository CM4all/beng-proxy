/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "config.h"
#include "uri-resolver.h"
#include "stopwatch.h"

#include <daemon/daemonize.h>
#include <daemon/log.h>
#include <socket/resolver.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <netdb.h>

static void usage(void) {
    puts("usage: cm4all-beng-proxy [options]\n\n"
         "valid options:\n"
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
         " -l program     specifies a logger program (executed by /bin/sh)\n"
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
         " --logger-user name\n"
#endif
         " -U name        execute the logger program with this user id\n"
#ifdef __GLIBC__
         " --port PORT\n"
#endif
         " -p PORT        the TCP port beng-proxy listens on\n"
#ifdef __GLIBC__
         " --listen IP:PORT\n"
#endif
         " -L IP:PORT     listen on this IP address\n"
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
handle_set2(struct config *config, const char *argv0,
            const char *name, size_t name_length, const char *value)
{
    static const char max_connections[] = "max_connections";
    static const char tcp_stock_limit[] = "tcp_stock_limit";
    static const char fcgi_stock_limit[] = "fastcgi_stock_limit";
    static const char http_cache_size[] = "http_cache_size";
    static const char filter_cache_size[] = "filter_cache_size";
    static const char translate_cache_size[] = "translate_cache_size";
    static const char stopwatch[] = "stopwatch";
    static const char enable_splice[] = "enable_splice";
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
    } else if (name_length == sizeof(http_cache_size) - 1 &&
               memcmp(name, http_cache_size,
                      sizeof(http_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for http_cache_size");

        config->http_cache_size = l;
    } else if (name_length == sizeof(filter_cache_size) - 1 &&
               memcmp(name, filter_cache_size,
                      sizeof(filter_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for filter_cache_size");

        config->filter_cache_size = l;
    } else if (name_length == sizeof(translate_cache_size) - 1 &&
               memcmp(name, translate_cache_size,
                      sizeof(translate_cache_size) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for translate_cache_size");

        config->translate_cache_size = l;
    } else if (name_length == sizeof(stopwatch) - 1 &&
               memcmp(name, stopwatch, sizeof(stopwatch) - 1) == 0) {
        if (strcmp(value, "yes") == 0)
            stopwatch_enable();
        else if (strcmp(value, "no") != 0)
            arg_error(argv0, "Invalid value for stopwatch");
    } else if (name_length == sizeof(enable_splice) - 1 &&
               memcmp(name, enable_splice, sizeof(enable_splice) - 1) == 0) {
        if (strcmp(value, "no") == 0)
            config->enable_splice = false;
        else if (strcmp(value, "yes") != 0)
            arg_error(argv0, "Invalid value for enable_splice");
    } else
        arg_error(argv0, "Unknown variable: %.*s", (int)name_length, name);
}

static void
handle_set(struct config *config, const char *argv0, const char *p)
{
    const char *eq;

    eq = strchr(p, '=');
    if (eq == NULL)
        arg_error(argv0, "No '=' found in --set argument");

    if (eq == p)
        arg_error(argv0, "No name found in --set argument");

    handle_set2(config, argv0, p, eq - p, eq + 1);
}

/** read configuration options from the command line */
void
parse_cmdline(struct config *config, pool_t pool, int argc, char **argv)
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
        {"pidfile", 1, NULL, 'P'},
        {"user", 1, NULL, 'u'},
        {"logger-user", 1, NULL, 'U'},
        {"port", 1, NULL, 'p'},
        {"listen", 1, NULL, 'L'},
        {"workers", 1, NULL, 'w'},
        {"document-root", 1, NULL, 'r'},
        {"translation-socket", 1, NULL, 't'},
        {"memcached-server", 1, NULL, 'M'},
        {"bulldog-path", 1, NULL, 'B'},
        {"set", 1, NULL, 's'},
        {NULL,0,NULL,0}
    };
#endif
    struct addrinfo hints;

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv, "hVvqDP:l:u:U:p:L:w:r:t:M:B:s:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv, "hVvqDP:l:u:U:p:L:w:r:t:M:B:s:");
#endif
        if (ret == -1)
            break;

        switch (ret) {
        case 'h':
            usage();
            exit(0);

        case 'V':
            printf("cm4all-workshop v%s\n", VERSION);
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
            daemon_config.logger = optarg;
            break;

        case 'u':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a user in debug mode");

            daemon_user_by_name(&daemon_config.user, optarg, NULL);
            if (!daemon_user_defined(&daemon_config.user))
                arg_error(argv[0], "refusing to run as root");
            break;

        case 'U':
            if (debug_mode)
                arg_error(argv[0], "cannot specify a user in debug mode");

            daemon_user_by_name(&daemon_config.logger_user, optarg, NULL);
            break;

        case 'p':
            if (config->num_ports >= MAX_PORTS)
                arg_error(argv[0], "too many listener ports");
            ret = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --port");
            if (ret <= 0 || ret > 0xffff)
                arg_error(argv[0], "invalid port after --port");
            config->ports[config->num_ports++] = ret;
            break;

        case 'L':
            if (config->num_listen >= MAX_LISTEN)
                arg_error(argv[0], "too many listeners");

            memset(&hints, 0, sizeof(hints));
            hints.ai_socktype = SOCK_STREAM;
            hints.ai_flags = AI_PASSIVE;

            ret = socket_resolve_host_port(optarg,
                                           debug_mode ? 8080 : 80,
                                           &hints,
                                           &config->listen[config->num_listen]);
            if (ret != 0)
                arg_error(argv[0], "failed to resolve %s", optarg);
            ++config->num_listen;
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

            config->memcached_server = uri_address_new_resolve(pool, optarg,
                                                               11211, &hints);
            break;

        case 'B':
            config->bulldog_path = optarg;
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

    if (!debug_mode && !daemon_user_defined(&daemon_config.user))
        arg_error(argv[0], "no user name specified (-u)");
}
