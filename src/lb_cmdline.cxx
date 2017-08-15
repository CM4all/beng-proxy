/*
 * Parse command line options.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_cmdline.hxx"
#include "lb/Config.hxx"
#include "stopwatch.hxx"
#include "io/Logger.hxx"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <getopt.h>
#include <string.h>
#include <unistd.h>

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
         " --user name\n"
#endif
         " -u name        switch to another user id\n"
#ifdef __GLIBC__
         " --logger-user name\n"
#endif
         " -U name        execute the access logger program with this user id\n"
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
HandleSet(LbCmdLine &cmdline, const char *argv0,
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

        cmdline.tcp_stock_limit = l;
    } else
        arg_error(argv0, "Unknown variable: %.*s", (int)name_length, name);
}

static void
HandleSet(LbCmdLine &cmdline, const char *argv0, const char *p)
{
    const char *eq;

    eq = strchr(p, '=');
    if (eq == NULL)
        arg_error(argv0, "No '=' found in --set argument");

    if (eq == p)
        arg_error(argv0, "No name found in --set argument");

    HandleSet(cmdline, argv0, p, eq - p, eq + 1);
}

/** read configuration options from the command line */
void
ParseCommandLine(LbCmdLine &cmdline, LbConfig &config,
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
        {"access-logger", 1, NULL, 'A'},
        {"user", 1, NULL, 'u'},
        {"logger-user", 1, NULL, 'U'},
        {"bulldog-path", 1, NULL, 'B'},
        {"set", 1, NULL, 's'},
        {NULL,0,NULL,0}
    };
#endif
    const char *user_name = NULL;
    unsigned verbose = 1;

    while (1) {
#ifdef __GLIBC__
        int option_index = 0;

        ret = getopt_long(argc, argv, "hVvqf:CA:u:U:B:s:",
                          long_options, &option_index);
#else
        ret = getopt(argc, argv, "hVvqf:CA:u:U:B:s:");
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
            ++verbose;
            break;

        case 'q':
            verbose = 0;
            break;

        case 'f':
            cmdline.config_path = optarg;
            break;

        case 'C':
            cmdline.check = true;
            break;

        case 'A':
            config.access_log.SetLegacy(optarg);
            break;

        case 'u':
            user_name = optarg;
            break;

        case 'U':
            cmdline.logger_user.Lookup(optarg);
            break;

        case 'B':
            cmdline.bulldog_path = optarg;
            break;

        case 's':
            HandleSet(cmdline, argv[0], optarg);
            break;

        case '?':
            arg_error(argv[0], NULL);

        default:
            exit(1);
        }
    }

    SetLogLevel(verbose);

    /* check non-option arguments */

    if (optind < argc)
        arg_error(argv[0], "unrecognized argument: %s", argv[optind]);

    /* check completeness */

    if (user_name != NULL) {
        cmdline.user.Lookup(user_name);
        if (!cmdline.user.IsComplete())
            arg_error(argv[0], "refusing to run as root");
    } else if (geteuid() == 0)
        arg_error(argv[0], "no user name specified (-u)");
}
