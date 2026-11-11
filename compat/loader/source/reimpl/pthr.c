/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022      GrapheneCt
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/pthr.h"

#include <stdlib.h>
#include <string.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>
#include <stdatomic.h>

#include "utils/utils.h"
#include "utils/logger.h"
#include <stdint.h>

#define BIONIC_PTHREAD_COND_INITIALIZER              0
#define BIONIC_PTHREAD_MUTEX_INITIALIZER             0
#define BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER   0x4000
#define BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER  0x8000

enum {
    BIONIC_PTHREAD_MUTEX_NORMAL = 0,
    BIONIC_PTHREAD_MUTEX_RECURSIVE = 1,
    BIONIC_PTHREAD_MUTEX_ERRORCHECK = 2,

    BIONIC_PTHREAD_MUTEX_ERRORCHECK_NP = BIONIC_PTHREAD_MUTEX_ERRORCHECK,
    BIONIC_PTHREAD_MUTEX_RECURSIVE_NP  = BIONIC_PTHREAD_MUTEX_RECURSIVE,

    BIONIC_PTHREAD_MUTEX_DEFAULT = BIONIC_PTHREAD_MUTEX_NORMAL
};

#define PTHR_INLINE static inline __attribute__((always_inline))

/*
 * Bionic mutexes/conds are 4 bytes; we store a pointer to a real newlib object in
 * that slot. A statically-initialised bionic object holds one of the small
 * initializer values above (0, 0x4000, 0x8000), so any value below 0x10000 means
 * "not created yet". Creation is installed with an atomic compare-and-swap, so two
 * threads racing on the first lock of a static mutex always end up sharing the
 * same real mutex. (The previous version used a check-then-init on a 1024-entry
 * table: racing threads could each create their own mutex, and once the table
 * filled up every lock call created a brand-new mutex - both lead to lost locking,
 * leaks and intermittent crashes. It also scanned the table on every lock.)
 */
#define PTHR_IS_SMALL_VALUE(v) ((uintptr_t) (v) < 0x10000)

/*
 * Registry of the real mutex/cond objects this loader created. A bionic slot only
 * counts as initialised if it holds one of these pointers; anything else (zero, an
 * initializer constant, or leftover garbage in uninitialised memory) is treated as
 * "not created yet". Lock-free open addressing: inserts CAS an empty slot, removals
 * leave a tombstone, lookups never block.
 */
#define PTHR_REG_SIZE 32768u   /* power of two */
#define PTHR_REG_EMPTY 0u
#define PTHR_REG_TOMB 1u
static uintptr_t pthr_registry[PTHR_REG_SIZE];

static inline uint32_t pthr_hash(uintptr_t p) {
    return (uint32_t) ((p >> 3) * 2654435761u) & (PTHR_REG_SIZE - 1);
}

static void pthr_reg_add(uintptr_t p) {
    uint32_t i = pthr_hash(p);
    for (uint32_t n = 0; n < PTHR_REG_SIZE; n++, i = (i + 1) & (PTHR_REG_SIZE - 1)) {
        uintptr_t cur = __atomic_load_n(&pthr_registry[i], __ATOMIC_ACQUIRE);
        if (cur == p) return;
        if (cur == PTHR_REG_EMPTY || cur == PTHR_REG_TOMB) {
            if (__atomic_compare_exchange_n(&pthr_registry[i], &cur, p, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return;
            if (cur == p) return;
        }
    }
    l_error("pthread registry full");
}

static int pthr_reg_has(uintptr_t p) {
    if (PTHR_IS_SMALL_VALUE(p)) return 0;
    uint32_t i = pthr_hash(p);
    for (uint32_t n = 0; n < PTHR_REG_SIZE; n++, i = (i + 1) & (PTHR_REG_SIZE - 1)) {
        uintptr_t cur = __atomic_load_n(&pthr_registry[i], __ATOMIC_ACQUIRE);
        if (cur == p) return 1;
        if (cur == PTHR_REG_EMPTY) return 0;
    }
    return 0;
}

static void pthr_reg_remove(uintptr_t p) {
    uint32_t i = pthr_hash(p);
    for (uint32_t n = 0; n < PTHR_REG_SIZE; n++, i = (i + 1) & (PTHR_REG_SIZE - 1)) {
        uintptr_t cur = __atomic_load_n(&pthr_registry[i], __ATOMIC_ACQUIRE);
        if (cur == p) { __atomic_store_n(&pthr_registry[i], PTHR_REG_TOMB, __ATOMIC_RELEASE); return; }
        if (cur == PTHR_REG_EMPTY) return;
    }
}

#define PTHR_IS_REAL_PTR(v) pthr_reg_has((uintptr_t) (v))

static pthread_mutex_t *_mutex_create(int kind) {
    pthread_mutex_t *m = malloc(sizeof(pthread_mutex_t));
    if (!m) return NULL;

    pthread_mutexattr_t mutattr;
    pthread_mutexattr_init(&mutattr);
    pthread_mutexattr_settype(&mutattr, kind);
    int ret = pthread_mutex_init(m, &mutattr);
    pthread_mutexattr_destroy(&mutattr);

    if (ret != 0) {
        free(m);
        return NULL;
    }
    pthr_reg_add((uintptr_t) m);
    return m;
}

// null check for `attr` must be performed before this
PTHR_INLINE int _attr_t_static_init(pthread_attr_t_bionic * attr) {
    if (attr->magic != 0x42424242) {
        attr->magic = 0x42424242;
        attr->real_ptr = malloc(sizeof(pthread_attr_t));
        return pthread_attr_init(attr->real_ptr);
    }
    return 0;
}

// Lazily creates the real mutex for a statically-initialised bionic mutex. Thread-safe.
static pthread_mutex_t *_mutex_get(pthread_mutex_t_bionic * mutex) {
    uintptr_t cur = __atomic_load_n((uintptr_t *) &mutex->real_ptr, __ATOMIC_ACQUIRE);
    if (PTHR_IS_REAL_PTR(cur))
        return (pthread_mutex_t *) cur;

    int kind = PTHREAD_MUTEX_NORMAL;   // also used for uninitialised garbage
    if (cur == BIONIC_PTHREAD_RECURSIVE_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_RECURSIVE;
    else if (cur == BIONIC_PTHREAD_ERRORCHECK_MUTEX_INITIALIZER) kind = PTHREAD_MUTEX_ERRORCHECK;

    pthread_mutex_t *m = _mutex_create(kind);
    if (!m) {
        l_error("mutex creation for %p has failed", mutex);
        return NULL;
    }

    uintptr_t expected = cur;
    if (__atomic_compare_exchange_n((uintptr_t *) &mutex->real_ptr, &expected, (uintptr_t) m,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return m;
    }

    // Another thread won the race: use its mutex, discard ours.
    pthr_reg_remove((uintptr_t) m);
    pthread_mutex_destroy(m);
    free(m);
    return PTHR_IS_REAL_PTR(expected) ? (pthread_mutex_t *) expected : _mutex_get(mutex);
}

// Lazily creates the real cond for a statically-initialised bionic cond. Thread-safe.
static pthread_cond_t *_cond_get(pthread_cond_t_bionic * cond) {
    uintptr_t cur = __atomic_load_n((uintptr_t *) &cond->real_ptr, __ATOMIC_ACQUIRE);
    if (PTHR_IS_REAL_PTR(cur))
        return (pthread_cond_t *) cur;

    pthread_cond_t *c = malloc(sizeof(pthread_cond_t));
    if (!c || pthread_cond_init(c, NULL) != 0) {
        free(c);
        l_error("cond creation for %p has failed", cond);
        return NULL;
    }
    pthr_reg_add((uintptr_t) c);

    uintptr_t expected = cur;
    if (__atomic_compare_exchange_n((uintptr_t *) &cond->real_ptr, &expected, (uintptr_t) c,
                                    0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        return c;
    }

    pthr_reg_remove((uintptr_t) c);
    pthread_cond_destroy(c);
    free(c);
    return PTHR_IS_REAL_PTR(expected) ? (pthread_cond_t *) expected : _cond_get(cond);
}

int pthread_create_soloader(pthread_t *thread, const pthread_attr_t_bionic *attr, void *(*start)(void *), void *param) {
    int ret;

    if (!attr) {
        pthread_attr_t a;
        pthread_attr_init(&a);
        pthread_attr_setstacksize(&a, 512 * 1024);
        ret = pthread_create(thread, &a, start, param);
        pthread_attr_destroy(&a);
    } else{
        _attr_t_static_init((pthread_attr_t_bionic *) attr);
        pthread_attr_setstacksize(attr->real_ptr, 512 * 1024);
        ret = pthread_create(thread, attr->real_ptr, start, param);
    }

    return ret;
}

int pthread_mutexattr_init_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_init(attr);
}

int pthread_mutexattr_settype_soloader(pthread_mutexattr_t *attr, int type)
{
    return pthread_mutexattr_settype(attr, type);
}

int pthread_mutexattr_destroy_soloader(pthread_mutexattr_t *attr)
{
    return pthread_mutexattr_destroy(attr);
}

int pthread_kill_soloader(pthread_t thread, int sig)
{
    return pthread_kill(thread, sig);
}

int pthread_mutex_init_soloader(pthread_mutex_t_bionic *uid, const pthread_mutexattr_t *attr)
{
    if (!uid) return EINVAL;

    // Explicit init: the slot may hold stack/heap garbage, so never read it.
    int kind = PTHREAD_MUTEX_NORMAL;
    if (attr) pthread_mutexattr_gettype((pthread_mutexattr_t *) attr, &kind);

    pthread_mutex_t *m = _mutex_create(kind);
    if (!m) {
        l_error("mutex initialization for %p has failed", uid);
        return ENOMEM;
    }
    __atomic_store_n((uintptr_t *) &uid->real_ptr, (uintptr_t) m, __ATOMIC_RELEASE);
    return 0;
}

int pthread_mutex_destroy_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return 0;
    uintptr_t cur = __atomic_exchange_n((uintptr_t *) &mutex->real_ptr, 0, __ATOMIC_ACQ_REL);
    if (!PTHR_IS_REAL_PTR(cur)) return 0;
    pthr_reg_remove(cur);
    int ret = pthread_mutex_destroy((pthread_mutex_t *) cur);
    free((void *) cur);
    return ret;
}

int pthread_mutex_lock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    pthread_mutex_t *m = _mutex_get(mutex);
    if (!m) return ENOMEM;
    return pthread_mutex_lock(m);
}

int pthread_mutex_trylock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    pthread_mutex_t *m = _mutex_get(mutex);
    if (!m) return ENOMEM;
    return pthread_mutex_trylock(m);
}

int pthread_mutex_unlock_soloader(pthread_mutex_t_bionic *mutex)
{
    if (!mutex) return EINVAL;
    uintptr_t cur = __atomic_load_n((uintptr_t *) &mutex->real_ptr, __ATOMIC_ACQUIRE);
    if (!PTHR_IS_REAL_PTR(cur)) return EINVAL;
    return pthread_mutex_unlock((pthread_mutex_t *) cur);
}

int pthread_join_soloader(pthread_t thread, void **value_ptr)
{
    return pthread_join(thread, value_ptr);
}

int pthread_condattr_init_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_init(attr);
}

int pthread_condattr_destroy_soloader(pthread_condattr_t *attr)
{
    if (!attr) return EINVAL;
    return pthread_condattr_destroy(attr);
}

int pthread_cond_init_soloader(pthread_cond_t_bionic *cond,
                               const pthread_condattr_t *attr)
{
    if (!cond) return EINVAL;

    pthread_cond_t *c = malloc(sizeof(pthread_cond_t));
    if (!c) return ENOMEM;
    int ret = pthread_cond_init(c, attr);
    if (ret != 0) {
        free(c);
        l_error("cond initialization for %p has failed", cond);
        return ret;
    }
    pthr_reg_add((uintptr_t) c);
    __atomic_store_n((uintptr_t *) &cond->real_ptr, (uintptr_t) c, __ATOMIC_RELEASE);
    return 0;
}

int pthread_cond_destroy_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return 0;
    uintptr_t cur = __atomic_exchange_n((uintptr_t *) &cond->real_ptr, 0, __ATOMIC_ACQ_REL);
    if (!PTHR_IS_REAL_PTR(cur)) return 0;
    pthr_reg_remove(cur);
    int ret = pthread_cond_destroy((pthread_cond_t *) cur);
    free((void *) cur);
    return ret;
}

int pthread_cond_signal_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    pthread_cond_t *c = _cond_get(cond);
    if (!c) return ENOMEM;
    return pthread_cond_signal(c);
}

int pthread_cond_timedwait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex, struct timespec *abstime)
{
    if (!cond || !mutex) return EINVAL;

    pthread_cond_t *c = _cond_get(cond);
    pthread_mutex_t *m = _mutex_get(mutex);
    if (!c || !m) return ENOMEM;
    return pthread_cond_timedwait(c, m, abstime);
}


int pthread_cond_wait_soloader(pthread_cond_t_bionic *cond, pthread_mutex_t_bionic *mutex)
{
    if (!cond || !mutex) return EINVAL;

    pthread_cond_t *c = _cond_get(cond);
    pthread_mutex_t *m = _mutex_get(mutex);
    if (!c || !m) return ENOMEM;
    return pthread_cond_wait(c, m);
}

int pthread_cond_broadcast_soloader(pthread_cond_t_bionic *cond)
{
    if (!cond) return EINVAL;

    pthread_cond_t *c = _cond_get(cond);
    if (!c) return ENOMEM;
    return pthread_cond_broadcast(c);
}

int pthread_attr_init_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return EINVAL;

    return _attr_t_static_init(attr);
}

int pthread_attr_destroy_soloader(pthread_attr_t_bionic *attr)
{
    if (!attr) return 0;
    if (attr->magic != 0x42424242) return 0;

    int ret = pthread_attr_destroy(attr->real_ptr);
    free(attr->real_ptr);
    attr->magic = 0x0;

    return ret;
}

int pthread_attr_setdetachstate_soloader(pthread_attr_t_bionic *attr, int state)
{
    if (!attr) return -1;
    _attr_t_static_init(attr);
    return pthread_attr_setdetachstate(attr->real_ptr, state);
}

int pthread_attr_setstacksize_soloader(pthread_attr_t_bionic *attr, size_t stacksize) {
    if (!attr) return -1;
    _attr_t_static_init(attr);
    return pthread_attr_setstacksize(attr->real_ptr, stacksize);
}

int pthread_setschedparam_soloader(pthread_t thread, int policy,
                                   const struct sched_param *param)
{
   return pthread_setschedparam(thread, policy, param);
}

int pthread_getschedparam_soloader(pthread_t thread, int *policy,
                                   struct sched_param *param)
{
    return pthread_getschedparam(thread, policy, param);
}

int pthread_detach_soloader(pthread_t thread)
{
    return pthread_detach(thread);
}

int pthread_equal_soloader(const pthread_t t1, const pthread_t t2)
{
    if (t1 == t2)
        return 1;
    if (!t1 || !t2)
        return 0;
    return pthread_equal(t1, t2);
}

pthread_t pthread_self_soloader()
{
    return pthread_self();
}

// 0 = not run, 1 = running, 2 = done. Late callers wait until init_routine has
// actually finished (the old version let them continue while it was still running).
int pthread_once_soloader(volatile int *once_control, void (*init_routine)(void)) {
    if (!once_control || !init_routine)
        return -1;
    if (__atomic_load_n(once_control, __ATOMIC_ACQUIRE) == 2)
        return 0;

    int expected = 0;
    if (__atomic_compare_exchange_n(once_control, &expected, 1, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        (*init_routine)();
        __atomic_store_n(once_control, 2, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(once_control, __ATOMIC_ACQUIRE) != 2)
        sceKernelDelayThread(100);
    return 0;
}

#ifndef MAX_TASK_COMM_LEN
#define MAX_TASK_COMM_LEN 16
#endif

int pthread_setname_np_soloader(pthread_t thread, const char* thread_name) {
    if (thread == 0 || thread_name == NULL) {
        return EINVAL;
    }
    size_t thread_name_len = strlen(thread_name);
    if (thread_name_len >= MAX_TASK_COMM_LEN) {
        return ERANGE;
    }

    sceClibPrintf("PTHREAD: pthread_setname_np with name %s for thread:0x%x\n", thread_name, pthread_self());

    return 0;
}

int sem_destroy_soloader(int * uid) {
    if (sceKernelDeleteSema(*uid) < 0)
        return -1;
    return 0;
}

int sem_getvalue_soloader (int * uid, int * sval) {
    SceKernelSemaInfo info;
    info.size = sizeof(SceKernelSemaInfo);

    if (sceKernelGetSemaInfo(*uid, &info) < 0) return -1;
    if (!sval) sval = malloc(sizeof(int32_t));
    *sval = info.currentCount;
    return 0;
}

int sem_init_soloader (int * uid, int pshared, unsigned int value) {
    *uid = sceKernelCreateSema("sema", 0, (int) value, 0x7fffffff, NULL);
    if (*uid < 0)
        return -1;
    return 0;
}

int sem_post_soloader (int * uid) {
    if (sceKernelSignalSema(*uid, 1) < 0)
        return -1;
    return 0;
}

int sem_timedwait_soloader (int * uid, const struct timespec * abstime) {
    if (sceKernelPollSema(*uid, 1) >= 0)
        return 0;
    if (!abstime) return -1;
    long long now = (long long) current_timestamp_ms() * 1000; // us
    long long _timeout = (long long) abstime->tv_sec * 1000 * 1000 + abstime->tv_nsec / 1000; // us
    // Previously inverted (returned "timed out" whenever the deadline was still in the future).
    if (_timeout - now <= 0) return -1;
    uint timeout_real = (uint) (_timeout - now);
    if (sceKernelWaitSema(*uid, 1, &timeout_real) < 0)
        return -1;
    return 0;
}

int sem_trywait_soloader (int * uid) {
    if (sceKernelPollSema(*uid, 1) < 0)
        return -1;
    return 0;
}

int sem_wait_soloader (int * uid) {
    if (sceKernelWaitSema(*uid, 1, NULL) < 0)
        return -1;
    return 0;
}
