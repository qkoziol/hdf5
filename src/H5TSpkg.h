/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose: This file contains declarations which are visible only within
 *          the H5TS package.  Source files outside the H5TS package should
 *          include H5TSprivate.h instead.
 */
#if !(defined H5TS_FRIEND || defined H5TS_MODULE)
#error "Do not include this file outside the H5TS package!"
#endif

#ifndef H5TSpkg_H
#define H5TSpkg_H

#ifdef H5_HAVE_THREADSAFE
/* Get package's private header */
#include "H5TSprivate.h"

/* Other private headers needed by this file */

/**************************/
/* Package Private Macros */
/**************************/

#ifdef H5_HAVE_WIN_THREADS

/* Portability function aliases */
#define H5TS__wait_for_thread(thread) WaitForSingleObject(thread, INFINITE)

#else /* H5_HAVE_WIN_THREADS */

/* Portability function aliases */
#define H5TS__wait_for_thread(thread) pthread_join(thread, NULL)

#endif /* H5_HAVE_WIN_THREADS */

/****************************/
/* Package Private Typedefs */
/****************************/

/* Portability wrappers */
#ifdef H5_HAVE_WIN_THREADS

typedef INIT_ONCE H5TS_once_t;

#else

typedef pthread_once_t H5TS_once_t;

#endif /* H5_HAVE_WIN_THREADS */

/* Recursive exclusive locks */
typedef struct H5TS_ex_lock_t {
    H5TS_mutex_t  mutex;
    H5TS_cond_t   cond_var;
    H5TS_thread_t owner_thread;
    unsigned      lock_count;

/* Thread cancellability only supported with pthreads */
#ifdef H5_HAVE_PTHREAD_H
    /* Cancellation control */
    bool disable_cancel;
    int  previous_state;
#endif /* H5_HAVE_PTHREAD_H */
} H5TS_ex_lock_t;

/* Thread Barrier */
#ifdef H5_HAVE_PTHREAD_BARRIER
typedef pthread_barrier_t H5TS_barrier_t;
#else
typedef struct H5TS_barrier_t {
    H5TS_mutex_t mutex;
    H5TS_cond_t  cv;
    uint64_t     count;
    uint64_t     entered;
    uint64_t     threshold;
} H5TS_barrier_t;
#endif

/* Info for the global API lock */
typedef struct H5TS_api_info_t {
    /* API lock */
    H5TS_ex_lock_t api_lock;

    /* Count of # of attempts to acquire API lock */
    H5TS_mutex_t attempt_mutex; /* mutex for attempt_lock_count */
    unsigned     attempt_lock_count;
} H5TS_api_info_t;

#ifdef H5_HAVE_WIN_THREADS

#else

/* Enable statistics when H5TS debugging is enabled */
#ifdef H5TS_DEBUG
#define H5TS_ENABLE_REC_RW_LOCK_STATS 1
#else
#define H5TS_ENABLE_REC_RW_LOCK_STATS 0
#endif

#if H5TS_ENABLE_REC_RW_LOCK_STATS
/******************************************************************************
 *
 * Structure H5TS_rw_lock_stats_t
 *
 * Catchall structure for statistics on the recursive p-threads based
 * recursive R/W lock (see declaration of H5TS_rw_lock_t below).
 *
 * Since the mutex must be held when reading a consistent set of statistics
 * from the recursibe R/W lock, it simplifies matters to bundle them into
 * a single structure.  This structure exists for that purpose.
 *
 * If you modify this structure, be sure to make equivalent changes to
 * the reset_stats code in H5TS__rw_lock_reset_stats().
 *
 * Individual fields are:
 *
 * Read lock stats:
 *      read_locks_granted: The total number of read locks granted, including
 *              recursive lock requests.
 *
 *      read_locks_released: The total number of read locks released, including
 *              recursive lock requests.
 *
 *      real_read_locks_granted: The total number of read locks granted, less
 *              any recursive lock requests.
 *
 *      real_read_locks_released:  The total number of read locks released,
 *              less any recursive lock releases.
 *
 *      max_read_locks: The maximum number of read locks active at any point
 *              in time.
 *
 *      max_read_lock_recursion_depth: The maximum recursion depth observed
 *              for any read lock.
 *
 *      read_locks_delayed: The number of read locks not granted immediately.
 *
 *
 * Write lock stats:
 *      write_locks_granted: The total number of write locks granted,
 *              including recursive lock requests.
 *
 *      write_locks_released: The total number of write locks released,
 *              including recursive lock requests.
 *
 *      real_write_locks_granted: The total number of write locks granted,
 *              less any recursive lock requests.
 *
 *      real_write_locks_released: The total number of write locks released,
 *              less any recursive lock requests.
 *
 *      max_write_locks: The maximum number of write locks active at any point
 *              in time.  Must be either zero or one.
 *
 *      max_write_lock_recursion_depth: The maximum recursion depth observed
 *              for any write lock.
 *
 *      write_locks_delayed: The number of write locks not granted immediately.
 *
 *      max_write_locks_pending: The maximum number of pending write locks at
 *              any point in time.
 *
 ******************************************************************************/

typedef struct H5TS_rw_lock_stats_t {
    int64_t read_locks_granted;
    int64_t read_locks_released;
    int64_t real_read_locks_granted;
    int64_t real_read_locks_released;
    int64_t max_read_locks;
    int64_t max_read_lock_recursion_depth;
    int64_t read_locks_delayed;
    int64_t write_locks_granted;
    int64_t write_locks_released;
    int64_t real_write_locks_granted;
    int64_t real_write_locks_released;
    int64_t max_write_locks;
    int64_t max_write_lock_recursion_depth;
    int64_t write_locks_delayed;
    int64_t max_write_locks_pending;
} H5TS_rw_lock_stats_t;
#endif

/******************************************************************************
 *
 * Structure H5TS_rw_lock_t
 *
 * A readers / writer (R/W) lock is a lock that allows either an arbitrary
 * number of readers, or a single writer into a critical region.  A recursive
 * lock is one that allows a thread that already holds a lock (read or
 * write) to successfully request the lock again, only dropping the lock
 * when the number of unlock calls equals the number of lock calls.
 *
 * This structure holds the fields needed to implement a recursive R/W lock
 * that allows recursive write locks, and for the associated statistics
 * collection fields.
 *
 * Note that we can't use the pthreads or Win32 R/W locks: they permit
 * recursive read locks, but disallow recursive write locks.
 *
 * This recursive R/W lock implementation is an extension of the R/W lock
 * implementation given in "UNIX network programming" Volume 2, Chapter 8
 * by w. Richard Stevens, 2nd edition.
 *
 * Individual fields are:
 *
 * mutex:       Mutex used to maintain mutual exclusion on the fields of
 *              of this structure.
 *
 * lock_type:   Whether the lock is unused, a reader, or a writer.
 *
 * writers_cv:  Condition variable used for waiting writers.
 *
 * write_thread: The thread that owns a write lock, which is recursive
 *              for that thread.
 *
 * rec_write_lock_count: The # of recursive write locks outstanding
 *              for the thread that owns the write lock.
 *
 * waiting_writers_count: The count of waiting writers.
 *
 * readers_cv:  Condition variable used for waiting readers.
 *
 * active_reader_threads: The # of threads holding a read lock.
 *
 * rec_read_lock_count_key: Instance of thread-local key used to maintain
 *              a thread-specific recursive lock count for each thread
 *              holding a read lock.
 *
 * stats:       Instance of H5TS_rw_lock_stats_t used to track
 *              statistics on the recursive R/W lock.
 *
 ******************************************************************************/

typedef enum {
    UNUSED = 0, /* Lock is currently unused */
    WRITE,      /* Lock is a recursive write lock */
    READ        /* Lock is a recursive read lock */
} H5TS_rw_lock_type_t;

typedef struct H5TS_rw_lock_t {
    /* General fields */
    H5TS_mutex_t        mutex;
    H5TS_rw_lock_type_t lock_type;

    /* Writer fields */
    H5TS_cond_t   writers_cv;
    H5TS_thread_t write_thread;
    int32_t       rec_write_lock_count;
    int32_t       waiting_writers_count;

    /* Reader fields */
    bool        is_key_registered;
    H5TS_cond_t readers_cv;
    int32_t     active_reader_threads;
    H5TS_key_t  rec_read_lock_count_key;

#if H5TS_ENABLE_REC_RW_LOCK_STATS
    /* Stats */
    H5TS_rw_lock_stats_t stats;
#endif
} H5TS_rw_lock_t;

#endif /* H5_HAVE_WIN_THREADS */

/*****************************/
/* Package Private Variables */
/*****************************/

/* API threadsafety info */
extern H5TS_api_info_t H5TS_api_info_p;

/* Per-thread info */
extern H5TS_key_t H5TS_thrd_info_key_g;

/******************************/
/* Package Private Prototypes */
/******************************/
herr_t H5TS__mutex_acquire(unsigned lock_count, bool *acquired);
herr_t H5TS__mutex_release(unsigned *lock_count);
herr_t H5TS__tinfo_init(void);
void   H5TS__tinfo_destroy(void *tinfo_node);
herr_t H5TS__tinfo_term(void);

#ifdef H5_HAVE_WIN_THREADS

/* Functions called from DllMain */
H5_DLL BOOL CALLBACK H5TS__win32_process_enter(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *lpContex);

#else

H5_DLL void H5TS__pthread_first_thread_init(void);

#endif /* H5_HAVE_WIN_THREADS */

/* Recursive R/W lock related function declarations */
H5_DLL herr_t H5TS__rw_lock_init(H5TS_rw_lock_t *rw_lock);
H5_DLL herr_t H5TS__rw_rdlock(H5TS_rw_lock_t *rw_lock);
H5_DLL herr_t H5TS__rw_wrlock(H5TS_rw_lock_t *rw_lock);
H5_DLL herr_t H5TS__rw_unlock(H5TS_rw_lock_t *rw_lock);
H5_DLL herr_t H5TS__rw_lock_destroy(H5TS_rw_lock_t *rw_lock);

/* Recursive exclusive lock related function declarations */
H5_DLL herr_t H5TS__ex_lock_init(H5TS_ex_lock_t *lock, bool disable_cancel);
H5_DLL herr_t H5TS__ex_lock(H5TS_ex_lock_t *lock);
H5_DLL herr_t H5TS__ex_acquire(H5TS_ex_lock_t *lock, unsigned lock_count, bool *acquired);
H5_DLL herr_t H5TS__ex_release(H5TS_ex_lock_t *lock, unsigned int *lock_count);
H5_DLL herr_t H5TS__ex_unlock(H5TS_ex_lock_t *lock);
H5_DLL herr_t H5TS__ex_lock_destroy(H5TS_ex_lock_t *lock);

/* Barrier related function declarations */
H5_DLL herr_t H5TS__barrier_init(H5TS_barrier_t *barrier, uint64_t count);
H5_DLL herr_t H5TS__barrier_wait(H5TS_barrier_t *barrier);
H5_DLL herr_t H5TS__barrier_destroy(H5TS_barrier_t *barrier);

#ifdef H5TS_TESTING
#if H5TS_ENABLE_REC_RW_LOCK_STATS
H5_DLL herr_t H5TS__rw_lock_get_stats(H5TS_rw_lock_t *rw_lock, H5TS_rw_lock_stats_t *stats);
H5_DLL herr_t H5TS__rw_lock_reset_stats(H5TS_rw_lock_t *rw_lock);
H5_DLL herr_t H5TS__rw_lock_print_stats(const char *header_str, H5TS_rw_lock_stats_t *stats);
#endif

/* Testing routines */
H5_DLL H5TS_thread_t H5TS__create_thread(void *(*func)(void *), void *udata);

#endif /* H5TS_TESTING */

#endif /* H5_HAVE_THREADSAFE */

#endif /* H5TSpkg_H */