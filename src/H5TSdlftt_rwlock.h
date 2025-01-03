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
 * Purpose: This file contains support for mutex locks, equivalent to the
 *        pthread 'pthread_rwlock_t' type and capabilities, but efficiently
 *        obeying the "DLFFT" locking protocol.
 *
 * Note:  Because this threadsafety framework operates outside the library,
 *        it does not use the error stack (although it does use error macros
 *        that don't push errors on a stack) and only uses the "namecheck only"
 *        FUNC_ENTER_* / FUNC_LEAVE_* macros.
 */

/****************/
/* Module Setup */
/****************/

/***********/
/* Headers */
/***********/

#ifndef H5TS__get_dlftt_DEF
#define H5TS__get_dlftt_DEF
/* Declare this routine here also, to avoid including package header */
H5_DLL herr_t H5TS__get_dlftt(unsigned *dlftt);
#endif /* H5TS__get_dlftt_DEF */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/

/*********************/
/* Package Variables */
/*********************/

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_rwlock_rdlock
 *
 * Purpose:     Acquires a shared lock on a R/W lock, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'          Acquire DLFTT R/W lock
 *               │               ----------------------
 *               ▼
 *               Λ
 *              ╱ ╲
 *             ╱   ╲
 *            ╱     ╲
 *           ╱       ╲
 *          ╱         ╲    N    ┌────────────────┐
 *         ▕  bypass?  ▏───────▶│Get DLFTT value │────────┐
 *          ╲         ╱         └────────────────┘        │
 *           ╲       ╱                                    │
 *            ╲     ╱                                     ▼
 *             ╲   ╱                                      Λ
 *              ╲ ╱                                      ╱ ╲
 *               V                                      ╱   ╲
 *            Y  │                                     ╱     ╲
 *               ▼                                    ╱       ╲
 *        ┌────────────┐     ┌───────────────┐   Y   ╱         ╲
 *        │ ++refcount │◀────│ bypass = true │◀─────▕ DLFTT > 0?▏
 *        └────────────┘     └───────────────┘       ╲         ╱
 *               │                                    ╲       ╱
 *               │                                     ╲     ╱
 *               │                                      ╲   ╱
 *               │                                       ╲ ╱
 *               │                                        V
 *               │                                     N  │
 *               │                                        ▼
 *               │                  .─.            ┌────────────┐
 *               └────────────────▶( X )◀──────────│    Lock    │
 *                                  `─'            └────────────┘
 *                                   │
 *                                   ▼
 *                              .─────────.
 *                             (    End    )
 *                              `─────────'
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_rwlock_rdlock(H5TS_dlftt_rwlock_t *lck)
{
    /* Check whether we are bypassing locking the R/W lock */
    if (lck->bypass)
        /* Increment refcount */
        lck->rc++;
    else {
        unsigned dlftt = 0;

        /* Query the DLFTT value */
        if (H5_UNLIKELY(H5TS__get_dlftt(&dlftt) < 0))
            return FAIL;

        /* Acquire the lock if locking is not disabled */
        if (0 == dlftt) {
            /* Acquire the lock */
            if (H5_UNLIKELY(H5TS_rwlock_rdlock(&lck->lck) < 0))
                return FAIL;
        } /* end if */
        else {
            /* Indicate that lock should be bypassed */
            lck->bypass = true;
            lck->rc     = 1;
        } /* end else */
    }     /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_rwlock_rdlock() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_rwlock_rdunlock
 *
 * Purpose:     Releases a shared lock on a R/W lock, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'
 *               │                Release DLFTT R/W Lock
 *               ▼                ----------------------
 *               Λ
 *              ╱ ╲
 *             ╱   ╲
 *            ╱     ╲
 *           ╱       ╲
 *          ╱         ╲  Y ┌────────────┐
 *         ▕  bypass?  ▏──▶│ --refcount │
 *          ╲         ╱    └────────────┘
 *           ╲       ╱            │
 *            ╲     ╱             ▼
 *             ╲   ╱              Λ
 *              ╲ ╱              ╱ ╲
 *               V              ╱   ╲
 *             N │             ╱     ╲
 *               ▼            ╱       ╲
 *        ┌────────────┐     ╱refcount ╲  Y ┌───────────────┐
 *        │   Unlock   │    ▕   == 0?   ▏──▶│bypass = false │
 *        └────────────┘     ╲         ╱    └───────────────┘
 *               │            ╲       ╱             │
 *               │             ╲     ╱              │
 *               │              ╲   ╱               │
 *               │               ╲ ╱                │
 *               │                V                 │
 *               │              N │                 │
 *               │                ▼                 │
 *               │               .─.                │
 *               └─────────────▶( X )◀──────────────┘
 *                               `─'
 *                                │
 *                                ▼
 *                           .─────────.
 *                          (    End    )
 *                           `─────────'
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_rwlock_rdunlock(H5TS_dlftt_rwlock_t *lck)
{
    /* Check if we are bypassing the lock currently */
    if (lck->bypass) {
        /* Decrement refcount */
        lck->rc--;

        /* Check for done bypassing */
        if (0 == lck->rc)
            lck->bypass = false;
    } /* end if */
    else {
        /* Release the lock */
        if (H5_UNLIKELY(H5TS_rwlock_rdunlock(&lck->lck) < 0))
            return FAIL;
    } /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_rwlock_rdunlock() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_rwlock_wrlock
 *
 * Purpose:     Acquires an exclusive lock on a R/W lock, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'          Acquire DLFTT R/W lock
 *               │               ----------------------
 *               ▼
 *               Λ
 *              ╱ ╲
 *             ╱   ╲
 *            ╱     ╲
 *           ╱       ╲
 *          ╱         ╲    N    ┌────────────────┐
 *         ▕  bypass?  ▏───────▶│Get DLFTT value │────────┐
 *          ╲         ╱         └────────────────┘        │
 *           ╲       ╱                                    │
 *            ╲     ╱                                     ▼
 *             ╲   ╱                                      Λ
 *              ╲ ╱                                      ╱ ╲
 *               V                                      ╱   ╲
 *            Y  │                                     ╱     ╲
 *               ▼                                    ╱       ╲
 *        ┌────────────┐     ┌───────────────┐   Y   ╱         ╲
 *        │ ++refcount │◀────│ bypass = true │◀─────▕ DLFTT > 0?▏
 *        └────────────┘     └───────────────┘       ╲         ╱
 *               │                                    ╲       ╱
 *               │                                     ╲     ╱
 *               │                                      ╲   ╱
 *               │                                       ╲ ╱
 *               │                                        V
 *               │                                     N  │
 *               │                                        ▼
 *               │                  .─.            ┌────────────┐
 *               └────────────────▶( X )◀──────────│    Lock    │
 *                                  `─'            └────────────┘
 *                                   │
 *                                   ▼
 *                              .─────────.
 *                             (    End    )
 *                              `─────────'
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_rwlock_wrlock(H5TS_dlftt_rwlock_t *lck)
{
    /* Check whether we are bypassing locking the R/W lock */
    if (lck->bypass)
        /* Increment refcount */
        lck->rc++;
    else {
        unsigned dlftt = 0;

        /* Query the DLFTT value */
        if (H5_UNLIKELY(H5TS__get_dlftt(&dlftt) < 0))
            return FAIL;

        /* Acquire the lock if locking is not disabled */
        if (0 == dlftt) {
            /* Acquire the lock */
            if (H5_UNLIKELY(H5TS_rwlock_wrlock(&lck->lck) < 0))
                return FAIL;
        } /* end if */
        else {
            /* Indicate that lock should be bypassed */
            lck->bypass = true;
            lck->rc     = 1;
        } /* end else */
    }     /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_rwlock_wrlock() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_rwlock_wrlock_downgrade
 *
 * Purpose:     Downgrade a "DLFTT" write lock to read lock without releasing it
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_rwlock_wrlock_downgrade(H5TS_dlftt_rwlock_t *lck)
{
    /* Downgrade if we are not bypassing the lock currently */
    if (!lck->bypass)
        /* Downgrade the lock */
        if (H5_UNLIKELY(H5TS_rwlock_wrlock_downgrade(&lck->lck) < 0))
            return FAIL;

    return SUCCEED;
} /* end H5TS_dlftt_rwlock_wrlock_downgrade() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_rwlock_wrunlock
 *
 * Purpose:     Releases an exclusive lock on a R/W lock, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'
 *               │                Release DLFTT R/W Lock
 *               ▼                ----------------------
 *               Λ
 *              ╱ ╲
 *             ╱   ╲
 *            ╱     ╲
 *           ╱       ╲
 *          ╱         ╲  Y ┌────────────┐
 *         ▕  bypass?  ▏──▶│ --refcount │
 *          ╲         ╱    └────────────┘
 *           ╲       ╱            │
 *            ╲     ╱             ▼
 *             ╲   ╱              Λ
 *              ╲ ╱              ╱ ╲
 *               V              ╱   ╲
 *             N │             ╱     ╲
 *               ▼            ╱       ╲
 *        ┌────────────┐     ╱refcount ╲  Y ┌───────────────┐
 *        │   Unlock   │    ▕   == 0?   ▏──▶│bypass = false │
 *        └────────────┘     ╲         ╱    └───────────────┘
 *               │            ╲       ╱             │
 *               │             ╲     ╱              │
 *               │              ╲   ╱               │
 *               │               ╲ ╱                │
 *               │                V                 │
 *               │              N │                 │
 *               │                ▼                 │
 *               │               .─.                │
 *               └─────────────▶( X )◀──────────────┘
 *                               `─'
 *                                │
 *                                ▼
 *                           .─────────.
 *                          (    End    )
 *                           `─────────'
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_rwlock_wrunlock(H5TS_dlftt_rwlock_t *lck)
{
    /* Check if we are bypassing the lock currently */
    if (lck->bypass) {
        /* Decrement refcount */
        lck->rc--;

        /* Check for done bypassing */
        if (0 == lck->rc)
            lck->bypass = false;
    } /* end if */
    else {
        /* Release the lock */
        if (H5_UNLIKELY(H5TS_rwlock_wrunlock(&lck->lck) < 0))
            return FAIL;
    } /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_rwlock_wrunlock() */

