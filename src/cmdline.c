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
#include <pwd.h>
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
         " --name NAME\n"
#endif
         " -N NAME        set the node name\n"
#ifdef __GLIBC__
         " --concurrency NUM\n"
#endif
         " -c NUM         set the maximum number of concurrent operators (default: 2)\n"
#ifdef __GLIBC__
         " --database CONNINFO\n"
#endif
         " -d CONNINFO    set the PostgreSQL connect string\n"
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
         " --document-root DIR\n"
#endif
         " -r DIR         set the document root\n"
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
parse_username(const char *argv0, const char *name, uid_t *uid_r, gid_t *gid_r)
{
    const struct passwd *pwd = getpwnam(name);

    if (pwd == NULL)
        arg_error(argv0, "no such user: %s", name);

    if (pwd->pw_uid == 0 || pwd->pw_gid == 0)
        arg_error(argv0, "refuse to change to a superuser account");

    *uid_r = pwd->pw_uid;
    *gid_r = pwd->pw_gid;
}

/** read configuration options from the command line */
void
parse_cmdline(struct config *config, int argc, char **argv)
{
    int ret;
#ifdef __GLIBC__
    static const struct option long_options[] = {
        {"help", 0, 0, 'h'},
        {"version", 0, 0, 'V'},
        {"verbose", 0, 0, 'v'},
        {"quiet", 0, 0, 'q'},
        {"logger", 1, 0, 'l'},
        {"pidfile", 1, 0, 'P'},
        {"user", 1, 0, 'u'},
        {"document-root", 1, 0, 'r'},
        {0,0,0,0}
    };
#endif

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv, "hVvqDP:l:u:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv, "hVvqDP:l:u:");
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
            daemon_verbose = 0;
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
            parse_username(argv[0], optarg, &config->uid, &config->gid);
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

    if (debug_mode) {
        if (config->uid != 0)
            arg_error(argv[0], "cannot specify a user in debug mode");
    } else {
        /* non-root only for debugging */
        if (config->uid == 0)
            arg_error(argv[0], "no user name specified (-u)");
    }
}
