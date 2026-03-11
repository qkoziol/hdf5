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
 * Purpose: Concurrency wrappers for atomics
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

#define H5TS_ATOMIC_GET_NEXT_INT(obj, limit) H5TS_atomic_get_next_int(obj, limit)

#ifdef H5_HAVE_CONCURRENCY
#define H5TS_ATOMIC_LOAD_INT(obj) H5TS_atomic_load_int(obj)

#define H5TS_ATOMIC_INIT_SIZE_T(obj, desired)  H5TS_atomic_init_size_t(obj, desired)
#define H5TS_ATOMIC_LOAD_SIZE_T(obj)           H5TS_atomic_load_size_t(obj)
#define H5TS_ATOMIC_STORE_SIZE_T(obj, desired) H5TS_atomic_store_size_t(obj, desired)
#define H5TS_ATOMIC_FETCH_ADD_SIZE_T(obj, arg) H5TS_atomic_fetch_add_size_t(obj, arg)
#define H5TS_ATOMIC_FETCH_SUB_SIZE_T(obj, arg) H5TS_atomic_fetch_sub_size_t(obj, arg)
#define H5TS_ATOMIC_DESTROY_SIZE_T(obj)        H5TS_atomic_destroy_size_t(obj)
#else /* H5_HAVE_CONCURRENCY */
#define H5TS_ATOMIC_LOAD_INT(obj) *(obj)

#define H5TS_ATOMIC_INIT_SIZE_T(obj, desired)  *(obj) = (desired)
#define H5TS_ATOMIC_LOAD_SIZE_T(obj)           *(obj)
#define H5TS_ATOMIC_STORE_SIZE_T(obj, desired) *(obj) = (desired)
#define H5TS_ATOMIC_FETCH_ADD_SIZE_T(obj, arg) *(obj) += (arg)
#define H5TS_ATOMIC_FETCH_SUB_SIZE_T(obj, arg) *(obj) -= (arg)
#define H5TS_ATOMIC_DESTROY_SIZE_T(obj)        /* */
#endif                                         /* H5_HAVE_CONCURRENCY */

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

#ifdef H5_HAVE_CONCURRENCY
/*--------------------------------------------------------------------------
 * Function:    H5TS_atomic_get_next_int
 *
 * Purpose:     Retrieves the next value of an integer, up to a limit
 *
 * Note:        Will never return the limit value
 *
 * Return:      -1 when over the limit, a value between [1-limit) otherwise
 *
 *--------------------------------------------------------------------------
 */
static inline int
H5TS_atomic_get_next_int(H5TS_atomic_int_t *obj, int limit)
{
    int cur_val, new_val;

    do {
        cur_val = H5TS_atomic_load_int(obj);
        if (cur_val == limit)
            return -1;
        new_val = cur_val + 1;
    } while (!H5TS_atomic_compare_exchange_weak_int(obj, &cur_val, new_val));

    return cur_val;
} /* end H5TS_atomic_get_next_int() */
#else  /* H5_HAVE_CONCURRENCY */
/*--------------------------------------------------------------------------
 * Function:    H5TS_atomic_get_next_int
 *
 * Purpose:     Retrieves the next value of an integer, up to a limit
 *
 * Note:        Will never return the limit value
 *
 * Return:      -1 when over the limit, a value between [1-limit) otherwise
 *
 *--------------------------------------------------------------------------
 */
static inline int
H5TS_atomic_get_next_int(int *obj, int limit)
{
    int new_val;

    if (*obj < limit) {
        new_val = *obj;
        (*obj)++;
    }
    else
        new_val = -1;

    return new_val;
} /* end H5TS_atomic_get_next_int() */
#endif /* H5_HAVE_CONCURRENCY */
