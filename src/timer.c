#include <errno.h>
#include <inttypes.h>
#include "timer.h"

#define GIGA 1000000000

#if defined(__unix__) && !defined(__CYGWIN__)

# ifdef CLOCK_MONOTONIC_RAW
#  define CLOCK CLOCK_MONOTONIC_RAW
# else
#  define CLOCK CLOCK_MONOTONIC
# endif

extern __attribute__ ((weak)) int clock_gettime(clockid_t, struct timespec *);

#elif defined(__APPLE__)
# include <mach/clock.h>
# include <mach/mach.h>
#elif defined(_WIN32) || defined(__CYGWIN__)
# define VC_EXTRALEAN
# define WIN32_LEAN_AND_MEAN
# include <Windows.h>
#endif

bool can_measure_time(void) {
#if defined(__unix__) && !defined(__CYGWIN__)
    return clock_gettime != NULL;
#else
    return true;
#endif
}

int gettime_compat(struct timespec_compat *ts) {
#if defined(__APPLE__)
    clock_serv_t cclock;
    mach_timespec_t mts;

    host_get_clock_service(mach_host_self(), SYSTEM_CLOCK, &cclock);
    int res = clock_get_time(cclock, &mts);
    mach_port_deallocate(mach_task_self(), cclock);

    *ts = (struct timespec_compat) { mts.tv_sec, mts.tv_nsec };
    return res > 0 ? -1 : 0;
#elif defined(_WIN32) || defined(__CYGWIN__)
    LARGE_INTEGER freq, count;
    if (!QueryPerformanceFrequency(&freq)
        || !QueryPerformanceCounter(&count))
        return -1;

    int64_t sec  = count.QuadPart / freq.QuadPart;
    int64_t nano = (int64_t) ((double) count.QuadPart * GIGA / (double) freq.QuadPart) % GIGA;

    *ts = (struct timespec_compat) { sec, nano };
    return 0;
#elif defined(__unix__)
    if (!can_measure_time()) {
        errno = ENOTSUP;
        return -1;
    }

    struct timespec ts_;
    int res = clock_gettime(CLOCK, &ts_);

    *ts = (struct timespec_compat) { ts_.tv_sec, ts_.tv_nsec };
    return res;
#else
    return -1;
#endif
}

int timer_start(struct timespec_compat *state) {
    return gettime_compat(state) == -1 ? 0 : 1;
}

int timer_end(double *time, struct timespec_compat *state) {
    struct timespec_compat last;
    if (gettime_compat(&last) == -1)
        return 0;

    *time = (last.tv_sec - state->tv_sec) + (last.tv_nsec - state->tv_nsec) / (double) GIGA;
    return 1;
}
