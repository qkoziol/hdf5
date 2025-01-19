#define H5TS_atomic_init_int(obj, desired)  atomic_init((obj), (desired))
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

/************************* */
/* Types in the C Standard */
/************************* */
#define H5TS_ATOMIC_LOAD_INT(obj) H5TS_atomic_load_int(obj)

#define H5TS_ATOMIC_INIT_SIZE_T(obj, desired)  H5TS_atomic_init_size_t(obj, desired)
#define H5TS_ATOMIC_LOAD_SIZE_T(obj)           H5TS_atomic_load_size_t(obj)
#define H5TS_ATOMIC_STORE_SIZE_T(obj, desired) H5TS_atomic_store_size_t(obj, desired)
#define H5TS_ATOMIC_FETCH_ADD_SIZE_T(obj, arg) H5TS_atomic_fetch_add_size_t(obj, arg)
#define H5TS_ATOMIC_FETCH_SUB_SIZE_T(obj, arg) H5TS_atomic_fetch_sub_size_t(obj, arg)
#define H5TS_ATOMIC_DESTROY_SIZE_T(obj)        H5TS_atomic_destroy_size_t(obj)

/*******************************/
/* Types not in the C Standard */
/*******************************/

/* Declarations of variables of this type */
#define H5TS_ATOMIC_TYPE(type) H5_GLUE3(H5TS_atomic_,type,_t)

#if defined(H5_HAVE_STDATOMIC_H) && !defined(__cplusplus)

/* Typedef for variables of this type */
/* (Only needed once per type, in source file or header) */
#define H5TS_DEF_ATOMIC_TYPE(type) typedef _Atomic type H5TS_ATOMIC_TYPE(type);

/* Operations on the type */
#define H5TS_ATOMIC_INIT(type, obj, desired) atomic_init(obj, desired)
#define H5TS_ATOMIC_LOAD(type, obj) atomic_load(obj)
#define H5TS_ATOMIC_STORE(type, obj, desired) atomic_store(obj, desired)
#define H5TS_ATOMIC_FETCH_ADD(type, obj, arg) atomic_fetch_add(obj, arg)
#define H5TS_ATOMIC_FETCH_SUB(type, obj, arg) atomic_fetch_sub(obj, arg)
#define H5TS_ATOMIC_DESTROY(type, obj)        /* */
#else /* defined(H5_HAVE_STDATOMIC_H) && !defined(__cplusplus) */

/* Typedef for variables of this type */
/* (Only needed once per type, in source file or header) */
#define H5TS_DEF_ATOMIC_TYPE(type) \
typedef struct { \
    H5TS_mutex_t mutex; \
    type value; \
} H5TS_ATOMIC_TYPE(type); \
 \
static inline void H5_GLUE(H5TS_atomic_init_, type) (H5TS_ATOMIC_TYPE(type) *obj, type desired) \
{ \
    /* FUNC_ENTER_NOAPI_NAMECHECK_ONLY */ \
 \
    /* Initialize mutex that protects the "atomic" value */ \
    H5TS_mutex_init(&obj->mutex, H5TS_MUTEX_TYPE_PLAIN);  \
 \
    /* Set the value */ \
    obj->value = desired; \
 \
    /* FUNC_LEAVE_NOAPI_VOID_NAMECHECK_ONLY */ \
} \
 \
static inline type H5_GLUE(H5TS_atomic_load_, type) (H5TS_ATOMIC_TYPE(type) *obj) \
{ \
    type ret_value; \
 \
    /* Lock mutex that protects the "atomic" value */ \
    H5TS_mutex_lock(&obj->mutex); \
 \
    /* Get the value */ \
    ret_value = obj->value; \
 \
    /* Release the object's mutex */ \
    H5TS_mutex_unlock(&obj->mutex); \
 \
    return ret_value; \
} \
 \
static inline void H5_GLUE(H5TS_atomic_store_, type) (H5TS_ATOMIC_TYPE(type) *obj, type desired) \
{ \
    /* Lock mutex that protects the "atomic" value */ \
    H5TS_mutex_lock(&obj->mutex); \
 \
    /* Set the value */ \
    obj->value = desired; \
 \
    /* Release the object's mutex */ \
    H5TS_mutex_unlock(&obj->mutex); \
} \
 \
static inline type H5_GLUE(H5TS_atomic_fetch_add_, type) (H5TS_ATOMIC_TYPE(type) *obj, type arg) \
{ \
    type ret_value; \
 \
    /* Lock mutex that protects the "atomic" value */ \
    H5TS_mutex_lock(&obj->mutex); \
 \
    /* Get the current value */ \
    ret_value = obj->value; \
 \
    /* Increment the value */ \
    obj->value += arg; \
 \
    /* Release the object's mutex */ \
    H5TS_mutex_unlock(&obj->mutex); \
 \
    return ret_value; \
} \
 \
static inline type H5_GLUE(H5TS_atomic_fetch_sub_, type) (H5TS_ATOMIC_TYPE(type) *obj, type arg) \
{ \
    type ret_value; \
 \
    /* Lock mutex that protects the "atomic" value */ \
    H5TS_mutex_lock(&obj->mutex); \
 \
    /* Get the current value */ \
    ret_value = obj->value; \
 \
    /* Decrement the value */ \
    obj->value -= arg; \
 \
    /* Release the object's mutex */ \
    H5TS_mutex_unlock(&obj->mutex); \
 \
    return ret_value; \
} \
 \
static inline void H5_GLUE(H5TS_atomic_destroy_, type) (H5TS_ATOMIC_TYPE(type) *obj) \
{ \
    /* Destroy mutex that protects the "atomic" value */ \
    H5TS_mutex_destroy(&obj->mutex); \
}

/* Operations on the type */
#define H5TS_ATOMIC_INIT(type, obj, desired) H5_GLUE(H5TS_atomic_init_, type)(obj, desired)
#define H5TS_ATOMIC_LOAD(type, obj) H5_GLUE(H5TS_atomic_load_, type)(obj)
#define H5TS_ATOMIC_STORE(type, obj, desired) H5_GLUE(H5TS_atomic_store_, type)(obj, desired)
#define H5TS_ATOMIC_FETCH_ADD(type, obj, arg) H5_GLUE(H5TS_atomic_fetch_add_, type)(obj, arg)
#define H5TS_ATOMIC_FETCH_SUB(type, obj, arg) H5_GLUE(H5TS_atomic_fetch_sub_, type)(obj, arg)
#define H5TS_ATOMIC_DESTROY(type, obj) H5_GLUE(H5TS_atomic_destroy_, type)(obj)
#endif /* defined(H5_HAVE_STDATOMIC_H) && !defined(__cplusplus) */

#else /* H5_HAVE_CONCURRENCY */

/************************* */
/* Types in the C Standard */
/************************* */

#define H5TS_ATOMIC_LOAD_INT(obj) *(obj)

#define H5TS_ATOMIC_INIT_SIZE_T(obj, desired)  *(obj) = (desired)
#define H5TS_ATOMIC_LOAD_SIZE_T(obj)           *(obj)
#define H5TS_ATOMIC_STORE_SIZE_T(obj, desired) *(obj) = (desired)
#define H5TS_ATOMIC_FETCH_ADD_SIZE_T(obj, arg) *(obj) += (arg)
#define H5TS_ATOMIC_FETCH_SUB_SIZE_T(obj, arg) *(obj) -= (arg)
#define H5TS_ATOMIC_DESTROY_SIZE_T(obj)        /* */

/*******************************/
/* Types not in the C Standard */
/*******************************/

/* Declarations of variables of this type */
#define H5TS_ATOMIC_TYPE(type) H5_GLUE3(H5TS_atomic_,type,_t)

/* Typedef for variables of this type */
/* (Only needed once per type, in source file or header) */
#define H5TS_DEF_ATOMIC_TYPE(type) typedef type H5TS_ATOMIC_TYPE(type);

/* Operations on the type */
#define H5TS_ATOMIC_INIT(type, obj, desired)  *(obj) = (desired)
#define H5TS_ATOMIC_LOAD(type, obj)           *(obj)
#define H5TS_ATOMIC_STORE(type, obj, desired) *(obj) = (desired)
#define H5TS_ATOMIC_FETCH_ADD(type, obj, arg) *(obj) += (arg)
#define H5TS_ATOMIC_FETCH_SUB(type, obj, arg) *(obj) -= (arg)
#define H5TS_ATOMIC_DESTROY(type, obj)        /* */

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
