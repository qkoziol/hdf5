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
 * H5Iint.c - Private routines for handling IDs
 */

/****************/
/* Module Setup */
/****************/

#include "H5Imodule.h" /* This source code file is part of the H5I module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5FLprivate.h" /* Free Lists                               */
#include "H5Ipkg.h"      /* IDs                                      */
#include "H5MMprivate.h" /* Memory management                        */
#include "H5Tprivate.h"  /* Datatypes                                */
#include "H5VLprivate.h" /* Virtual Object Layer                     */

/****************/
/* Local Macros */
/****************/

/* Combine a Type number and an ID index into an ID */
#define H5I_MAKE(g, i) ((((hid_t)(g) & TYPE_MASK) << ID_BITS) | ((hid_t)(i) & ID_MASK))

/******************/
/* Local Typedefs */
/******************/

/* User data for iterator callback for retrieving an ID corresponding to an object pointer */
typedef struct {
    const void *object;   /* object pointer to search for */
    H5I_type_t  obj_type; /* type of object we are searching for */
    hid_t       ret_id;   /* ID returned */
} H5I_get_id_ud_t;

/* User data for iterator callback for ID iteration */
typedef struct {
    H5I_search_func_t user_func;  /* 'User' function to invoke */
    void             *user_udata; /* User data to pass to 'user' function */
    bool              app_ref;    /* Whether this is an appl. ref. call */
    H5I_type_t        obj_type;   /* Type of object we are iterating over */
} H5I_iterate_ud_t;

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/

static void  *H5I__unwrap(void *object, H5I_type_t type);
static herr_t H5I__clear_type(H5I_type_info_t *type_info, bool force, bool app_ref);
static herr_t H5I__destroy_type_info(H5I_type_t type, H5I_type_info_t *type_info);
static herr_t H5I__remove_id_info(H5I_type_info_t *type_info, H5I_id_info_t *info, void **request,
                                  bool make_cb, bool force, bool try, bool id_locked);
static void  *H5I__remove_common(H5I_type_info_t *type_info, H5I_id_info_t *info, void **request,
                                 bool make_cb);
static int    H5I__dec_ref(hid_t id, void **request);
static int    H5I__dec_app_ref(hid_t id, void **request);
static int    H5I__dec_app_ref_always_close(hid_t id, void **request);
static herr_t H5I__lookup_id(H5I_type_info_t *type_info, hid_t id, H5I_id_info_t **out_id_info,
                             H5I_lock_mode_t mode);
static herr_t H5I__find_id_with_type(hid_t id, H5I_id_info_t **out_id_info, H5I_lock_mode_t id_lock_mode,
                                     H5I_type_info_t **out_type_info, H5I_lock_mode_t type_lock_mode);
static herr_t H5I__find_id(hid_t id, H5I_id_info_t **id_info, H5I_lock_mode_t id_lock_mode);
static herr_t H5I__id_exists(hid_t id, bool *exists);
static int    H5I__find_id_cb(void *_item, void *_key, void *_udata);
static herr_t H5I__type_info_free(H5I_type_info_t *type_info);
static herr_t H5I__type_info_wrlock_downgrade(H5I_type_t type);
static herr_t H5I__id_info_rdlock(H5I_id_info_t *info);
static herr_t H5I__id_info_wrlock(H5I_id_info_t *info);
static herr_t H5I__id_info_wrunlock_downgrade(H5I_id_info_t *info);
static herr_t H5I__id_info_rdunlock(H5I_id_info_t *info);
static herr_t H5I__id_info_wrunlock(H5I_id_info_t *info);
static herr_t H5I__id_info_free(H5I_id_info_t *info, bool is_locked);

/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
bool H5_PKG_INIT_VAR = false;

/* Concurrency globals for type info array */
#ifdef H5_HAVE_CONCURRENCY
static bool H5I_concur_gbl_init; /* Whether the global mutex & atomic variables have been initialized */
#endif                           /* H5_HAVE_CONCURRENCY */

/* Declared extern in H5Ipkg.h and documented there */
H5I_ti_arr_elmt_t H5I_type_info_array_g[H5I_MAX_NUM_TYPES];
H5TS_ATOMIC_TYPE(int) H5I_next_type_g;

/* Declare a free list to manage the H5I_id_info_t struct */
H5FL_DEFINE_STATIC(H5I_id_info_t);

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5I__init_package
 *
 * Purpose:     Initialize interface-specific information.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__init_package(void)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

    /* Initialize the global atomic variables */
    H5TS_ATOMIC_INIT(int, &H5I_next_type_g, (int)H5I_NTYPES);

#ifdef H5_HAVE_CONCURRENCY
    /* Sanity check */
    assert(!H5I_concur_gbl_init);

    /* Initialize the mutexes protecting the type information */
    for (unsigned u = 0; u < H5I_MAX_NUM_TYPES; u++) {
        if (H5TS_dlftt_rwlock_init(&H5I_type_info_array_g[u].lock) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTINIT, FAIL, "can't initialize global type info's mutex");
        H5I_type_info_array_g[u].lock_init = true;
    } /* end for */

    /* Indicate that the concurrency globals are initialized */
    H5I_concur_gbl_init = true;
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5I_term_package
 *
 * Purpose:     Terminate the H5I interface: release all memory, reset all
 *              global variables to initial values. This only happens if all
 *              types have been destroyed from other interfaces.
 *
 * Return:      Success:    Positive if any action was taken that might
 *                          affect some other interface; zero otherwise.
 *
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5I_term_package(void)
{
    int in_use = 0; /* Number of ID types still in use */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (H5_PKG_INIT_VAR) {
        H5I_type_info_t *type_info = NULL; /* Pointer to ID type */
        int              i;

        /* Count the number of types still in use */
        for (i = 0; i < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g); i++) {
            /* Acquire shared access for the type */
            H5I__type_info_rdlock(i);

            if ((type_info = H5I_type_info_array_g[i].type_info) && type_info->hash_table)
                in_use++;

            /* Release shared access for the type */
            H5I__type_info_rdunlock(i);
        }

        /* If no types are still being used then clean up */
        if (0 == in_use) {
            for (i = 0; i < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g); i++) {
                /* Acquire exclusive access for the type */
                H5I__type_info_wrlock(i);

                type_info = H5I_type_info_array_g[i].type_info;
                if (type_info) {
                    assert(NULL == type_info->hash_table);
                    H5I__type_info_free(type_info);
                    H5I_type_info_array_g[i].type_info = NULL;
                    in_use++;
                }

                /* Release exclusive access for the type */
                H5I__type_info_wrunlock(i);
            }

            /* Shut down interface */
            if (0 == in_use) {
                /* Destroy the type info counter */
                H5TS_ATOMIC_DESTROY(int, &H5I_next_type_g);

#ifdef H5_HAVE_CONCURRENCY
                /* Indicate that the concurrency globals are initialized */
                if (H5I_concur_gbl_init) {
                    /* Destroy the mutexes protecting global type info array elements */
                    for (unsigned u = 0; u < H5I_MAX_NUM_TYPES; u++)
                        if (H5I_type_info_array_g[u].lock_init) {
                            H5TS_dlftt_rwlock_destroy(&H5I_type_info_array_g[u].lock);
                            H5I_type_info_array_g[u].lock_init = false;
                        }

                    H5I_concur_gbl_init = false;
                }
#endif /* H5_HAVE_CONCURRENCY */

                /* Mark interface closed */
                H5_PKG_INIT_VAR = false;
            }
        }
    }

    FUNC_LEAVE_NOAPI(in_use)
} /* end H5I_term_package() */

/*-------------------------------------------------------------------------
 * Function:    H5I_register_type
 *
 * Purpose:     Creates a new type of ID's to give out.
 *              The class is initialized or its reference count is incremented
 *              (if it is already initialized).
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_type(H5I_class_t *cls, bool internal)
{
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the ID type*/
    bool             have_type_lock = false;   /* Whether the type's lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(cls);

    /* Allocate the type information for new type */
    if (NULL == (type_info = (H5I_type_info_t *)H5MM_calloc(sizeof(H5I_type_info_t))))
        HGOTO_ERROR(H5E_ID, H5E_CANTALLOC, FAIL, "ID type allocation failed");

    /* Initialize the non-zero fields */
    type_info->cls         = cls;
    type_info->nextid      = cls->reserved;
    type_info->init_count  = 1;
    type_info->is_internal = internal;

    /* Generate a new H5I_type_t value, if necessary */
    if (H5I_UNINIT == cls->type) {
        H5I_type_t new_type; /* New ID type value */

        /* Get the next available type value */
        new_type = H5TS_ATOMIC_GET_NEXT_INT(&H5I_next_type_g, H5I_MAX_NUM_TYPES);
        if (new_type < 0) {
            bool found = false; /* If search was successful */

            /* Look for a free type to give out */
            for (int i = H5I_NTYPES; i < H5I_MAX_NUM_TYPES; i++) {
                /* Acquire exclusive access to the global type info */
                if (H5I__type_info_wrlock(i) < 0)
                    HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");
                have_type_lock = true;

                if (NULL == H5I_type_info_array_g[i].type_info) {
                    /* Found a free type ID */
                    new_type = (H5I_type_t)i;
                    found    = true;
                    break;
                }

                /* Release the lock protecting the global type info */
                have_type_lock = false;
                if (H5I__type_info_wrunlock(i) < 0)
                    HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");
            }

            /* Verify that we found a type to give out */
            if (found == false)
                HGOTO_ERROR(H5E_ID, H5E_NOSPACE, H5I_BADID, "Maximum number of ID types exceeded");
        }

        /* Set type for class */
        cls->type = new_type;
    }

    /* Acquire the lock if not already held */
    if (!have_type_lock) {
        /* Acquire exclusive access to  the global type info */
        if (H5I__type_info_wrlock(cls->type) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");
        have_type_lock = true;
    }

    /* Claim the location in the array */
    H5I_type_info_array_g[cls->type].type_info = type_info;

done:
    /* Release the lock if held */
    if (have_type_lock && H5I__type_info_wrunlock(cls->type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");

    /* Clean up on error */
    if (ret_value < 0)
        if (type_info)
            H5I__type_info_free(type_info);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_register_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I_nmembers
 *
 * Purpose:     Returns the number of members in a type.
 *
 * Return:      Success:    Number of members; zero if the type is empty
 *                          or has been deleted.
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
int64_t
H5I_nmembers(H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the ID type */
    bool             have_type_lock = false; /* Whether the type's lock is held */
    int64_t          ret_value      = 0;     /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Validate parameter */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Acquire a shared lock on the global type info */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");
    have_type_lock = true;

    /* Check for valid type */
    if (NULL == (type_info = H5I_type_info_array_g[type].type_info))
        HGOTO_DONE(0);

    /* Check if type is initialized */
    if (type_info->init_count <= 0)
        HGOTO_DONE(0);

    /* Set return value */
    H5_CHECKED_ASSIGN(ret_value, int64_t, type_info->id_count, uint64_t);

done:
    /* Release the lock if held */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_nmembers() */

/*-------------------------------------------------------------------------
 * Function:    H5I__unwrap
 *
 * Purpose:     Unwraps the object pointer for the 'item' that corresponds
 *              to an ID.
 *
 * Return:      Pointer to the unwrapped pointer (can't fail)
 *
 *-------------------------------------------------------------------------
 */
static void *
H5I__unwrap(void *object, H5I_type_t type)
{
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity checks */
    assert(object);

    /* The stored object pointer might be an H5VL_object_t, in which
     * case we'll need to get the wrapped object struct (H5F_t *, etc.).
     */
    if (H5I_FILE == type || H5I_GROUP == type || H5I_DATASET == type || H5I_ATTR == type) {
        const H5VL_object_t *vol_obj = (const H5VL_object_t *)object;

        ret_value = H5VL_object_data(vol_obj);
    }
    else if (H5I_DATATYPE == type) {
        H5T_t *dt = (H5T_t *)object;

        ret_value = (void *)H5T_get_actual_type(dt);
    }
    else
        ret_value = object;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__unwrap() */

/*-------------------------------------------------------------------------
 * Function:    H5I__del_id_from_type
 *
 * Purpose:     Delete an ID from a type, freeing it if possible
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__clear_type(H5I_type_info_t *type_info, bool force, bool app_ref)
{
    H5I_id_info_t *item      = NULL;
    H5I_id_info_t *tmp       = NULL;
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Increment the generation of the type */
    type_info->gen++;

    /* Indicate that we're iterating this type right now */
    type_info->iterating++;

    /* Delete nodes from the local hash table */
    HASH_ITER(hh, type_info->hash_table, item, tmp)
    {
        /* Check if this ID node was deleted, through an iteration callback */
        if (item->del_later) {
            /* Remove ID from hash table */
            if (H5I__remove_id_info(type_info, item, H5_REQUEST_NULL, item->make_cb_later, true, false,
                                    false) < 0) {
                type_info->iterating--;
                HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, FAIL, "can't remove ID node from hash table");
            }
        }
        else {
            /* Delete the object if its refcount is <= 1 or forcing is on */
            if (force || (item->count - (!app_ref * item->app_count)) <= 1) {
                herr_t status; /* Whether ID was successfully removed */

                /* Try removing ID from hash table */
                status = H5I__remove_id_info(type_info, item, H5_REQUEST_NULL, true, force, true, false);

                /* If not successful, move the item to a new type generation */
                if (status < 0)
                    item->gen = type_info->gen;
            }
            else
                /* Move item to new type generation */
                item->gen = type_info->gen;
        }
    }

    /* Indicate that we're done iterating this type */
    type_info->iterating--;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__clear_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I_clear_type
 *
 * Purpose:     Removes all objects from the type, calling the free
 *              function for each object regardless of the reference count.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_clear_type(H5I_type_t type, bool force, bool app_ref)
{
    bool   have_type_lock = false;   /* Whether the lock is held */
    herr_t ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Validate parameters */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Clear the objects from the type */
    if (H5I__clear_type(H5I_type_info_array_g[type].type_info, force, app_ref) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTRELEASE, FAIL, "can't release IDs for type");

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_clear_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I__destroy_type_info
 *
 * Purpose:     Internal routine to destroysa type along with all IDs in that
 *              type regardless of their reference counts. Destroying IDs
 *              involves calling the free-func for each ID's object and
 *              then adding the ID struct to the ID free list.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__destroy_type_info(H5I_type_t type, H5I_type_info_t *type_info)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Close/clear/destroy all IDs for this type */
    if (H5I__clear_type(type_info, true, false) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTRELEASE, FAIL, "can't release IDs for type");

    /* Reset the global type info pointer */
    H5I_type_info_array_g[type].type_info = NULL;

    /* Release type info object */
    if (H5I__type_info_free(type_info) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTRELEASE, FAIL, "can't release type info object");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__destroy_type_info() */

/*-------------------------------------------------------------------------
 * Function:    H5I__destroy_type
 *
 * Purpose:     Destroys a type along with all IDs in that type
 *              regardless of their reference counts. Destroying IDs
 *              involves calling the free-func for each ID's object and
 *              then adding the ID struct to the ID free list.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__destroy_type(H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the ID type */
    bool             have_type_lock = false;   /* Whether the lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Validate parameter */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Close/clear/destroy all IDs for this type */
    if (H5I__destroy_type_info(type, type_info) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTRELEASE, FAIL, "can't release IDs for type");

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__destroy_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I__register
 *
 * Purpose:     Registers an OBJECT in a TYPE and returns an ID for it.
 *              This routine does _not_ check for unique-ness of the objects,
 *              if you register an object twice, you will get two different
 *              IDs for it.  This routine does make certain that each ID in a
 *              type is unique.  IDs are created by getting a unique number
 *              for the type the ID is in and incorporating the TYPE into
 *              the ID which is returned to the user.
 *
 *              IDs are marked as "future" if the realize_cb and discard_cb
 *              parameters are non-NULL.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I__register(H5I_type_t type, const void *object, bool app_ref, H5I_future_realize_func_t realize_cb,
              H5I_future_discard_func_t discard_cb)
{
    H5I_type_info_t *type_info      = NULL;            /* Pointer to the type */
    H5I_id_info_t   *info           = NULL;            /* Pointer to the new ID information */
    hid_t            new_id         = H5I_INVALID_HID; /* New ID */
    bool             have_type_lock = false;           /* Whether the lock is held */
    hid_t            ret_value      = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, H5I_INVALID_HID, "invalid type number");

    /* Allocate new ID info */
    if (NULL == (info = H5FL_CALLOC(H5I_id_info_t)))
        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, H5I_INVALID_HID, "memory allocation failed");

    /* Create the ID info */
    info->count      = 1; /* initial reference count */
    info->app_count  = !!app_ref;
    info->u.c_object = object;
    info->is_future  = (NULL != realize_cb);
    info->realize_cb = realize_cb;
    info->discard_cb = discard_cb;
#ifdef H5_HAVE_CONCURRENCY
    /* Initialize the R/W lock protecting the ID info */
    if (H5TS_dlftt_rwlock_init(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTINIT, H5I_INVALID_HID, "can't initialize ID's lock");
    info->lock_init = true;
#endif /* H5_HAVE_CONCURRENCY */

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, H5I_INVALID_HID, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (NULL == type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, H5I_INVALID_HID, "invalid type");

    /* Set up the ID for the object */
    new_id    = H5I_MAKE(type, type_info->nextid);
    info->id  = new_id;
    info->gen = type_info->gen;

    /* Insert into the type */
    HASH_ADD(hh, type_info->hash_table, id, sizeof(hid_t), info);
    type_info->id_count++;
    type_info->nextid++;
    if (info->is_future)
        type_info->num_fut_ids++;

    /* Sanity check for the 'nextid' getting too large and wrapping around */
    assert(type_info->nextid <= ID_MASK);

    /* Set the most recent ID to this object */
    type_info->last_id_info = info;

    /* Set return value */
    ret_value = new_id;

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, H5I_INVALID_HID, "can't release lock on type");

    /* Release the ID info on error */
    if (ret_value < 0)
        if (info)
            H5I__id_info_free(info, false);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__register() */

/*-------------------------------------------------------------------------
 * Function:    H5I_register
 *
 * Purpose:     Library-private wrapper for H5I__register.
 *
 * Return:      Success:    New object ID
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5I_register(H5I_type_t type, const void *object, bool app_ref)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    /* Sanity checks */
    assert(type >= H5I_FILE && type < H5I_NTYPES);
    assert(object);

    /* Retrieve ID for object */
    if (H5I_INVALID_HID == (ret_value = H5I__register(type, object, app_ref, NULL, NULL)))
        HGOTO_ERROR(H5E_ID, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register object");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_register() */

/*-------------------------------------------------------------------------
 * Function:    H5I_register_using_existing_id
 *
 * Purpose:     Registers an OBJECT in a TYPE with the supplied ID for it.
 *              This routine will check to ensure the supplied ID is not already
 *              in use, and ensure that it is a valid ID for the given type,
 *              but will NOT check to ensure the OBJECT is not already
 *              registered (thus, it is possible to register one object under
 *              multiple IDs).
 *
 * NOTE:        Intended for use in refresh calls, where we have to close
 *              and re-open the underlying data, then hook the object back
 *              up to the original ID.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_register_using_existing_id(H5I_type_t type, void *object, bool app_ref, hid_t existing_id)
{
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the type */
    H5I_id_info_t   *info           = NULL;    /* Pointer to the new ID information */
    bool             have_type_lock = false;   /* Whether the type lock is held */
    bool             id_exists      = false;   /* Whether ID exists already */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check arguments */
    assert(object);

    /* Make sure ID is not already in use */
    if (H5I__id_exists(existing_id, &id_exists) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTGET, FAIL, "error when determining if ID already in use");
    if (true == id_exists)
        HGOTO_ERROR(H5E_ID, H5E_BADVALUE, FAIL, "ID already in use");

    /* Make sure type number is valid */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Make sure requested ID belongs to object's type */
    if (H5I_TYPE(existing_id) != type)
        HGOTO_ERROR(H5E_ID, H5E_BADRANGE, FAIL, "invalid type for provided ID");

    /* Allocate new structure to house this ID */
    if (NULL == (info = H5FL_CALLOC(H5I_id_info_t)))
        HGOTO_ERROR(H5E_ID, H5E_NOSPACE, FAIL, "memory allocation failed");

    /* Create the struct & insert requested ID */
    info->id        = existing_id;
    info->count     = 1; /* initial reference count*/
    info->app_count = !!app_ref;
    info->u.object  = object;
    /* This API call is only used by the native VOL connector, which is
     * not asynchronous.
     */
    info->is_future  = false;
    info->realize_cb = NULL;
    info->discard_cb = NULL;
#ifdef H5_HAVE_CONCURRENCY
    /* Initialize the R/W lock protecting the ID info */
    if (H5TS_dlftt_rwlock_init(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTINIT, FAIL, "can't initialize ID's lock");
    info->lock_init = true;
#endif /* H5_HAVE_CONCURRENCY */

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get type pointer from list of types */
    type_info = H5I_type_info_array_g[type].type_info;
    if (NULL == type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Set up the ID for the object */
    info->gen = type_info->gen;

    /* Insert into the type */
    HASH_ADD(hh, type_info->hash_table, id, sizeof(hid_t), info);
    type_info->id_count++;

    /* Set the most recent ID to this object */
    type_info->last_id_info = info;

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    /* Release the ID info on error */
    if (ret_value < 0)
        if (info)
            H5I__id_info_free(info, false);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_register_using_existing_id() */

/*-------------------------------------------------------------------------
 * Function:    H5I_subst
 *
 * Purpose:     Substitute a new object pointer for the specified ID.
 *
 * Return:      Success:    Non-NULL previous object pointer associated
 *                          with the specified ID.
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_subst(hid_t id, const void *new_object)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID's info */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    void          *ret_value    = NULL;  /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* General lookup of the ID */
    if (H5I__find_id(id, &info, H5I_LOCK_EXCLUSIVE) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't find ID info");
    have_id_lock = true;

    /* Get the old object pointer to return */
    ret_value = info->u.object;

    /* Set the new object pointer for the ID */
    info->u.c_object = new_object;

done:
    /* Release exclusive access for the ID */
    if (have_id_lock && H5I__id_info_wrunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_subst() */

/*-------------------------------------------------------------------------
 * Function:    H5I_object
 *
 * Purpose:     Find an object pointer for the specified ID.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID
 *
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object(hid_t id)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID info */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    void          *ret_value    = NULL;  /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* General lookup of the ID */
    if (H5I__find_id(id, &info, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't find ID info");
    have_id_lock = true;

    /* Get the object pointer to return */
    ret_value = info->u.object;

done:
    /* Release exclusive access for the ID */
    if (have_id_lock && H5I__id_info_rdunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_object() */

/*-------------------------------------------------------------------------
 * Function:    H5I_object_verify
 *
 * Purpose:     Find an object pointer for the specified ID, verifying that
 *              its in a particular type.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID.
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_object_verify(hid_t id, H5I_type_t type)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID info */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    void          *ret_value    = NULL;  /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    assert(type >= 1 && (int)type < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g));

    /* Verify that the type of the ID is correct */
    if (type != H5I_TYPE(id))
        HGOTO_ERROR(H5E_ID, H5E_BADTYPE, NULL, "ID is wrong type");

    /* Look up the ID info */
    if (H5I__find_id(id, &info, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't find ID info");
    have_id_lock = true;

    /* Get the object pointer to return */
    ret_value = info->u.object;

done:
    /* Release shared access for the ID */
    if (have_id_lock && H5I__id_info_rdunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_object_verify() */

/*-------------------------------------------------------------------------
 * Function:    H5I_acquire
 *
 * Purpose:     Find an object pointer for the specified ID, verifying that
 *              it's in a particular type and invoking its 'lock' callback.
 *
 * Return:      Success:    Non-NULL object pointer associated with the
 *                          specified ID.
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_acquire(hid_t id, H5I_type_t type, H5I_lock_mode_t mode)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the type */
    H5I_id_info_t   *info           = NULL;  /* Pointer to the ID info */
    bool             have_id_lock   = false; /* Whether the ID lock is held */
    bool             have_type_lock = false; /* Whether the type lock is held */
    void            *ret_value      = NULL;  /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Sanity checks */
    assert(type >= 1 && (int)type < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g));

    /* Verify that the type of the ID is correct */
    if (type != H5I_TYPE(id))
        HGOTO_ERROR(H5E_ID, H5E_BADTYPE, NULL, "ID is wrong type");

    /* Retrieve the ID info with the type info */
    if (H5I__find_id_with_type(id, &info, H5I_LOCK_SHARED, &type_info, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "can't lookup ID");
    if (type_info)
        have_type_lock = true;
    if (NULL == info)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "ID not found");
    have_id_lock = true;

    /* Check for a 'lock' function and call it, if it exists */
    if (type_info->cls->lock_func) {
        herr_t status = FAIL;

        if (type_info->is_internal)
            status = (type_info->cls->lock_func)(info->u.object, mode);
        else {
            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(NULL)
                {
                    status = (type_info->cls->lock_func)(info->u.object, mode);
                }
            H5_AFTER_USER_CB(NULL)
        }
        if (status < 0)
            HGOTO_ERROR(H5E_ID, H5E_CALLBACK, NULL, "ID lock callback failed");
    }

    /* Get the [now locked] object pointer to return */
    ret_value = info->u.object;

done:
    /* Release exclusive access for the ID */
    if (have_id_lock && H5I__id_info_rdunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID");

    /* Release shared access for the type */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID's type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_acquire() */

/*-------------------------------------------------------------------------
 * Function:    H5I_release
 *
 * Purpose:     Release a lock on an object.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_release(void *obj, H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the ID type */
    bool             have_type_lock = false; /* Whether the type lock is held */
    herr_t           status         = FAIL;
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    assert(type >= 1 && (int)type < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g));

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;

    /* Call 'unlock' callback */
    assert(type_info->cls->unlock_func);
    if (type_info->is_internal)
        status = (type_info->cls->unlock_func)(obj);
    else {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                status = (type_info->cls->unlock_func)(obj);
            }
        H5_AFTER_USER_CB(FAIL)
    }
    if (status < 0)
        HGOTO_ERROR(H5E_ID, H5E_CALLBACK, FAIL, "ID unlock callback failed");

done:
    /* Release shared access for the type */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_acquire() */

/*-------------------------------------------------------------------------
 * Function:    H5I_get_type
 *
 * Purpose:     Given an object ID return the type to which it
 *              belongs.  The ID need not be the ID of an object which
 *              currently exists because the type number is encoded
 *              in the object ID.
 *
 * Return:      Success:    A positive integer (corresponding to an H5I_type_t
 *                          enum value for library ID types, but not for user
 *                          ID types).
 *              Failure:    H5I_BADID
 *
 *-------------------------------------------------------------------------
 */
H5I_type_t
H5I_get_type(hid_t id)
{
    H5I_type_t ret_value = H5I_BADID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_BADID)

    if (id > 0)
        ret_value = H5I_TYPE(id);

    assert(ret_value >= H5I_BADID && (int)ret_value < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_get_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I_is_file_object
 *
 * Purpose:     Convenience function to determine if an ID represents
 *              a file object.
 *
 *              In H5O calls, you can't use object_verify to ensure
 *              the ID was of the correct class since there's no
 *              H5I_OBJECT ID class.
 *
 * Return:      Success:    true/false
 *              Failure:    FAIL
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5I_is_file_object(hid_t id)
{
    H5I_type_t type      = H5I_get_type(id);
    htri_t     ret_value = FAIL;

    FUNC_ENTER_NOAPI(FAIL)

    /* Fail if the ID type is out of range */
    if (type < 1 || type >= H5I_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "ID type out of range");

    /* Return true if the ID is a file object (dataset, group, map, or committed
     * datatype), false otherwise.
     */
    if (H5I_DATASET == type || H5I_GROUP == type || H5I_MAP == type)
        ret_value = true;
    else if (H5I_DATATYPE == type) {
        H5T_t *dt;

        if (NULL == (dt = (H5T_t *)H5I_object(id)))
            HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "unable to get underlying datatype struct");

        ret_value = H5T_is_named(dt);
    }
    else
        ret_value = false;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5I_is_file_object() */

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_verify
 *
 * Purpose:     Removes the specified ID from its type, first checking that
 *              the ID's type is the same as the ID type supplied as an argument
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I__remove_verify(hid_t id, H5I_type_t type)
{
    void *ret_value = NULL; /*return value            */

    FUNC_ENTER_PACKAGE_NOERR

    /* Argument checking will be performed by H5I_remove() */

    /* Verify that the type of the ID is correct */
    if (type == H5I_TYPE(id))
        ret_value = H5I_remove(id);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__remove_verify() */

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_id_info
 *
 * Purpose:     Common code to remove a specified ID from its type.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__remove_id_info(H5I_type_info_t *type_info, H5I_id_info_t *info, void **request, bool make_cb, bool force,
                    bool try, bool id_locked)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type_info);
    assert(info);

    /* Check if this ID was the last one accessed */
    if (type_info->last_id_info == info)
        type_info->last_id_info = NULL;

    /* Check if we want to make any callbacks */
    if (make_cb) {
        herr_t status; /* Status from callback */

        /* Check if this is an un-realized future object */
        if (info->is_future) {
            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(FAIL)
                {
                    /* Discard the future object */
                    status = (info->discard_cb)(info->u.object);
                }
            H5_AFTER_USER_CB(FAIL)
            if (status < 0)
                if (!force) {
                    /* Leave without pushing error when only trying */
                    if (try)
                        HGOTO_DONE(FAIL);
                    else
                        HGOTO_ERROR(H5E_ID, H5E_CALLBACK, FAIL, "ID free callback failed");
                }
        }
        else {
            /* Check for a 'free' function and call it, if it exists */
            if (type_info->cls->free_func) {
                if (type_info->is_internal)
                    status = (type_info->cls->free_func)(info->u.object, request);
                else {
                    /* Prepare & restore library for user callback */
                    H5_BEFORE_USER_CB(FAIL)
                        {
                            status = (type_info->cls->free_func)(info->u.object, request);
                        }
                    H5_AFTER_USER_CB(FAIL)
                }
                if (status < 0)
                    if (!force) {
                        /* Leave without pushing error when only trying */
                        if (try)
                            HGOTO_DONE(FAIL);
                        else
                            HGOTO_ERROR(H5E_ID, H5E_CALLBACK, FAIL, "ID free callback failed");
                    }
            }
        }
    }

    /* Remove ID from hash table */
    HASH_DELETE(hh, type_info->hash_table, info);

    /* Decrement the number of IDs in the type */
    type_info->id_count--;
    if (info->is_future)
        type_info->num_fut_ids--;

    /* Delete ID info */
    H5I__id_info_free(info, id_locked);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__remove_id_info() */

/*-------------------------------------------------------------------------
 * Function:    H5I__remove_common
 *
 * Purpose:     Common code to remove a specified ID from its type.
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static void *
H5I__remove_common(H5I_type_info_t *type_info, H5I_id_info_t *info, void **request, bool make_cb)
{
    void *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type_info);
    assert(info);

    /* Save pointer to ID's object */
    ret_value = info->u.object;

    /* Delete the node if we're not iterating the type, or if we've already
     * visited the node when iterating
     */
    if (0 == type_info->iterating || info->gen >= type_info->gen) {
        /* Remove ID from hash table */
        if (H5I__remove_id_info(type_info, info, request, make_cb, false, false, true) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node from hash table");
    }
    else {
        info->del_later     = true;
        info->make_cb_later = make_cb;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__remove_common() */

/*-------------------------------------------------------------------------
 * Function:    H5I_remove
 *
 * Purpose:     Removes the specified ID from its type.
 *
 * Return:      Success:    A pointer to the object that was removed, the
 *                          same pointer which would have been found by
 *                          calling H5I_object().
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
void *
H5I_remove(hid_t id)
{
    H5I_type_info_t *type_info      = NULL;      /* Pointer to the ID type */
    H5I_type_t       type           = H5I_BADID; /* ID's type */
    H5I_id_info_t   *id_info        = NULL;      /* ID's info */
    bool             have_type_lock = false;     /* Whether the type lock is held */
    bool             have_id_lock   = false;     /* Whether the ID lock is held */
    void            *ret_value      = NULL;      /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, NULL, "invalid type number");

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, NULL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, NULL, "invalid type");

    /* Look up the ID in the type info */
    if (H5I__lookup_id(type_info, id, &id_info, H5I_LOCK_EXCLUSIVE) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTGET, NULL, "can't lookup ID");
    if (NULL == id_info)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, NULL, "ID info not found");
    have_id_lock = true;

    /* Remove the node from the type */
    if (NULL == (ret_value = H5I__remove_common(type_info, id_info, H5_REQUEST_NULL, false)))
        HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, NULL, "can't remove ID node");
    have_id_lock = false; /* Deleting the ID will unlock & destroy its mutex */

done:
    /* Release exclusive access for the ID, if still held */
    if (have_id_lock && H5I__id_info_wrunlock(id_info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on ID");

    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, NULL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_remove() */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_ref
 *
 * Purpose:     This will fail if the type is not a reference counted type.
 *              The ID type's 'free' function will be called for the ID
 *              if the reference count for the ID reaches 0 and a free
 *              function has been defined at type creation time.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_ref(hid_t id, void **request)
{
    H5I_id_info_t *info           = NULL;  /* Pointer to the ID */
    bool           have_id_lock   = false; /* Whether the ID lock is held */
    bool           have_type_lock = false; /* Whether the type lock is held */
    int            ret_value      = 0;     /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (H5I__find_id(id, &info, H5I_LOCK_EXCLUSIVE) < 0)
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
    have_id_lock = true;

    /* If this is the last reference to the object then invoke the type's
     * free method on the object. If the free method is undefined or
     * successful then remove the object from the type; otherwise leave
     * the object in the type without decrementing the reference
     * count. If the reference count is more than one then decrement the
     * reference count without calling the free method.
     *
     * Beware: the free method may call other H5I functions.
     *
     * If an object is closing, we can remove the ID even though the free
     * method might fail.  This can happen when a mandatory filter fails to
     * write when a dataset is closed and the chunk cache is flushed to the
     * file.  We have to close the dataset anyway. (SLU - 2010/9/7)
     */
    if (1 == info->count) {
        H5I_type_info_t *type_info; /*ptr to the type    */

        /* Acquire exclusive access for the type */
        if (H5I__type_info_wrlock(H5I_TYPE(id)) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, (-1), "can't acquire lock on type");
        have_type_lock = true;

        /* Get the ID's type */
        type_info = H5I_type_info_array_g[H5I_TYPE(id)].type_info;

        /* Try removing the node from the type */
        if (NULL == H5I__remove_common(type_info, info, request, true))
            HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, (-1), "can't remove ID node");
        have_id_lock = false; /* Deleting the ID will unlock & destroy its mutex */
    }                         /* end if */
    else {
        --(info->count);
        ret_value = (int)info->count;
    } /* end else */

done:
    /* Release exclusive access for the ID, if still held */
    if (have_id_lock && H5I__id_info_wrunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on ID");

    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(H5I_TYPE(id)) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_ref */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_ref
 *
 * Purpose:     Decrements the number of references outstanding for an ID.
 *
 * Return:      Success:    New reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_ref(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_ref(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_app_ref(hid_t id, void **request)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID info */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    int            ret_value    = 0;     /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(id >= 0);

    /* Call regular decrement reference count routine */
    if ((ret_value = H5I__dec_ref(id, request)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

    /* Check if the ID still exists */
    if (ret_value > 0) {
        /* General lookup of the ID */
        if (H5I__find_id(id, &info, H5I_LOCK_EXCLUSIVE) < 0)
            HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
        have_id_lock = true;

        /* Adjust app_ref */
        --(info->app_count);
        assert(info->count >= info->app_count);

        /* Set return value */
        ret_value = (int)info->app_count;
    } /* end if */

done:
    /* Release exclusive access for the ID, if still held */
    if (have_id_lock && H5I__id_info_wrunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_app_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref
 *
 * Purpose:     Wrapper for case of modifying the application ref. count for
 *              an ID as well as normal reference count.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_async
 *
 * Purpose:     Asynchronous wrapper for case of modifying the application ref.
 *              count for an ID as well as normal reference count.
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] asynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_async() */

/*-------------------------------------------------------------------------
 * Function:    H5I__dec_app_ref_always_close
 *
 * Purpose:     Wrapper for case of always closing the ID, even when the free
 *              routine fails
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              request != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__dec_app_ref_always_close(hid_t id, void **request)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(id >= 0);

    /* Call application decrement reference count routine */
    ret_value = H5I__dec_app_ref(id, request);

    /* Check for failure */
    if (ret_value < 0) {
        /*
         * If an object is closing, we can remove the ID even though the free
         * method might fail.  This can happen when a mandatory filter fails to
         * write when a dataset is closed and the chunk cache is flushed to the
         * file.  We have to close the dataset anyway. (SLU - 2010/9/7)
         */
        H5I_remove(id);

        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__dec_app_ref_always_close() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close
 *
 * Purpose:     Wrapper for case of always closing the ID, even when the free
 *              routine fails.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close(hid_t id)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* Synchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, H5_REQUEST_NULL)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_always_close() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_app_ref_always_close_async
 *
 * Purpose:     Asynchronous wrapper for case of always closing the ID, even
 *              when the free routine fails
 *
 * Note:        Allows for asynchronous 'close' operation on object, with
 *              token != H5_REQUEST_NULL.
 *
 * Return:      Success:    New app. reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_app_ref_always_close_async(hid_t id, void **token)
{
    int ret_value = 0; /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* [Possibly] asynchronously decrement refcount on ID */
    if ((ret_value = H5I__dec_app_ref_always_close(id, token)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTDEC, (-1), "can't asynchronously decrement ID ref count");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_app_ref_always_close_async() */

/*-------------------------------------------------------------------------
 * Function:    H5I_inc_ref
 *
 * Purpose:     Increment the reference count for an object.
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_inc_ref(hid_t id, bool app_ref)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID info */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    int            ret_value    = 0;     /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (H5I__find_id(id, &info, H5I_LOCK_EXCLUSIVE) < 0)
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
    have_id_lock = true;

    /* Adjust reference counts */
    ++(info->count);
    if (app_ref)
        ++(info->app_count);

    /* Set return value */
    ret_value = (int)(app_ref ? info->app_count : info->count);

done:
    /* Release exclusive access for the ID, if still held */
    if (have_id_lock && H5I__id_info_wrunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_inc_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I_get_ref
 *
 * Purpose:     Retrieve the reference count for an object.
 *
 * Return:      Success:    The reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_get_ref(hid_t id, bool app_ref)
{
    H5I_id_info_t *info         = NULL;  /* Pointer to the ID */
    bool           have_id_lock = false; /* Whether the ID lock is held */
    int            ret_value    = 0;     /* Return value */

    FUNC_ENTER_NOAPI((-1))

    /* Sanity check */
    assert(id >= 0);

    /* General lookup of the ID */
    if (H5I__find_id(id, &info, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_BADID, (-1), "can't locate ID");
    have_id_lock = true;

    /* Set return value */
    ret_value = (int)(app_ref ? info->app_count : info->count);

done:
    /* Release shared access for the ID, if still held */
    if (have_id_lock && H5I__id_info_rdunlock(info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on ID");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_get_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I__inc_type_ref
 *
 * Purpose:     Increment the reference count for an ID type.
 *
 * Return:      Success:    The new reference count
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__inc_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the type */
    bool             have_type_lock = false; /* Whether the lock is held */
    int              ret_value      = -1;    /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type > 0 && (int)type < H5TS_ATOMIC_LOAD(int, &H5I_next_type_g));

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, (-1), "can't acquire lock on type");
    have_type_lock = true;

    /* Check arguments */
    type_info = H5I_type_info_array_g[type].type_info;
    if (NULL == type_info)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value */
    ret_value = (int)(++(type_info->init_count));

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__inc_type_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I_dec_type_ref
 *
 * Purpose:     Decrements the reference count on an entire type of IDs.
 *              If the type reference count becomes zero then the type is
 *              destroyed along with all IDs in that type regardless of
 *              their reference counts. Destroying IDs involves calling
 *              the free-func for each ID's object and then adding the ID
 *              struct to the ID free list.
 *              Returns the number of references to the type on success; a
 *              return value of 0 means that the type will have to be
 *              re-initialized before it can be used again (and should probably
 *              be set to H5I_UNINIT).
 *
 * Return:      Success:    Number of references to type
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I_dec_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the ID type */
    bool             have_type_lock = false; /* Whether the lock is held */
    herr_t           ret_value      = 0;     /* Return value */

    FUNC_ENTER_NOAPI((-1))

    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, (-1), "invalid type number");

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, (-1), "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (type_info == NULL || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Decrement the number of users of the ID type.  If this is the
     * last user of the type then release all IDs from the type and
     * free all memory it used.  The free function is invoked for each ID
     * being freed.
     */
    if (1 == type_info->init_count) {
        H5I__destroy_type_info(type, type_info);
        ret_value = 0;
    }
    else {
        --(type_info->init_count);
        ret_value = (herr_t)type_info->init_count;
    }

done:
    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_dec_type_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I__get_type_ref
 *
 * Purpose:     Retrieve the reference count for an ID type.
 *
 * Return:      Success:    The reference count
 *
 *              Failure:    -1
 *
 *-------------------------------------------------------------------------
 */
int
H5I__get_type_ref(H5I_type_t type)
{
    H5I_type_info_t *type_info      = NULL;  /* Pointer to the type  */
    bool             have_type_lock = false; /* Whether the lock is held */
    int              ret_value      = -1;    /* Return value         */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type >= 0);

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, (-1), "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (!type_info)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, (-1), "invalid type");

    /* Set return value */
    ret_value = (int)type_info->init_count;

done:
    /* Release shared access for the type */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, (-1), "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__get_type_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5I__iterate_cb
 *
 * Purpose:     Callback routine for H5I_iterate, invokes "user" callback
 *              function, and then sets return value, based on the result of
 *              that callback.
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__iterate_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_id_info_t    *info      = (H5I_id_info_t *)_item;     /* Pointer to the ID info */
    H5I_iterate_ud_t *udata     = (H5I_iterate_ud_t *)_udata; /* User data for callback */
    int               ret_value = H5_ITER_CONT;               /* Callback return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Only invoke the callback function if this ID is visible externally and
     * its reference count is positive.
     */
    if (!udata->app_ref || info->app_count > 0) {
        void  *object;
        herr_t cb_ret_val = FAIL;

        /* The stored object pointer might be an H5VL_object_t, in which
         * case we'll need to get the wrapped object struct (H5F_t *, etc.).
         */
        object = H5I__unwrap(info->u.object, udata->obj_type);

        /* Invoke callback function */
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB_NOERR(H5_ITER_ERROR)
            {
                cb_ret_val = (*udata->user_func)((void *)object, info->id, udata->user_udata);
            }
        H5_AFTER_USER_CB_NOERR(H5_ITER_ERROR)

        /* Set the return value based on the callback's return value */
        if (cb_ret_val > 0)
            ret_value = H5_ITER_STOP; /* terminate iteration early */
        else if (cb_ret_val < 0)
            ret_value = H5_ITER_ERROR; /* indicate failure (which terminates iteration) */
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__iterate_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5I_iterate
 *
 * Purpose:     Apply function FUNC to each member of type TYPE (with
 *              non-zero application reference count if app_ref is true).
 *              Stop if FUNC returns a non zero value (i.e. anything
 *              other than H5_ITER_CONT).
 *
 *              If FUNC returns a positive value (i.e. H5_ITER_STOP),
 *              return SUCCEED.
 *
 *              If FUNC returns a negative value (i.e. H5_ITER_ERROR),
 *              return FAIL.
 *
 *              The FUNC should take a pointer to the object and the
 *              udata as arguments and return non-zero to terminate
 *              siteration, and zero to continue.
 *
 * Limitation:  Currently there is no way to start the iteration from
 *              where a previous iteration left off.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_iterate(H5I_type_t type, H5I_search_func_t func, void *udata, bool app_ref)
{
    H5I_type_info_t *type_info      = NULL; /* Pointer to the type */
    H5I_id_info_t   *item           = NULL;
    bool             have_id_lock   = false;   /* Whether the ID lock is held */
    bool             have_type_lock = false;   /* Whether the lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check arguments */
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ARGS, H5E_BADRANGE, FAIL, "invalid type number");

    /* Acquire exclusive access for the type */
    if (H5I__type_info_wrlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if (type_info && type_info->init_count > 0 && type_info->id_count > 0) {
        H5I_iterate_ud_t iter_udata; /* User data for iteration callback */
        H5I_id_info_t   *tmp = NULL;

        /* Increment the generation of the type */
        type_info->gen++;

        /* Indicate that we're iterating this type right now */
        type_info->iterating++;

        /* Set up iterator user data */
        iter_udata.user_func  = func;
        iter_udata.user_udata = udata;
        iter_udata.app_ref    = app_ref;
        iter_udata.obj_type   = type;

        /* Iterate over IDs */
        HASH_ITER(hh, type_info->hash_table, item, tmp)
        {
            /* Check if this ID node was deleted, through an iteration callback */
            if (item->del_later) {
                /* Remove ID from hash table */
                if (H5I__remove_id_info(type_info, item, H5_REQUEST_NULL, item->make_cb_later, true, false,
                                        false) < 0) {
                    type_info->iterating--;
                    HGOTO_ERROR(H5E_ID, H5E_CANTDELETE, FAIL, "can't remove ID node from hash table");
                }
            }
            else {
                int ret;

                /* Acquire exclusive access to the ID */
                if (H5I__id_info_wrlock(item) < 0) {
                    type_info->iterating--;
                    HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on ID");
                }
                have_id_lock = true;

                ret = H5I__iterate_cb((void *)item, NULL, (void *)&iter_udata);
                if (H5_ITER_ERROR == ret) {
                    type_info->iterating--;
                    HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");
                }
                if (H5_ITER_STOP == ret)
                    break;

                /* Move item to new type generation */
                item->gen = type_info->gen;

                /* Release exclusive access for the ID */
                if (H5I__id_info_wrunlock(item) < 0) {
                    type_info->iterating--;
                    HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on ID");
                }
                have_id_lock = false;
            }
        }

        /* Indicate that we're done iterating this type */
        type_info->iterating--;
    }

done:
    /* Release exclusive access for the ID */
    if (have_id_lock && H5I__id_info_wrunlock(item) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on ID");

    /* Release exclusive access for the type */
    if (have_type_lock && H5I__type_info_wrunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_iterate() */

/*-------------------------------------------------------------------------
 * Function:    H5I__lookup_id
 *
 * Purpose:     Find the ID info for an object ID within a type info object
 *
 * Note:        It's not an error to not find the ID
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__lookup_id(H5I_type_info_t *type_info, hid_t id, H5I_id_info_t **out_id_info, H5I_lock_mode_t mode)
{
    H5I_id_info_t *id_info   = NULL;    /* ID info to pass out */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(type_info);
    assert(out_id_info);

    /* Check for same ID as we have looked up last time */
    if (type_info->last_id_info && type_info->last_id_info->id == id)
        id_info = type_info->last_id_info;
    else {
        HASH_FIND(hh, type_info->hash_table, &id, sizeof(hid_t), id_info);

        /* Remember this ID, if found */
        if (id_info)
            type_info->last_id_info = id_info;
    }

    /* Acquire access to the ID */
    if (id_info) {
        if (H5I_LOCK_EXCLUSIVE == mode) {
            if (H5I__id_info_wrlock(id_info) < 0)
                HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire exclusive lock on ID");
        }
        else {
            assert(H5I_LOCK_SHARED == mode);
            if (H5I__id_info_rdlock(id_info) < 0)
                HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire shared lock on ID");
        }
    }

    /* Set OUT parameter */
    *out_id_info = id_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__lookup_id() */

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id_with_type
 *
 * Purpose:     Given an object ID find the info struct that describes the
 *              object, possibly returning the type info struct also.
 *
 * Note:        Both the ID info and the type info are returned in OUT params
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__find_id_with_type(hid_t id, H5I_id_info_t **out_id_info, H5I_lock_mode_t id_lock_mode,
                       H5I_type_info_t **out_type_info, H5I_lock_mode_t type_lock_mode)
{
    H5I_type_t       type;                         /* ID's type */
    H5I_type_info_t *type_info          = NULL;    /* Pointer to the type */
    H5I_id_info_t   *id_info            = NULL;    /* ID's info */
    bool             have_id_lock       = false;   /* Whether the ID lock is held */
    bool             have_type_lock     = false;   /* Whether the type lock is held */
    bool             possible_future_id = false;   /* Whether it's possible that the ID is a future */
    bool             type_lock_is_excl  = false;   /* Whether the type lock is an exclusive lock */
    bool             id_lock_is_excl    = false;   /* Whether the ID lock is an exclusive lock */
    herr_t           ret_value          = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(out_id_info);

    /* Reject returning an exclusive type lock for now */
    /* (It's possible, just complex and not needed yet) */
    if (out_type_info && H5I_LOCK_EXCLUSIVE == type_lock_mode) {
        assert(0 && "returning exclusively locked type info not currently supported");
        HGOTO_ERROR(H5E_ID, H5E_UNSUPPORTED, FAIL,
                    "returning exclusively locked type info not currently supported");
    }

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g))
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (!type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Check for any IDs being futures, which will mean that the type info
     * might need to be modified
     */
    /* Note: this is a variation on the double-checked locking pattern (DCLP).
     *
     * For background on DCLP:
     *  https://preshing.com/20130930/double-checked-locking-is-fixed-in-cpp11/
     */
    if (type_info->num_fut_ids > 0) {
        /* Release shared access for the type */
        if (H5I__type_info_rdunlock(type) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on ID's type");
        have_type_lock = false;

        /* Acquire exclusive access for the type */
        if (H5I__type_info_wrlock(type) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
        have_type_lock    = true;
        type_lock_is_excl = true;

        /* Get the pointer to the type info */
        type_info = H5I_type_info_array_g[type].type_info;
        if (!type_info || type_info->init_count <= 0)
            HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

        /* Check again for any IDs in this type being futures */
        /* (which could have gone to zero between dropping the shared lock
         *  and acquiring the exclusive lock)
         */
        if (0 == type_info->num_fut_ids) {
            /* Downgrade, but don't release the lock */
            if (H5I__type_info_wrlock_downgrade(type) < 0)
                HGOTO_ERROR(H5E_ID, H5E_CANTMODIFY, FAIL, "can't downgrade type info lock");
            type_lock_is_excl = false;
        }
        else
            possible_future_id = true;
    }

    /* Look up the ID in the type info */
    if (H5I__lookup_id(type_info, id, &id_info, (possible_future_id ? H5I_LOCK_EXCLUSIVE : id_lock_mode)) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "can't lookup ID");
    if (NULL == id_info)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "ID not found");
    have_id_lock = true;
    if (possible_future_id || H5I_LOCK_EXCLUSIVE == id_lock_mode)
        id_lock_is_excl = true;

    /* Check if this is a future ID */
    if (id_info->is_future) {
        hid_t          actual_id      = H5I_INVALID_HID; /* ID for actual object */
        H5I_id_info_t *actual_id_info = NULL;            /* Actual ID's info */
        void          *future_object;                    /* Pointer to the future object */
        void          *actual_object;                    /* Pointer to the actual object */
        herr_t         status = FAIL;

        /* Sanity checks */
        assert(type_lock_is_excl);
        assert(type_info->num_fut_ids > 0);

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                /* Invoke the realize callback, to get the actual object */
                status = (id_info->realize_cb)(id_info->u.object, &actual_id);
            }
        H5_AFTER_USER_CB(FAIL)
        if (status < 0)
            HGOTO_ERROR(H5E_ID, H5E_CALLBACK, FAIL, "future IDs 'realize' callback failed");

        /* Verify that we received a valid ID, of the same type */
        if (H5I_INVALID_HID == actual_id)
            HGOTO_ERROR(H5E_ID, H5E_BADVALUE, FAIL, "didn't receive actual ID from callback");
        if (H5I_TYPE(id) != H5I_TYPE(actual_id))
            HGOTO_ERROR(H5E_ID, H5E_BADTYPE, FAIL, "actual ID not same type as future ID");

        /* Look up the actual ID in the type info */
        if (H5I__lookup_id(type_info, actual_id, &actual_id_info, H5I_LOCK_EXCLUSIVE) < 0)
            HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "can't lookup actual ID");
        if (NULL == actual_id_info)
            HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "actual ID not found");

        /* Swap the actual object in for the future object */
        future_object = id_info->u.object;
        if (NULL == (actual_object = H5I__remove_common(type_info, actual_id_info, H5_REQUEST_NULL, false))) {
            H5I__id_info_wrunlock(actual_id_info);
            HGOTO_ERROR(H5E_ID, H5E_CANTREMOVE, FAIL, "can't remove actual ID");
        }
        id_info->u.object = actual_object;

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                /* Discard the future object */
                status = (id_info->discard_cb)(future_object);
            }
        H5_AFTER_USER_CB(FAIL)
        if (status < 0)
            HGOTO_ERROR(H5E_ID, H5E_CALLBACK, FAIL, "future IDs 'discard' callback failed");
        future_object = NULL;

        /* Change the ID from 'future' to 'actual' */
        id_info->is_future  = false;
        id_info->realize_cb = NULL;
        id_info->discard_cb = NULL;

        /* Decrement # of future IDs for type */
        type_info->num_fut_ids--;
    }

    /* Downgrade the ID lock if the caller requested a shared lock, but we
     * used an exclusive lock to cover the possibility that the ID was a future
     */
    if (possible_future_id && H5I_LOCK_SHARED == id_lock_mode) {
        /* Downgrade, but don't release the ID lock */
        if (H5I__id_info_wrunlock_downgrade(id_info) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTMODIFY, FAIL, "can't downgrade ID info lock");
        id_lock_is_excl = false;
    }

    /* Set OUT parameters */
    if (out_type_info) {
        /* Sanity check */
        assert(H5I_LOCK_SHARED == type_lock_mode);
        assert(have_type_lock);

        /* Downgrade lock if necessary */
        if (type_lock_is_excl) {
            /* Downgrade, but don't release the lock */
            if (H5I__type_info_wrlock_downgrade(type) < 0)
                HGOTO_ERROR(H5E_ID, H5E_CANTMODIFY, FAIL, "can't downgrade type info lock");
            type_lock_is_excl = false;
        }

        /* Transfer ownership, so the type lock is not released */
        *out_type_info = type_info;
        have_type_lock = false;
    }
    *out_id_info = id_info;
    have_id_lock = false;

done:
    /* Release ID lock, on error */
    if (ret_value < 0 && have_id_lock) {
        if (id_lock_is_excl)
            H5I__id_info_wrunlock(id_info);
        else {
            assert(H5I_LOCK_SHARED == id_lock_mode);
            H5I__id_info_rdunlock(id_info);
        }
    }
    /* Release type lock, if ownership hasn't been transferred */
    if (have_type_lock) {
        if (type_lock_is_excl)
            H5I__type_info_wrunlock(type);
        else
            H5I__type_info_rdunlock(type);
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__find_id_with_type() */

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id
 *
 * Purpose:     Given an object ID find the info struct that describes the
 *              object.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__find_id(hid_t id, H5I_id_info_t **id_info, H5I_lock_mode_t id_lock_mode)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Retrieve the ID info with the type info */
    if (H5I__find_id_with_type(id, id_info, id_lock_mode, NULL, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "can't find ID info");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__find_id() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_exists
 *
 * Purpose:     Check if an ID exists
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_exists(hid_t id, bool *exists)
{
    H5I_type_t       type;                     /* ID's type */
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the type */
    bool             have_type_lock = false;   /* Whether the type lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g)) {
        *exists = false;
        HGOTO_DONE(SUCCEED);
    }

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_DONE(FAIL);
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (!type_info || type_info->init_count <= 0) {
        *exists = false;
        HGOTO_DONE(SUCCEED);
    }

    /* Check for ID */
    if (type_info->last_id_info && type_info->last_id_info->id == id)
        *exists = true;
    else {
        H5I_id_info_t *info; /* ID's info */

        HASH_FIND(hh, type_info->hash_table, &id, sizeof(hid_t), info);

        *exists = (NULL != info);
    }

done:
    /* Release shared access for the type */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        ret_value = FAIL;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_exists() */

/*-------------------------------------------------------------------------
 * Function:    H5I__find_id_cb
 *
 * Purpose:     Callback for searching for an ID with a specific pointer
 *
 * Return:      Success:    H5_ITER_CONT (0) or H5_ITER_STOP (1)
 *              Failure:    H5_ITER_ERROR (-1)
 *
 *-------------------------------------------------------------------------
 */
static int
H5I__find_id_cb(void *_item, void H5_ATTR_UNUSED *_key, void *_udata)
{
    H5I_id_info_t   *info      = (H5I_id_info_t *)_item;    /* Pointer to the ID info */
    H5I_get_id_ud_t *udata     = (H5I_get_id_ud_t *)_udata; /* Pointer to user data */
    H5I_type_t       type      = udata->obj_type;
    const void      *object    = NULL;
    int              ret_value = H5_ITER_CONT; /* Return value */

    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(info);
    assert(udata);

    /* Get a pointer to the VOL connector's data */
    object = H5I__unwrap(info->u.object, type);

    /* Check for a match */
    if (object == udata->object) {
        udata->ret_id = info->id;
        ret_value     = H5_ITER_STOP;
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__find_id_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5I_find_id
 *
 * Purpose:     Return the ID of an object by searching through the ID list
 *              for the type.
 *
 * Return:      SUCCEED/FAIL
 *              (id will be set to H5I_INVALID_HID on errors or not found)
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I_find_id(const void *object, H5I_type_t type, hid_t *id)
{
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the type */
    bool             have_type_lock = false;   /* Whether the lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    assert(id);

    /* Reset out parameter */
    *id = H5I_INVALID_HID;

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (!type_info || type_info->init_count <= 0)
        HGOTO_ERROR(H5E_ID, H5E_BADGROUP, FAIL, "invalid type");

    /* Only iterate through ID list if it is initialized and there are IDs in type */
    if (type_info->init_count > 0 && type_info->id_count > 0) {
        H5I_get_id_ud_t udata; /* User data */
        H5I_id_info_t  *item = NULL;
        H5I_id_info_t  *tmp  = NULL;

        /* Set up iterator user data */
        udata.object   = object;
        udata.obj_type = type;
        udata.ret_id   = H5I_INVALID_HID;

        /* Iterate over IDs for the ID type */
        HASH_ITER(hh, type_info->hash_table, item, tmp)
        {
            int ret = H5I__find_id_cb((void *)item, NULL, (void *)&udata);
            if (H5_ITER_ERROR == ret)
                HGOTO_ERROR(H5E_ID, H5E_BADITER, FAIL, "iteration failed");
            if (H5_ITER_STOP == ret)
                break;
        }

        *id = udata.ret_id;
    }

done:
    /* Release shared access for the type */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on type");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I_find_id() */

/*-------------------------------------------------------------------------
 * Function:    H5I__is_id_valid
 *
 * Purpose:     Check if the given id is valid.  An id is valid if it is in
 *              use and has an application reference count of at least 1.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__is_id_valid(hid_t id, bool *is_valid)
{
    H5I_type_t       type;                     /* ID's type */
    H5I_type_info_t *type_info      = NULL;    /* Pointer to the type */
    H5I_id_info_t   *id_info        = NULL;    /* ID's info */
    bool             have_type_lock = false;   /* Whether the type lock is held */
    herr_t           ret_value      = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    type = H5I_TYPE(id);
    if (type <= H5I_BADID || (int)type >= H5TS_ATOMIC_LOAD(int, &H5I_next_type_g)) {
        *is_valid = false;
        HGOTO_DONE(SUCCEED);
    }

    /* Acquire shared access for the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't acquire lock on type");
    have_type_lock = true;

    /* Get the pointer to the type info */
    type_info = H5I_type_info_array_g[type].type_info;
    if (!type_info || type_info->init_count <= 0) {
        *is_valid = false;
        HGOTO_DONE(SUCCEED);
    }

    /* Look up the ID in the type info */
    if (H5I__lookup_id(type_info, id, &id_info, H5I_LOCK_SHARED) < 0)
        HGOTO_ERROR(H5E_ID, H5E_NOTFOUND, FAIL, "can't lookup ID");

    /* Check the ID */
    if (NULL == id_info)
        *is_valid = false;
    else if (!id_info->app_count) /* Check if the found id is an internal id */
        *is_valid = false;
    else
        *is_valid = true;

done:
    /* Release shared access for the ID, if still held */
    if (id_info && H5I__id_info_rdunlock(id_info) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't release lock on ID");

    /* Release the type lock if held */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5Iis_valid() */

/*-------------------------------------------------------------------------
 * Function:    H5I__is_type_valid
 *
 * Purpose:     Checks if the type is (currently) valid.
 *
 * Return:      TRUE/FALSE/FAIL
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5I__is_type_valid(H5I_type_t type)
{
#ifdef H5_HAVE_CONCURRENCY
    bool have_type_lock = false; /* Whether the lock is held */
#endif                           /* H5_HAVE_CONCURRENCY */
    htri_t ret_value = true;     /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Acquire the lock protecting the type */
    if (H5I__type_info_rdlock(type) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");
    have_type_lock = true;
#endif /* H5_HAVE_CONCURRENCY */

    /* Check for valid type */
    if (NULL == H5I_type_info_array_g[type].type_info)
        HGOTO_DONE(false);

done:
#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock if held */
    if (have_type_lock && H5I__type_info_rdunlock(type) < 0)
        HDONE_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");
#endif /* H5_HAVE_CONCURRENCY */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__is_type_valid() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_wrlock
 *
 * Purpose:     Acquire exclusive access to a type info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__type_info_wrlock(H5I_type_t
#ifndef H5_HAVE_CONCURRENCY
                          H5_ATTR_UNUSED
#endif /* NDEBUG */
                              type)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Acquire the lock protecting the global type info */
    assert(H5I_type_info_array_g[type].lock_init);
    if (H5TS_dlftt_rwlock_wrlock(&H5I_type_info_array_g[type].lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__type_info_wrlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_rdlock
 *
 * Purpose:     Acquire shared access to a type info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__type_info_rdlock(H5I_type_t
#ifndef H5_HAVE_CONCURRENCY
                          H5_ATTR_UNUSED
#endif /* NDEBUG */
                              type)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Acquire the lock protecting the global type info */
    assert(H5I_type_info_array_g[type].lock_init);
    if (H5TS_dlftt_rwlock_rdlock(&H5I_type_info_array_g[type].lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock type info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__type_info_rdlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_wrlock_downgrade
 *
 * Purpose:     Downgrade a write lock to read lock without releasing it
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__type_info_wrlock_downgrade(H5I_type_t
#ifndef H5_HAVE_CONCURRENCY
                                    H5_ATTR_UNUSED
#endif /* NDEBUG */
                                        type)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Downgrade the lock protecting the global type info */
    assert(H5I_type_info_array_g[type].lock_init);
    if (H5TS_dlftt_rwlock_wrlock_downgrade(&H5I_type_info_array_g[type].lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTMODIFY, FAIL, "can't downgrade lock");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__type_info_wrlock_downgrade() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_wrunlock
 *
 * Purpose:     Release exclusive access to a type info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__type_info_wrunlock(H5I_type_t
#ifndef H5_HAVE_CONCURRENCY
                            H5_ATTR_UNUSED
#endif /* NDEBUG */
                                type)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock protecting the type info */
    assert(H5I_type_info_array_g[type].lock_init);
    if (H5TS_dlftt_rwlock_wrunlock(&H5I_type_info_array_g[type].lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__type_info_wrunlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_rdunlock
 *
 * Purpose:     Release shared access to a type info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5I__type_info_rdunlock(H5I_type_t
#ifndef H5_HAVE_CONCURRENCY
                            H5_ATTR_UNUSED
#endif /* NDEBUG */
                                type)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock protecting the type info */
    assert(H5I_type_info_array_g[type].lock_init);
    if (H5TS_dlftt_rwlock_rdunlock(&H5I_type_info_array_g[type].lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock type info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__type_info_rdunlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__type_info_free
 *
 * Purpose:     Release a type info object
 *
 * Note:        Assumes that the type_info object is not in the global type
 *              info array, and therefore that no locking is necessary.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__type_info_free(H5I_type_info_t *type_info)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Check if we should release the ID class */
    if (type_info->cls->flags & H5I_CLASS_IS_APPLICATION)
        type_info->cls = H5MM_xfree_const(type_info->cls);

    HASH_CLEAR(hh, type_info->hash_table);
    type_info->hash_table = NULL;

    /* Release the object */
    H5MM_free(type_info);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5I__type_info_free() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_wrlock
 *
 * Purpose:     Acquire exclusive access to an ID info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_wrlock(H5I_id_info_t
#ifndef H5_HAVE_CONCURRENCY
                        H5_ATTR_UNUSED
#endif /* NDEBUG */
                            *info)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Acquire the lock protecting the ID info */
    assert(info->lock_init);
    if (H5TS_dlftt_rwlock_wrlock(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock ID info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_wrlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_rdlock
 *
 * Purpose:     Acquire shared access to an ID info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_rdlock(H5I_id_info_t
#ifndef H5_HAVE_CONCURRENCY
                        H5_ATTR_UNUSED
#endif /* NDEBUG */
                            *info)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Acquire the lock protecting the ID info */
    assert(info->lock_init);
    if (H5TS_dlftt_rwlock_rdlock(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTLOCK, FAIL, "can't lock ID info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_rdlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_wrunlock_downgrade
 *
 * Purpose:     Downgrade a write lock to read lock without releasing it
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_wrunlock_downgrade(H5I_id_info_t
#ifndef H5_HAVE_CONCURRENCY
                                    H5_ATTR_UNUSED
#endif /* NDEBUG */
                                        *info)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock protecting the ID info */
    assert(info->lock_init);
    if (H5TS_dlftt_rwlock_wrlock_downgrade(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTMODIFY, FAIL, "can't downgrade lock");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_wrlock_downgrade() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_wrunlock
 *
 * Purpose:     Release exclusive access to an ID info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_wrunlock(H5I_id_info_t
#ifndef H5_HAVE_CONCURRENCY
                          H5_ATTR_UNUSED
#endif /* NDEBUG */
                              *info)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock protecting the ID info */
    assert(info->lock_init);
    if (H5TS_dlftt_rwlock_wrunlock(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock ID info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_wrunlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_rdunlock
 *
 * Purpose:     Release shared access to an ID info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_rdunlock(H5I_id_info_t
#ifndef H5_HAVE_CONCURRENCY
                          H5_ATTR_UNUSED
#endif /* NDEBUG */
                              *info)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

#ifdef H5_HAVE_CONCURRENCY
    /* Release the lock protecting the ID info */
    assert(info->lock_init);
    if (H5TS_dlftt_rwlock_rdunlock(&info->lock) < 0)
        HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock ID info");

done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_rdunlock() */

/*-------------------------------------------------------------------------
 * Function:    H5I__id_info_free
 *
 * Purpose:     Release an ID info object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5I__id_info_free(H5I_id_info_t *info, bool
#ifndef H5_HAVE_CONCURRENCY
                                           H5_ATTR_UNUSED
#endif /* NDEBUG */
                                               is_locked)
{
    herr_t ret_value = SUCCEED; /* Return value */

#ifdef H5_HAVE_CONCURRENCY
    FUNC_ENTER_PACKAGE
#else  /* H5_HAVE_CONCURRENCY */
    FUNC_ENTER_PACKAGE_NOERR
#endif /* H5_HAVE_CONCURRENCY */

    /* Sanity check */
    assert(info);

#ifdef H5_HAVE_CONCURRENCY
    /* If locked, release the lock protecting the ID info */
    if (is_locked) {
        assert(info->lock_init);
        if (H5TS_dlftt_rwlock_wrunlock(&info->lock) < 0)
            HGOTO_ERROR(H5E_ID, H5E_CANTUNLOCK, FAIL, "can't unlock ID info");
    }

    if (info->lock_init)
        H5TS_dlftt_rwlock_destroy(&info->lock);
#endif /* H5_HAVE_CONCURRENCY */

    H5FL_FREE(H5I_id_info_t, info);

#ifdef H5_HAVE_CONCURRENCY
done:
#endif /* H5_HAVE_CONCURRENCY */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5I__id_info_free() */
