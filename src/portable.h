
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

#endif /* PORTABLE_H */
