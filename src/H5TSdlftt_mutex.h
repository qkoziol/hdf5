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
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_mutex_acquire(H5TS_dlftt_mutex_t *mtx)
{
    /* Query the DLFTT value */
    if (H5_UNLIKELY(H5TS__get_dlftt(&mtx->dlftt) < 0))
        return FAIL;

    /* Don't acquire the mutex if locking is disabled */
    if (0 == mtx->dlftt)
        /* Acquire the mutex */
        if (H5_UNLIKELY(H5TS_mutex_lock(&mtx->mtx) < 0))
            return FAIL;

    return SUCCEED;
} /* end H5TS_dlftt_mutex_acquire() */

/*--------------------------------------------------------------------------
 * Function:    H5TS_dlftt_mutex_release
 *
 * Purpose:     Releases the lock on a mutex, obeying the "DLFTT" protocol
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *--------------------------------------------------------------------------
 */
static inline herr_t
H5TS_dlftt_mutex_release(H5TS_dlftt_mutex_t *mtx)
{
    /* Don't release the mutex if locking is disabled */
    if (0 == mtx->dlftt)
        /* Release the mutex */
        if (H5_UNLIKELY(H5TS_mutex_unlock(&mtx->mtx) < 0))
            return FAIL;

    return SUCCEED;
} /* end H5TS_dlftt_mutex_release() */
