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
 *        pthread 'pthread_mutex_t' type and capabilities, but efficiently
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
 * Function:    H5TS_dlftt_mutex_acquire
 *
 * Purpose:     Acquires the lock on a mutex, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'          Acquire DLFTT Mutex
 *               │               -------------------
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
 *               └────────────────▶( X )◀──────────│ Lock mutex │
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
H5TS_dlftt_mutex_acquire(H5TS_dlftt_mutex_t *mtx)
{
    /* Check whether we are bypassing locking the mutex */
    if (mtx->bypass)
        /* Increment refcount */
        mtx->rc++;
    else {
        unsigned dlftt = 0;

        /* Query the DLFTT value */
        if (H5_UNLIKELY(H5TS__get_dlftt(&dlftt) < 0))
            return FAIL;

        /* Acquire the mutex if locking is not disabled */
        if (0 == dlftt) {
            /* Acquire the mutex */
            if (H5_UNLIKELY(H5TS_mutex_lock(&mtx->mtx) < 0))
                return FAIL;
        } /* end if */
        else {
            /* Indicate that lock should be bypassed */
            mtx->bypass = true;
            mtx->rc     = 1;
        } /* end else */
    }     /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_mutex_acquire() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_mutex_release
 *
 * Purpose:     Releases the lock on a mutex, obeying the "DLFTT" protocol
 *
 * Note:     	Algorithm flowchart:
 *
 *          .─────────.
 *         (   Start   )
 *          `─────────'
 *               │                Release DLFTT Mutex
 *               ▼                -------------------
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
 *        │Unlock mutex│    ▕   == 0?   ▏──▶│bypass = false │
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
H5TS_dlftt_mutex_release(H5TS_dlftt_mutex_t *mtx)
{
    /* Check if we are bypassing the lock currently */
    if (mtx->bypass) {
        /* Decrement refcount */
        mtx->rc--;

        /* Check for done bypassing */
        if (0 == mtx->rc)
            mtx->bypass = false;
    } /* end if */
    else {
        /* Release the mutex */
        if (H5_UNLIKELY(H5TS_mutex_unlock(&mtx->mtx) < 0))
            return FAIL;
    } /* end else */

    return SUCCEED;
} /* end H5TS_dlftt_mutex_release() */
