/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the LICENSE file, which can be found at the root of the source code       *
 * distribution tree, or in https://www.hdfgroup.org/licenses.               *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Purpose: This file contains support for non-recursive R/W locks, equivalent
 *        to the pthread 'pthread_rwlock_t' type and capabilities.
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

#ifdef H5_HAVE_C11_THREADS
/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_lock
 *
 * Purpose:  Acquire a read or write lock, determined at runtime
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_lock(H5TS_rwlock_t *lock, H5TS_rwlock_lock_mode_t mode)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Determine type of lock to acquire */
    if (H5TS_RWLOCK_LOCK_SHARED == mode) {
        /* Check for writers */
        if (lock->writers || lock->write_waiters) {
            /* Read waiting */
            lock->read_waiters++;

            /* Wait for writers */
            do {
                if (H5_UNLIKELY(thrd_success != cnd_wait(&lock->read_cv, &lock->mutex))) {
                    mtx_unlock(&lock->mutex);
                    return FAIL;
                }
            } while (lock->writers || lock->write_waiters);

            /* Read not waiting any longer */
            lock->read_waiters--;
        }

        /* Increment # of readers */
        lock->readers++;
    }
    else {
        /* Check for readers or other writers */
        if (lock->readers || lock->writers) {
            /* Write waiting */
            lock->write_waiters++;

            /* Wait for mutex */
            do {
                if (H5_UNLIKELY(thrd_success != cnd_wait(&lock->write_cv, &lock->mutex))) {
                    mtx_unlock(&lock->mutex);
                    return FAIL;
                }
            } while (lock->readers || lock->writers);

            /* Write not waiting any longer */
            lock->write_waiters--;
        }

        /* Increment # of writers */
        lock->writers++;
    }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_lock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_unlock
 *
 * Purpose:  Release a read or write lock, determined at runtime
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_unlock(H5TS_rwlock_t *lock, H5TS_rwlock_lock_mode_t mode)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Determine type of lock to release */
    if (H5TS_RWLOCK_LOCK_SHARED == mode) {
        /* Decrement # of readers */
        lock->readers--;

        /* Check for waiting writers when last readers */
        if (lock->write_waiters && 0 == lock->readers)
            if (H5_UNLIKELY(cnd_signal(&lock->write_cv) != thrd_success)) {
                mtx_unlock(&lock->mutex);
                return FAIL;
            }
    }
    else {
        /* Decrement # of writers */
        lock->writers--;

        /* Check for waiting writers */
        if (lock->write_waiters) {
            if (H5_UNLIKELY(cnd_signal(&lock->write_cv) != thrd_success)) {
                mtx_unlock(&lock->mutex);
                return FAIL;
            }
        }
        else if (lock->read_waiters)
            if (H5_UNLIKELY(cnd_broadcast(&lock->read_cv) != thrd_success)) {
                mtx_unlock(&lock->mutex);
                return FAIL;
            }
    }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_unlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_rdlock
 *
 * Purpose:  Acquire a read lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_rdlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Check for writers */
    if (lock->writers || lock->write_waiters) {
        /* Read waiting */
        lock->read_waiters++;

        /* Wait for writers */
        do {
            if (H5_UNLIKELY(thrd_success != cnd_wait(&lock->read_cv, &lock->mutex))) {
                mtx_unlock(&lock->mutex);
                return FAIL;
            }
        } while (lock->writers || lock->write_waiters);

        /* Read not waiting any longer */
        lock->read_waiters--;
    }

    /* Increment # of readers */
    lock->readers++;

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_rdlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_rdunlock
 *
 * Purpose:  Release a read lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_rdunlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Decrement # of readers */
    lock->readers--;

    /* Check for waiting writers when last readers */
    if (lock->write_waiters && 0 == lock->readers)
        if (H5_UNLIKELY(cnd_signal(&lock->write_cv) != thrd_success)) {
            mtx_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_rdunlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrlock
 *
 * Purpose:  Acquire a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Check for readers or other writers */
    if (lock->readers || lock->writers) {
        /* Write waiting */
        lock->write_waiters++;

        /* Wait for mutex */
        do {
            if (H5_UNLIKELY(thrd_success != cnd_wait(&lock->write_cv, &lock->mutex))) {
                mtx_unlock(&lock->mutex);
                return FAIL;
            }
        } while (lock->readers || lock->writers);

        /* Write not waiting any longer */
        lock->write_waiters--;
    }

    /* Increment # of writers */
    lock->writers++;

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_trywrlock
 *
 * Purpose:  Attempt to acquire a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_trywrlock(H5TS_rwlock_t *lock, bool *acquired)
{
    int ret;

    /* Check argument */
    if (H5_UNLIKELY(NULL == lock || NULL == acquired))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(thrd_error == (ret = mtx_lock(&lock->mutex))))
        return FAIL;
    if (thrd_busy == ret) {
        /* We did not acquire the lock */
        *acquired = false;
        return SUCCEED;
    }

    /* Check for readers or other writers */
    if (lock->readers || lock->writers)
        /* We did not acquire the lock */
        *acquired = false;
    else {
        /* Increment # of writers */
        lock->writers++;

        /* We acquired the lock */
        *acquired = true;
    }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_trywrlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrlock_downgrade
 *
 * Purpose:  Downgrade a write lock to read lock without releasing it
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrlock_downgrade(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Decrement # of writers */
    lock->writers--;

    /* Increment # of readers */
    lock->readers++;

    /* Wake other readers, if no waiting writers */
    if (lock->read_waiters && 0 == lock->write_waiters)
        if (H5_UNLIKELY(cnd_broadcast(&lock->read_cv) != thrd_success)) {
            mtx_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrlock_downgrade() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrunlock
 *
 * Purpose:  Release a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrunlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(mtx_lock(&lock->mutex) != thrd_success))
        return FAIL;

    /* Decrement # of writers */
    lock->writers--;

    /* Check for waiting writers */
    if (lock->write_waiters) {
        if (H5_UNLIKELY(cnd_signal(&lock->write_cv) != thrd_success)) {
            mtx_unlock(&lock->mutex);
            return FAIL;
        }
    }
    else if (lock->read_waiters)
        if (H5_UNLIKELY(cnd_broadcast(&lock->read_cv) != thrd_success)) {
            mtx_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(mtx_unlock(&lock->mutex) != thrd_success))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrunlock() */

#else
/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_lock
 *
 * Purpose:  Acquire a read or write lock, determined at runtime
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_lock(H5TS_rwlock_t *lock, H5TS_rwlock_lock_mode_t mode)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Determine type of lock to acquire */
    if (H5TS_RWLOCK_LOCK_SHARED == mode) {
        /* Check for writers */
        if (lock->writers || lock->write_waiters) {
            /* Read waiting */
            lock->read_waiters++;

            /* Wait for writers */
            do {
                if (H5_UNLIKELY(H5TS_cond_wait(&lock->read_cv, &lock->mutex))) {
                    H5TS_mutex_unlock(&lock->mutex);
                    return FAIL;
                }
            } while (lock->writers || lock->write_waiters);

            /* Read not waiting any longer */
            lock->read_waiters--;
        }

        /* Increment # of readers */
        lock->readers++;
    }
    else {
        /* Check for readers or other writers */
        if (lock->readers || lock->writers) {
            /* Write waiting */
            lock->write_waiters++;

            /* Wait for mutex */
            do {
                if (H5_UNLIKELY(H5TS_cond_wait(&lock->write_cv, &lock->mutex))) {
                    H5TS_mutex_unlock(&lock->mutex);
                    return FAIL;
                }
            } while (lock->readers || lock->writers);

            /* Write not waiting any longer */
            lock->write_waiters--;
        }

        /* Increment # of writers */
        lock->writers++;
    }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_lock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_unlock
 *
 * Purpose:  Release a read or write lock, determined at runtime
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_unlock(H5TS_rwlock_t *lock, H5TS_rwlock_lock_mode_t mode)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Determine type of lock to acquire */
    if (H5TS_RWLOCK_LOCK_SHARED == mode) {
        /* Decrement # of readers */
        lock->readers--;

        /* Check for waiting writers when last readers */
        if (lock->write_waiters && 0 == lock->readers)
            if (H5_UNLIKELY(H5TS_cond_signal(&lock->write_cv))) {
                H5TS_mutex_unlock(&lock->mutex);
                return FAIL;
            }
    }
    else {
        /* Decrement # of writers */
        lock->writers--;

        /* Check for waiting writers */
        if (lock->write_waiters) {
            if (H5_UNLIKELY(H5TS_cond_signal(&lock->write_cv))) {
                H5TS_mutex_unlock(&lock->mutex);
                return FAIL;
            }
        }
        else if (lock->read_waiters)
            if (H5_UNLIKELY(H5TS_cond_broadcast(&lock->read_cv))) {
                H5TS_mutex_unlock(&lock->mutex);
                return FAIL;
            }
    }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_lock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_rdlock
 *
 * Purpose:  Acquire a read lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_rdlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Check for writers */
    if (lock->writers || lock->write_waiters) {
        /* Read waiting */
        lock->read_waiters++;

        /* Wait for writers */
        do {
            if (H5_UNLIKELY(H5TS_cond_wait(&lock->read_cv, &lock->mutex))) {
                H5TS_mutex_unlock(&lock->mutex);
                return FAIL;
            }
        } while (lock->writers || lock->write_waiters);

        /* Read not waiting any longer */
        lock->read_waiters--;
    }

    /* Increment # of readers */
    lock->readers++;

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_rdlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_rdunlock
 *
 * Purpose:  Release a read lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_rdunlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Decrement # of readers */
    lock->readers--;

    /* Check for waiting writers when last readers */
    if (lock->write_waiters && 0 == lock->readers)
        if (H5_UNLIKELY(H5TS_cond_signal(&lock->write_cv))) {
            H5TS_mutex_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_rdunlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrlock
 *
 * Purpose:  Acquire a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Check for readers or other writers */
    if (lock->readers || lock->writers) {
        /* Write waiting */
        lock->write_waiters++;

        /* Wait for mutex */
        do {
            if (H5_UNLIKELY(H5TS_cond_wait(&lock->write_cv, &lock->mutex))) {
                H5TS_mutex_unlock(&lock->mutex);
                return FAIL;
            }
        } while (lock->readers || lock->writers);

        /* Write not waiting any longer */
        lock->write_waiters--;
    }

    /* Increment # of writers */
    lock->writers++;

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_trywrlock
 *
 * Purpose:  Attempt to acquire a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_trywrlock(H5TS_rwlock_t *lock, bool *acquired)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock || NULL == acquired))
        return FAIL;

    /* Attempt to acquire the lock */
    if (H5_UNLIKELY(H5TS_mutex_trylock(&lock->mutex, acquired) < 0))
        return FAIL;
    if (!*acquired)
        /* We did not acquire the lock */
        return SUCCEED;

    /* Check for readers or other writers */
    if (lock->readers || lock->writers)
        /* We did not acquire the lock */
        *acquired = false;
    else {
        /* Increment # of writers */
        lock->writers++;

        /* We acquired the lock */
        *acquired = true;
    }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_trywrlock() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrlock_downgrade
 *
 * Purpose:  Downgrade a write lock to read lock without releasing it
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrlock_downgrade(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Decrement # of writers */
    lock->writers--;

    /* Increment # of readers */
    lock->readers++;

    /* Wake other readers, if no waiting writers */
    if (lock->read_waiters && 0 == lock->write_waiters)
        if (H5_UNLIKELY(H5TS_cond_broadcast(&lock->read_cv))) {
            H5TS_mutex_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrlock_downgrade() */

/*-------------------------------------------------------------------------
 * Function: H5TS_rwlock_wrunlock
 *
 * Purpose:  Release a write lock
 *
 * Return:   Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static inline herr_t
H5TS_rwlock_wrunlock(H5TS_rwlock_t *lock)
{
    /* Check argument */
    if (H5_UNLIKELY(NULL == lock))
        return FAIL;

    /* Acquire the lock's mutex */
    if (H5_UNLIKELY(H5TS_mutex_lock(&lock->mutex)))
        return FAIL;

    /* Decrement # of writers */
    lock->writers--;

    /* Check for waiting writers */
    if (lock->write_waiters) {
        if (H5_UNLIKELY(H5TS_cond_signal(&lock->write_cv))) {
            H5TS_mutex_unlock(&lock->mutex);
            return FAIL;
        }
    }
    else if (lock->read_waiters)
        if (H5_UNLIKELY(H5TS_cond_broadcast(&lock->read_cv))) {
            H5TS_mutex_unlock(&lock->mutex);
            return FAIL;
        }

    /* Release mutex */
    if (H5_UNLIKELY(H5TS_mutex_unlock(&lock->mutex)))
        return FAIL;

    return SUCCEED;
} /* end H5TS_rwlock_wrunlock() */
#endif
