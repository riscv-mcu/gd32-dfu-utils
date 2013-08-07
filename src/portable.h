
#ifndef PORTABLE_H
#define PORTABLE_H

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_FTRUNCATE
# include <unistd.h>
#endif

#ifdef HAVE_NANOSLEEP
# include <time.h>
# define milli_sleep(msec) do {\
    struct timespec nanosleepDelay = { msec / 1000, (msec % 1000) * 1000000 };\
    nanosleep(&nanosleepDelay, NULL); } while (0)
#elif defined HAVE_WINDOWS_H
# define milli_sleep(msec) Sleep(msec)
#else
# error "Can't get no sleep! Please report"
#endif /* HAVE_NANOSLEEP */

#ifdef HAVE_ERR
# include <err.h>
#else
# include <errno.h>
# include <string.h>
# define warnx(...) do {\
    fprintf(stderr, __VA_ARGS__);\
    fprintf(stderr, "\n"); } while (0)
# define errx(eval, ...) do {\
    warnx(__VA_ARGS__);\
    exit(eval); } while (0)
# define warn(...) do {\
    fprintf(stderr, "%s: ", strerror(errno));\
    warnx(__VA_ARGS__); } while (0)
# define err(eval, ...) do {\
    warn(__VA_ARGS__);\
    exit(eval); } while (0)
#endif /* HAVE_ERR */

#ifdef HAVE_SYSEXITS_H
# include <sysexits.h>
#else
# define EX_OK		0	/* successful termination */
# define EX_USAGE	64	/* command line usage error */
# define EX_SOFTWARE	70	/* internal software error */
# define EX_IOERR	74	/* input/output error */
#endif /* HAVE_SYSEXITS_H */

#ifndef O_BINARY
# define O_BINARY   0
#endif

#endif /* PORTABLE_H */
