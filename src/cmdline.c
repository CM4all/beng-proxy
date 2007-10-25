/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "config.h"

#include <daemon/daemonize.h>
#include <daemon/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

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

/** read configuration options from the command line */
void
parse_cmdline(struct config *config, int argc, char **argv)
{
    int ret;
    char *endptr;
#ifdef __GLIBC__
    static const struct option long_options[] = {
        {"help", 0, 0, 'h'},
        {"version", 0, 0, 'V'},
        {"verbose", 0, 0, 'v'},
        {"quiet", 0, 0, 'q'},
        {"logger", 1, 0, 'l'},
        {"pidfile", 1, 0, 'P'},
        {"user", 1, 0, 'u'},
        {"logger-user", 1, 0, 'U'},
        {"port", 1, 0, 'p'},
        {"workers", 1, 0, 'w'},
        {"document-root", 1, 0, 'r'},
        {"translation-socket", 1, 0, 't'},
        {0,0,0,0}
    };
#endif

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv, "hVvqDP:l:u:U:p:w:r:t:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv, "hVvqDP:l:u:U:p:w:r:t:");
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
            config->port = (unsigned)strtoul(optarg, &endptr, 10);
            if (*endptr != 0)
                arg_error(argv[0], "invalid number after --port");
            if (config->port == 0 || config->port > 0xffff)
                arg_error(argv[0], "invalid port after --port");
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
