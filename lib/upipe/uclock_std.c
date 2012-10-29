/*
 * Copyright (C) 2012 OpenHeadend S.A.R.L.
 *
 * Authors: Christophe Massiot
 *          Benjamin Cohen
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject
 * to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** @file
 * @short standard common toolbox provided by the application to modules
 */

#include <upipe/ubase.h>
#include <upipe/urefcount.h>
#include <upipe/uclock.h>
#include <upipe/uclock_std.h>

#include <stdlib.h>
#include <time.h>

#ifdef __MACH__
#include <string.h>
#include <mach/clock.h>
#include <mach/mach.h>
#endif

/** super-set of the uclock structure with additional local members */
struct uclock_std {
    /** flags at the creation of this clock */
    enum uclock_std_flags flags;
#ifdef __MACH__
    /** mach cclock structure */
    clock_serv_t cclock;
#endif

    /** refcount management structure */
    urefcount refcount;
    /** structure exported to modules */
    struct uclock uclock;
};

/** @internal @This returns the high-level uclock structure.
 *
 * @param uclock_std pointer to the struct uclock_std structure
 * @return pointer to the struct uclock structure
 */
static inline struct uclock *uclock_std_to_uclock(struct uclock_std *uclock_std)
{
    return &uclock_std->uclock;
}

/** @internal @This returns the private struct uclock_std structure.
 *
 * @param uclock description structure of the struct uclock
 * @return pointer to the struct uclock_std structure
 */
static inline struct uclock_std *uclock_std_from_uclock(struct uclock *uclock)
{
    return container_of(uclock, struct uclock_std, uclock);
}

/** @This returns the current system time.
 *
 * @param uclock utility structure passed to the module
 * @return current system time in 27 MHz ticks
 */
static uint64_t uclock_std_now(struct uclock *uclock)
{
    struct uclock_std *std = uclock_std_from_uclock(uclock);

#ifdef __MACH__ // OS X does not have clock_gettime, use clock_get_time
    mach_timespec_t ts;
    clock_get_time((std->cclock), &ts);
#else
    struct timespec ts;

    if (unlikely(clock_gettime(unlikely(std->flags & UCLOCK_FLAG_REALTIME) ?
                               CLOCK_REALTIME : CLOCK_MONOTONIC, &ts) == -1))
        /* this should not happen as we have checked the clock existed
         * in alloc */
        return 0;
#endif

    uint64_t now = ts.tv_sec * UCLOCK_FREQ +
                   ts.tv_nsec * UCLOCK_FREQ / UINT64_C(1000000000);
    return now;
}

/** @This increments the reference count of a uclock.
 *
 * @param uclock pointer to uclock
 */
static void uclock_std_use(struct uclock *uclock)
{
    struct uclock_std *uclock_std = uclock_std_from_uclock(uclock);
    urefcount_use(&uclock_std->refcount);
}

/** @This decrements the reference count of a uclock or frees it.
 *
 * @param uclock pointer to uclock
 */
static void uclock_std_release(struct uclock *uclock)
{
    struct uclock_std *uclock_std = uclock_std_from_uclock(uclock);
    if (unlikely(urefcount_release(&uclock_std->refcount))) {
#ifdef __MACH__
        mach_port_deallocate(mach_task_self(), uclock_std->cclock);
#endif
        urefcount_clean(&uclock_std->refcount);
        free(uclock_std);
    }
}

/** @This allocates a new uclock structure.
 *
 * @param flags flags for the creation of a uclock structure
 * @return pointer to uclock, or NULL in case of error
 */
struct uclock *uclock_std_alloc(enum uclock_std_flags flags)
{
#ifdef __MACH__
    clock_serv_t cclock;
    mach_timespec_t ts;
    if (unlikely(host_get_clock_service(mach_host_self(), unlikely(flags & UCLOCK_FLAG_REALTIME) ? CALENDAR_CLOCK : REALTIME_CLOCK, &cclock) < 0)) {
        return NULL;
    }
    if(unlikely(clock_get_time(cclock, &ts) < 0)) {
        mach_port_deallocate(mach_task_self(), cclock);
        return NULL;
    }
#else
    struct timespec ts;
    if (unlikely(clock_gettime(unlikely(flags & UCLOCK_FLAG_REALTIME) ?
                               CLOCK_REALTIME : CLOCK_MONOTONIC, &ts) == -1))
        return NULL;
#endif

    struct uclock_std *uclock_std = malloc(sizeof(struct uclock_std));
    if (unlikely(uclock_std == NULL)) return NULL;
    uclock_std->flags = flags;
    urefcount_init(&uclock_std->refcount);
    uclock_std->uclock.uclock_now = uclock_std_now;
    uclock_std->uclock.uclock_use = uclock_std_use;
    uclock_std->uclock.uclock_release = uclock_std_release;
#ifdef __MACH__
    memcpy(&uclock_std->cclock, &cclock, sizeof(cclock));
#endif
    return uclock_std_to_uclock(uclock_std);
}
