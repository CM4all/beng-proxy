/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "config.h"
#include "stopwatch.h"

#include <daemon/daemonize.h>
#include <daemon/log.h>
#include <socket/resolver.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

static void usage(void) {
    puts("usage: cm4all-beng-lb [options]\n\n"
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
         " --config-file PATH\n"
#endif
         " -f PATH        load this configuration file instead of /etc/cm4all/beng/lb.conf\n"
#ifdef __GLIBC__
         " --check        check configuration file syntax\n"
#else
         " -C             check configuration file syntax\n"
#endif
#ifdef __GLIBC__
         " --logger program\n"
#endif
         " -l program     specifies an error logger program (executed by /bin/sh)\n"
#ifdef __GLIBC__
         " --access-logger program\n"
#endif
         " -A program     specifies an access logger program (executed by /bin/sh)\n"
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
         " --watchdog\n"
#endif
         " -W             enable the watchdog that auto-restarts beng-lb on crash\n"
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
    static const char stopwatch[] = "stopwatch";
    static const char dump_widget_tree[] = "dump_widget_tree";
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
parse_cmdline(struct config *config, struct pool *pool, int argc, char **argv)
{
    int ret;
#ifdef __GLIBC__
    static const struct option long_options[] = {
        {"help", 0, NULL, 'h'},
        {"version", 0, NULL, 'V'},
        {"verbose", 0, NULL, 'v'},
        {"quiet", 0, NULL, 'q'},
        {"config-file", 1, NULL, 'f'},
        {"check", 0, NULL, 'C'},
        {"logger", 1, NULL, 'l'},
        {"access-logger", 1, NULL, 'A'},
        {"no-daemon", 0, NULL, 'D'},
        {"pidfile", 1, NULL, 'P'},
        {"user", 1, NULL, 'u'},
        {"group", 1, NULL, 'g'},
        {"logger-user", 1, NULL, 'U'},
        {"watchdog", 0, NULL, 'W'},
        {"bulldog-path", 1, NULL, 'B'},
        {"set", 1, NULL, 's'},
        {NULL,0,NULL,0}
    };
#endif
    const char *user_name = NULL, *group_name = NULL;

    (void)pool;

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv, "hVvqf:CDP:l:A:u:g:U:WB:s:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv, "hVvqf:CDP:l:A:u:g:U:WB:s:");
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

        case 'f':
            config->config_path = optarg;
            break;

        case 'C':
            config->check = true;
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

        case 'A':
            config->access_logger = *optarg == 0
                ? NULL : optarg;
            break;

        case 'u':
            user_name = optarg;
            break;

        case 'g':
            group_name = optarg;
            break;

        case 'U':
            daemon_user_by_name(&daemon_config.logger_user, optarg, NULL);
            break;

        case 'c':
            config->control_listen = optarg;
            break;

        case 'm':
            config->multicast_group = optarg;
            break;

        case 'r':
            config->document_root = optarg;
            break;

        case 't':
            config->translation_socket = optarg;
            break;

        case 'W':
            /* Abusing beng-proxy's "num_workers" attributes for the
               watchdog; non-zero means watchdog is enabled.  That is
               similar enough to beng-proxy's interpretation: 0 means
               don't fork, no auto-restart. */
            config->num_workers = 1;
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

    if (user_name != NULL) {
        daemon_user_by_name(&config->user, user_name, group_name);
        if (!daemon_user_defined(&config->user))
            arg_error(argv[0], "refusing to run as root");
    } else if (group_name != NULL)
        arg_error(argv[0], "cannot set --group without --user");
    else if (geteuid() == 0)
        arg_error(argv[0], "no user name specified (-u)");
}
