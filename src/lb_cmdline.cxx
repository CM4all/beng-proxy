/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_cmdline.hxx"
#include "stopwatch.hxx"

#include <daemon/log.h>

#include <systemd/sd-daemon.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

LbCmdLine::LbCmdLine()
{
    memset(&user, 0, sizeof(user));
    memset(&logger_user, 0, sizeof(logger_user));
}

static void
PrintUsage()
{
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
         " --access-logger program\n"
#endif
         " -A program     specifies an access logger program (executed by /bin/sh)\n"
         "                \"internal\" logs into the error log\n"
         "                \"null\" disables the access logger\n"
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
         " -U name        execute the access logger program with this user id\n"
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
HandleSet(LbCmdLine *config, const char *argv0,
          const char *name, size_t name_length, const char *value)
{
    static const char tcp_stock_limit[] = "tcp_stock_limit";
    char *endptr;
    long l;

    if (name_length == sizeof(tcp_stock_limit) - 1 &&
        memcmp(name, tcp_stock_limit, sizeof(tcp_stock_limit) - 1) == 0) {
        l = strtol(value, &endptr, 10);
        if (*endptr != 0 || l < 0)
            arg_error(argv0, "Invalid value for tcp_stock_limit");

        config->tcp_stock_limit = l;
    } else
        arg_error(argv0, "Unknown variable: %.*s", (int)name_length, name);
}

static void
HandleSet(LbCmdLine *config, const char *argv0, const char *p)
{
    const char *eq;

    eq = strchr(p, '=');
    if (eq == NULL)
        arg_error(argv0, "No '=' found in --set argument");

    if (eq == p)
        arg_error(argv0, "No name found in --set argument");

    HandleSet(config, argv0, p, eq - p, eq + 1);
}

/** read configuration options from the command line */
void
ParseCommandLine(LbCmdLine *config,
                 int argc, char **argv)
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
        {"logger", 1, NULL, 'l'}, /* obsolete */
        {"access-logger", 1, NULL, 'A'},
        {"no-daemon", 0, NULL, 'D'}, /* obsolete */
        {"pidfile", 1, NULL, 'P'}, /* obsolete */
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
            PrintUsage();
            exit(0);

        case 'V':
            printf("cm4all-beng-lb v%s\n", VERSION);
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
        case 'P':
        case 'l':
            /* obsolete */
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
            daemon_user_by_name(&config->logger_user, optarg, NULL);
            break;

        case 'W':
            if (sd_booted())
                /* we don't need a watchdog process if systemd watches
                   on us */
                break;

            config->watchdog = true;
            break;

        case 'B':
            config->bulldog_path = optarg;
            break;

        case 's':
            HandleSet(config, argv[0], optarg);
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
