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

/*-------------------------------------------------------------------------
 *
 * Created:     H5FDint.c
 *
 * Purpose:     Internal routine for VFD operations
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5FDmodule.h" /* This source code file is part of the H5FD module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5FDpkg.h"     /* File Drivers                             */
#include "H5FLprivate.h" /* Free Lists                               */
#include "H5Iprivate.h"  /* IDs                                      */
#include "H5PLprivate.h" /* Plugins                                  */

/* VFD drivers */
#include "H5FDcore_private.h" /* core VFD driver */
#ifdef H5_HAVE_DIRECT
#include "H5FDdirect_private.h" /* direct VFD driver */
#endif
#include "H5FDfamily_private.h" /* family VFD driver */
#ifdef H5_HAVE_LIBHDFS
#include "H5FDhdfs_private.h" /* hdfs VFD driver */
#endif
#ifdef H5_HAVE_IOC_VFD
#include "H5FDioc_private.h" /* ioc VFD driver */
#endif
#include "H5FDlog_private.h" /* log VFD driver */
#ifdef H5_HAVE_MIRROR_VFD
#include "H5FDmirror_private.h" /* mirror VFD driver */
#endif
#ifdef H5_HAVE_PARALLEL
#include "H5FDmpio_private.h" /* mpio VFD driver */
#endif
#include "H5FDmulti_private.h" /* multi VFD driver */
#include "H5FDonion_private.h" /* onion VFD driver */
#ifdef H5_HAVE_ROS3_VFD
#include "H5FDros3_private.h" /* ros3 VFD driver */
#endif
#include "H5FDsec2_private.h"     /* sec2 VFD driver */
#include "H5FDsplitter_private.h" /* splitter VFD driver */
#include "H5FDstdio_private.h"    /* stdio VFD driver */
#ifdef H5_HAVE_SUBFILING_VFD
#include "H5FDsubfiling_private.h" /* subfiling VFD driver */
#endif

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/* Information needed for iterating over the registered VFD hid_t IDs.
 * The name or value of the new VFD that is being registered is stored
 * in the name (or value) field and the found_id field is initialized to
 * H5I_INVALID_HID (-1).  If we find a VFD with the same name / value,
 * we set the found_id field to the existing ID for return to the function.
 */
typedef struct H5FD_get_driver_ud_t {
    /* IN */
    H5PL_vfd_key_t key;

    /* OUT */
    H5FD_driver_t *found_driver; /* The driver, if we found a match */
} H5FD_get_driver_ud_t;

/********************/
/* Package Typedefs */
/********************/

/********************/
/* Local Prototypes */
/********************/
static herr_t         H5FD__free_cls(const H5FD_class_t *cls);
static herr_t         H5FD__set_def_driver(void);
static herr_t         H5FD__driver_find(H5PL_vfd_key_t *key, H5FD_driver_t **driver);
static H5FD_driver_t *H5FD__driver_create(H5FD_class_t *cls);
static herr_t         H5FD__driver_free(H5FD_driver_t *driver);
static herr_t         H5FD__driver_free_id(H5FD_driver_t *driver, void **request);

/*********************/
/* Package Variables */
/*********************/

/*
 * Global count of the number of H5FD_t's handed out.  This is used as a
 * "serial number" for files that are currently open and is used for the
 * 'fileno' field in H5O_info_t.  However, if a VFL driver is not able
 * to detect whether two files are the same, a file that has been opened
 * by H5Fopen more than once with that VFL driver will have two different
 * serial numbers.  :-/
 *
 * Also, if a file is opened, the 'fileno' field is retrieved for an
 * object and the file is closed and re-opened, the 'fileno' value will
 * be different.
 */
unsigned long H5FD_file_serial_no_p;

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/* Declare a free list to manage the H5FD_class_t struct */
H5FL_DEFINE_STATIC(H5FD_class_t);

/* Declare a free list to manage the H5FD_driver_t struct */
H5FL_DEFINE_STATIC(H5FD_driver_t);

/* File driver ID class */
static H5I_class_t H5I_VFL_CLS[1] = {{
    H5I_VFL,                         /* ID class value */
    0,                               /* Class flags */
    0,                               /* # of reserved IDs for class */
    NULL,                            /* Callback for locking objects of this class */
    NULL,                            /* Callback for unlocking objects of this class */
    (H5I_free_t)H5FD__driver_free_id /* Callback routine for closing objects of this class */
}};

/* Flag indicating "top" of interface has been initialized */
static bool H5FD_top_package_initialize_s = false;

/* List of currently active VFD drivers */
static H5FD_driver_t *H5FD_driver_list_head_g = NULL;

/*-------------------------------------------------------------------------
 * Function:    H5FD_init_phase1
 *
 * Purpose:     Initialize the interface from some other package.
 *
 * Return:      Success:	non-negative
 *              Failure:	negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_init_phase1(void)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)
    /* FUNC_ENTER() does all the work */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_init_phase1() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__init_package
 *
 * Purpose:     Initialize the virtual file layer.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__init_package(void)
{
    char  *lock_env_var = NULL;    /* Environment variable pointer */
    herr_t ret_value    = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    if (H5I_register_type(H5I_VFL_CLS) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to initialize interface");

    /* Reset the file serial numbers */
    H5FD_file_serial_no_p = 0;

    /* Check the use disabled file locks environment variable */
    lock_env_var = getenv(HDF5_USE_FILE_LOCKING);
    if (lock_env_var && !strcmp(lock_env_var, "BEST_EFFORT"))
        H5FD_ignore_disabled_file_locks_p = true; /* Override: Ignore disabled locks */
    else if (lock_env_var && (!strcmp(lock_env_var, "TRUE") || !strcmp(lock_env_var, "1")))
        H5FD_ignore_disabled_file_locks_p = false; /* Override: Don't ignore disabled locks */
    else
        H5FD_ignore_disabled_file_locks_p = FAIL; /* Environment variable not set, or not set correctly */

    /* Initialize all internal VFD drivers, so their driver IDs are set up */
    if (H5FD__core_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register core VFD");
#ifdef H5_HAVE_DIRECT
    if (H5FD__direct_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register direct VFD");
#endif
    if (H5FD__family_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register family VFD");
#ifdef H5_HAVE_LIBHDFS
    if (H5FD__hdfs_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register hdfs VFD");
#endif
#ifdef H5_HAVE_IOC_VFD
    if (H5FD__ioc_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register ioc VFD");
#endif
    if (H5FD__log_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register log VFD");
#ifdef H5_HAVE_MIRROR_VFD
    if (H5FD__mirror_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register mirror VFD");
#endif
#ifdef H5_HAVE_PARALLEL
    if (H5FD__mpio_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register mpio VFD");
#endif
    if (H5FD__multi_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register multi VFD");
    if (H5FD__onion_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register onion VFD");
#ifdef H5_HAVE_ROS3_VFD
    if (H5FD__ros3_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register ros3 VFD");
#endif
    if (H5FD__sec2_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register sec2 VFD");
    if (H5FD__splitter_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register splitter VFD");
    if (H5FD__stdio_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register stdio VFD");
#ifdef H5_HAVE_SUBFILING_VFD
    if (H5FD__subfiling_register() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register subfiling VFD");
#endif

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_init_phase2
 *
 * Purpose:     Initialize the virtual file layer.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_init_phase2(void)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Set up the default VFD driver in the default FAPL */
    if (H5FD__set_def_driver() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set default VFD driver");

    /* Mark "top" of interface as initialized */
    H5FD_top_package_initialize_s = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_init_phase2() */

/*-------------------------------------------------------------------------
 * Function: H5FD_top_term_package
 *
 * Purpose:  Close the "top" of the interface, releasing IDs, etc.
 *
 * Return:   Success:    Positive if anything was done that might
 *                affect other interfaces; zero otherwise.
 *           Failure:    Negative.
 *-------------------------------------------------------------------------
 */
int
H5FD_top_term_package(void)
{
    int n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (H5FD_top_package_initialize_s) {
        H5FD_driver_prop_t def_driver_prop = {NULL, NULL, NULL}; /* VFD driver for default FAPL */

        /* Reset default VFL driver for default FAPL */
        n += (H5P_set_driver(H5P_LST_FILE_ACCESS_g, NULL, NULL, NULL) < 0);

        /* Reset default VFL driver for default file access pclass */
        n += (H5P_reset_vfd_class(H5P_CLS_FILE_ACCESS_g, &def_driver_prop) < 0);

        /* Mark closed */
        if (0 == n)
            H5FD_top_package_initialize_s = false;
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5FD_top_term_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_term_package
 *
 * Purpose:     Terminate this interface: free all memory and reset global
 *              variables to their initial values.  Release all ID groups
 *              associated with this interface.
 *
 * Return:      Success:    Positive if anything was done that might
 *                          have affected other interfaces; zero
 *                          otherwise.
 *
 *              Failure:    Never fails.
 *
 *-------------------------------------------------------------------------
 */
int
H5FD_term_package(void)
{
    int n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (H5_PKG_INIT_VAR) {
        if (H5I_nmembers(H5I_VFL) > 0) {
            /* Destroy the VFL driver ID group */
            (void)H5I_clear_type(H5I_VFL, false, false);

            /* Reset all internal VFD driver IDs */
            H5FD__core_unregister();
#ifdef H5_HAVE_DIRECT
            H5FD__direct_unregister();
#endif
            H5FD__family_unregister();
#ifdef H5_HAVE_LIBHDFS
            H5FD__hdfs_unregister();
#endif
#ifdef H5_HAVE_IOC_VFD
            H5FD__ioc_unregister();
#endif
            H5FD__log_unregister();
#ifdef H5_HAVE_MIRROR_VFD
            H5FD__mirror_unregister();
#endif
#ifdef H5_HAVE_PARALLEL
            H5FD__mpio_unregister();
#endif
            H5FD__multi_unregister();
            H5FD__onion_unregister();
#ifdef H5_HAVE_ROS3_VFD
            H5FD__ros3_unregister();
#endif
            H5FD__sec2_unregister();
            H5FD__splitter_unregister();
            H5FD__stdio_unregister();
#ifdef H5_HAVE_SUBFILING_VFD
            H5FD__subfiling_unregister();
#endif

            n++; /*H5I*/
        }        /* end if */
        else {
            /* Destroy the VFL driver ID group */
            n += (H5I_dec_type_ref(H5I_VFL) > 0);

            /* Mark closed */
            if (0 == n)
                H5_PKG_INIT_VAR = false;
        } /* end else */
    }     /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5FD_term_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__free_cls
 *
 * Purpose:     Frees a file driver class struct and returns an indication of
 *              success. This function is used as the free callback for the
 *              virtual file layer object identifiers (cf H5FD__init_package).
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__free_cls(const H5FD_class_t *cls)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    assert(cls);

    /* If the file driver has a terminate callback, call it to give the file
     * driver a chance to free singletons or other resources which will become
     * invalid once the class structure is freed.
     */
    if (cls->terminate) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = cls->terminate();
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEOBJ, FAIL, "virtual file driver '%s' did not terminate cleanly",
                        cls->name);
    }

    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    H5FL_FREE(H5FD_class_t, (H5FD_class_t *)cls);
    H5_GCC_CLANG_DIAG_ON("cast-qual")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__free_cls() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__set_def_driver
 *
 * Purpose:     Parses a string that contains the name of the default VFL
 *              driver for the default FAPL.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__set_def_driver(void)
{
    const char        *driver_env_var;
    const char        *driver_config_env_var = NULL;
    H5FD_driver_t     *driver                = NULL;               /* VFD driver */
    H5FD_driver_prop_t def_driver_prop       = {NULL, NULL, NULL}; /* VFD driver for default FAPL */
    herr_t             ret_value             = SUCCEED;

    FUNC_ENTER_PACKAGE

    /* Check if VFL driver environment variable is set */
    driver_env_var = getenv(HDF5_DRIVER);

    /* Only parse VFL driver string if it's set */
    if (driver_env_var && *driver_env_var) {
        htri_t driver_is_registered;

        /* Check for [legacy] aliases of the internal VFDs */
        if (!strcmp(driver_env_var, "core_paged"))
            driver_env_var = "core";
        else if (!strcmp(driver_env_var, "split"))
            driver_env_var = "multi";

        /* Check to see if the driver is already registered */
        if ((driver_is_registered = H5FD__is_driver_registered_by_name(driver_env_var, &driver)) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't check if VFL driver is already registered");
        else if (driver_is_registered) {
            /* Increment ref count on the already-registered VFD driver */
            if (H5FD__driver_inc_rc(driver) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTINC, FAIL, "can't increment ref count on VFD driver");
        } /* end else-if */
        else {
            /* Register the VFL driver */
            if (NULL == (driver = H5FD__register_driver_by_name(driver_env_var)))
                HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "can't register VFL driver");
        } /* end else */

        /* Get any driver config string from the environment variable */
        driver_config_env_var = getenv(HDF5_DRIVER_CONFIG);
    }
    else {
        /* Set the default VFD driver */
        driver = H5_DEFAULT_VFD_DRVR;

        /* Increment the ref count on the default driver */
        if (H5FD__driver_inc_rc(driver) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTINC, FAIL, "can't increment ref count on VFD driver");
    }

    /* Set new default VFL driver for default FAPL */
    if (H5P_set_driver(H5P_LST_FILE_ACCESS_g, driver, NULL, driver_config_env_var) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set default VFL driver for default FAPL");

    /* Get the [updated] driver property to use for the class */
    if (H5P_peek(H5P_LST_FILE_ACCESS_g, H5F_ACS_FILE_DRV_NAME, &def_driver_prop) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't get VFL driver info");

    /* Set new default VFL driver for default file access pclass */
    if (H5P_reset_vfd_class(H5P_CLS_FILE_ACCESS_g, &def_driver_prop) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL,
                    "can't set default VFD driver for default file access property class");

done:
    /* Release VFD used for default FAPL */
    if (driver && H5FD__driver_dec_rc(driver) < 0)
        HDONE_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "unable to release VFL driver");

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__set_def_driver() */

/*-------------------------------------------------------------------------
 * Function:	H5FD__driver_create
 *
 * Purpose:     Utility function to create a driver around a class
 *
 * Return:      Success:    Pointer to a new driver object
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
static H5FD_driver_t *
H5FD__driver_create(H5FD_class_t *cls)
{
    H5FD_driver_t *driver    = NULL; /* New VFD driver struct */
    H5FD_driver_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    assert(cls);

    /* Setup VFD driver struct */
    if (NULL == (driver = H5FL_CALLOC(H5FD_driver_t)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "can't allocate VFD driver struct");
    driver->cls = cls;

    /* Add driver to list of active VFD drivers */
    if (H5FD_driver_list_head_g) {
        driver->next                  = H5FD_driver_list_head_g;
        H5FD_driver_list_head_g->prev = driver;
    }
    H5FD_driver_list_head_g = driver;

    /* Set return value */
    ret_value = driver;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_create() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_find
 *
 * Purpose:     Find a matching driver
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__driver_find(H5PL_vfd_key_t *key, H5FD_driver_t **driver)
{
    H5FD_driver_t *node; /* Current node in linked list */

    FUNC_ENTER_PACKAGE_NOERR

    /* Check arguments */
    assert(key);
    assert(driver);

    /* Iterate over linked list of active drivers */
    node = H5FD_driver_list_head_g;
    while (node) {
        if (H5FD_GET_DRIVER_BY_NAME == key->kind) {
            if (0 == strcmp(node->cls->name, key->u.name)) {
                *driver = node;
                break;
            } /* end if */
        }     /* end if */
        else {
            assert(H5FD_GET_DRIVER_BY_VALUE == key->kind);
            if (node->cls->value == key->u.value) {
                *driver = node;
                break;
            } /* end if */
        }     /* end else */

        /* Advance to next node */
        node = node->next;
    }

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__driver_find() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_inc_rc
 *
 * Purpose:     Wrapper to increment the ref. count on a driver.
 *
 * Return:      Current ref. count (can't fail)
 *
 *-------------------------------------------------------------------------
 */
int64_t
H5FD__driver_inc_rc(H5FD_driver_t *driver)
{
    int64_t ret_value = -1;

    FUNC_ENTER_PACKAGE_NOERR

    /* Check arguments */
    assert(driver);

    /* Increment refcount for driver */
    driver->nrefs++;

    /* Set return value */
    ret_value = driver->nrefs;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_inc_rc() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_dec_rc
 *
 * Purpose:     Wrapper to decrement the ref. count on a driver.
 *
 * Return:      Current ref. count (>=0) on success, <0 on failure
 *
 *-------------------------------------------------------------------------
 */
int64_t
H5FD__driver_dec_rc(H5FD_driver_t *driver)
{
    int64_t ret_value = -1; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    assert(driver);

    /* Decrement refcount for driver */
    driver->nrefs--;

    /* Set return value */
    ret_value = driver->nrefs;

    /* Check for last reference */
    if (0 == driver->nrefs)
        if (H5FD__driver_free(driver) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTRELEASE, FAIL, "unable to free VFD driver");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_dec_rc() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_free
 *
 * Purpose:     Free a driver object
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__driver_free(H5FD_driver_t *driver)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    assert(driver);
    assert(0 == driver->nrefs);

    /* Remove driver from list of active VFD drivers */
    if (H5FD_driver_list_head_g == driver) {
        H5FD_driver_list_head_g = H5FD_driver_list_head_g->next;
        if (H5FD_driver_list_head_g)
            H5FD_driver_list_head_g->prev = NULL;
    }
    else {
        if (driver->prev)
            driver->prev->next = driver->next;
        if (driver->next)
            driver->next->prev = driver->prev;
    }

    if (H5FD__free_cls(driver->cls) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTRELEASE, FAIL, "can't free VFD class");

    H5FL_FREE(H5FD_driver_t, driver);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_free() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_free_id
 *
 * Purpose:     Shim for freeing driver ID
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__driver_free_id(H5FD_driver_t *driver, void H5_ATTR_UNUSED **request)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Check arguments */
    assert(driver);

    /* Decrement refcount on driver */
    if (H5FD__driver_dec_rc(driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "unable to decrement ref count on VFD driver");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_free_id() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__driver_register
 *
 * Purpose:     Registers a new file driver as a member of the virtual file
 *              driver class.  Certain fields of the class struct are
 *              required and that is checked here so it doesn't have to be
 *              checked every time the field is accessed.
 *
 * Return:      Success:    A file driver ID which is good until the
 *                          library is closed or the driver is
 *                          unregistered.
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
H5FD_driver_t *
H5FD__driver_register(const H5FD_class_t *cls)
{
    H5FD_driver_t *driver = NULL; /* Connector for class */
    H5FD_class_t  *saved  = NULL;
    H5FD_mem_t     type;
    H5FD_driver_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    assert(cls);
    assert(cls->open && cls->close);
    assert(cls->get_eoa && cls->set_eoa);
    assert(cls->get_eof);
    assert(cls->read && cls->write);
    for (type = H5FD_MEM_DEFAULT; type < H5FD_MEM_NTYPES; type++)
        assert(cls->fl_map[type] >= H5FD_MEM_NOLIST && cls->fl_map[type] < H5FD_MEM_NTYPES);

    /* Copy the class structure so the caller can reuse or free it */
    if (NULL == (saved = H5FL_MALLOC(H5FD_class_t)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "memory allocation failed for file driver class struct");
    H5MM_memcpy(saved, cls, sizeof(H5FD_class_t));
    if (NULL == (saved->name = H5MM_strdup(cls->name)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "memory allocation failed for file driver name");

    /* Create new file driver for the class */
    if (NULL == (driver = H5FD__driver_create(saved)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTCREATE, NULL, "unable to create file driver");

    /* Set return value */
    ret_value = driver;

done:
    if (NULL == ret_value) {
        if (driver) {
            if (H5FD__driver_free(driver) < 0)
                HDONE_ERROR(H5E_VFL, H5E_CANTRELEASE, NULL, "can't free file driver");
        }
        else if (saved) {
            if (saved->name)
                H5MM_xfree_const(saved->name);
            H5FL_FREE(H5FD_class_t, saved);
        }
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__driver_register() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__register_driver_by_name
 *
 * Purpose:     Registers a new VFD as a member of the virtual file driver
 *              class.
 *
 * Return:      Success:    A pointer to a VFD
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
H5FD_driver_t *
H5FD__register_driver_by_name(const char *name)
{
    H5FD_driver_t *driver = NULL;    /* Driver for class */
    H5PL_vfd_key_t key;              /* Info for driver search */
    H5FD_driver_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Set up data for find */
    key.kind   = H5FD_GET_DRIVER_BY_NAME;
    key.u.name = name;

    /* Check if driver is already registered */
    if (H5FD__driver_find(&key, &driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTFIND, NULL, "can't search VFD drivers");

    /* If not found, create a new connector */
    if (NULL == driver) {
        H5PL_key_t          plugin_key;
        const H5FD_class_t *cls;

        /* Try loading the connector */
        plugin_key.vfd.kind   = H5FD_GET_DRIVER_BY_NAME;
        plugin_key.vfd.u.name = name;
        if (NULL == (cls = H5PL_load(H5PL_TYPE_VFD, &plugin_key)))
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, NULL, "unable to load VFD driver");

        /* Create a connector for the class we loaded */
        if (NULL == (driver = H5FD__driver_register(cls)))
            HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, NULL, "unable to register VFD driver");
    } /* end if */

    /* Inc. refcount on driver object, so it can be uniformly released */
    H5FD__driver_inc_rc(driver);

    /* Set return value */
    ret_value = driver;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__register_driver_by_name() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__register_driver_by_value
 *
 * Purpose:     Registers a new VFD as a member of the virtual file driver
 *              class.
 *
 * Return:      Success:    A VFD ID which is good until the library is
 *                          closed.
 *
 *              Failure:    H5I_INVALID_HID
 *
 *-------------------------------------------------------------------------
 */
H5FD_driver_t *
H5FD__register_driver_by_value(H5FD_class_value_t value)
{
    H5FD_driver_t *driver = NULL;
    H5PL_vfd_key_t key; /* Info for driver search */
    H5FD_driver_t *ret_value = NULL;

    FUNC_ENTER_PACKAGE

    /* Set up data for find */
    key.kind    = H5FD_GET_DRIVER_BY_VALUE;
    key.u.value = value;

    /* Check if driver is already registered */
    if (H5FD__driver_find(&key, &driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTFIND, NULL, "can't search VFD drivers");

    /* If not found, create a new connector */
    if (NULL == driver) {
        H5PL_key_t          plugin_key;
        const H5FD_class_t *cls;

        /* Try loading the driver */
        plugin_key.vfd.kind    = H5FD_GET_DRIVER_BY_VALUE;
        plugin_key.vfd.u.value = value;
        if (NULL == (cls = (const H5FD_class_t *)H5PL_load(H5PL_TYPE_VFD, &plugin_key)))
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, NULL, "unable to load VFD");

        /* Register the driver we loaded */
        if (NULL == (driver = H5FD__driver_register(cls)))
            HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, NULL, "unable to register VFD ID");
    } /* end if */

    /* Inc. refcount on driver object, so it can be uniformly released */
    H5FD__driver_inc_rc(driver);

    /* Set return value */
    ret_value = driver;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__register_driver_by_value() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__is_driver_registered_by_name
 *
 * Purpose:     Checks if a driver with a particular name is registered.
 *              If `registered_driver` is non-NULL and a driver with the
 *              specified name has been registered, the driver will be
 *              returned in `registered_driver`.
 *
 * Return:      >0 if a VFD with that name has been registered
 *              0 if a VFD with that name has NOT been registered
 *              <0 on errors
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5FD__is_driver_registered_by_name(const char *driver_name, H5FD_driver_t **registered_driver)
{
    H5PL_vfd_key_t key; /* Info for driver search */
    H5FD_driver_t *_registered_driver = NULL;
    htri_t         ret_value          = false; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Set up data for find */
    key.kind   = H5FD_GET_DRIVER_BY_NAME;
    key.u.name = driver_name;

    /* Allow this routine to be called with registered_driver = NULL */
    if (NULL == registered_driver)
        registered_driver = &_registered_driver;

    /* Find driver with name */
    if (H5FD__driver_find(&key, registered_driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADITER, FAIL, "can't find VFD driver");

    /* Found a driver with that name */
    if (*registered_driver)
        ret_value = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__is_driver_registered_by_name() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__is_driver_registered_by_value
 *
 * Purpose:     Checks if a driver with a particular value (ID) is
 *              registered. If `registered_driver` is non-NULL and a driver
 *              with the specified value has been registered, the driver will
 *              be returned in `registered_driver`.
 *
 * Return:      >0 if a VFD with that value has been registered
 *              0 if a VFD with that value has NOT been registered
 *              <0 on errors
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5FD__is_driver_registered_by_value(H5FD_class_value_t driver_value, H5FD_driver_t **registered_driver)
{
    H5PL_vfd_key_t key; /* Info for driver search */
    H5FD_driver_t *_registered_driver = NULL;
    htri_t         ret_value          = false; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Set up data for find */
    key.kind    = H5FD_GET_DRIVER_BY_VALUE;
    key.u.value = driver_value;

    /* Allow this routine to be called with registered_driver = NULL */
    if (NULL == registered_driver)
        registered_driver = &_registered_driver;

    /* Find driver with name */
    if (H5FD__driver_find(&key, registered_driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADITER, FAIL, "can't find VFD driver");

    /* Found a driver with that name */
    if (*registered_driver)
        ret_value = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__is_driver_registered_by_value() */

/*-------------------------------------------------------------------------
 * Function:   H5FD_driver_prop_clone
 *
 * Purpose:    In-place clone of driver property contents.
 *
 * Note:        This is an "in-place" copy, since this routine gets called
 *              after a top-level copy has been performed and this routine
 *              finishes the "deep" part of the copy.
 *
 * Return:     Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_driver_prop_clone(H5FD_driver_prop_t *driver_prop)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Clone the driver property */
    if (driver_prop->driver) {
        /* Increment the reference count on driver and copy driver info */
        if (H5FD__driver_inc_rc(driver_prop->driver) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTINC, FAIL, "unable to increment ref count on VFL driver");

        /* Copy driver info, if it exists */
        if (driver_prop->driver_info) {
            void *new_pl; /* Copy of driver info */

            /* Allow the driver to copy or do it ourselves */
            if (driver_prop->driver->cls->fapl_copy) {
                if (NULL == (new_pl = (driver_prop->driver->cls->fapl_copy)(driver_prop->driver_info)))
                    HGOTO_ERROR(H5E_VFL, H5E_CANTCOPY, FAIL, "driver info copy failed");
            } /* end if */
            else if (driver_prop->driver->cls->fapl_size > 0) {
                if (NULL == (new_pl = H5MM_malloc(driver_prop->driver->cls->fapl_size)))
                    HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, FAIL, "driver info allocation failed");
                H5MM_memcpy(new_pl, driver_prop->driver_info, driver_prop->driver->cls->fapl_size);
            } /* end else-if */
            else
                HGOTO_ERROR(H5E_VFL, H5E_UNSUPPORTED, FAIL, "no way to copy driver info");

            /* Set the driver info for the copy */
            driver_prop->driver_info = new_pl;
        } /* end if */

        /* Copy driver configuration string, if it exists */
        if (driver_prop->driver_config_str) {
            char *new_config_str = NULL;

            if (NULL == (new_config_str = H5MM_strdup(driver_prop->driver_config_str)))
                HGOTO_ERROR(H5E_VFL, H5E_CANTCOPY, FAIL, "driver configuration string copy failed");
            driver_prop->driver_config_str = new_config_str;
        } /* end if */
    }     /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_prop_clone() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_driver_prop_cmp
 *
 * Purpose:     Compare two VFD driver properties.
 *
 * Return:      positive if PROP1 is greater than PROP2, negative if PROP2
 *              is greater than PROP1 and zero if PROP1 and PROP2 are equal.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_driver_prop_cmp(int *cmp_value, const H5FD_driver_prop_t *prop1, const H5FD_driver_prop_t *prop2)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Check arguments */
    assert(cmp_value);
    assert(prop1);
    assert(prop2);

    /* Fast check */
    if (prop1 == prop2)
        /* Set output comparison value */
        *cmp_value = 0;
    else {
        int tmp_cmp_value = 0; /* Value from comparison */

        /* Compare drivers' classes */
        if (H5FD_cmp_driver_cls(&tmp_cmp_value, prop1->driver->cls, prop2->driver->cls) < 0)
            HGOTO_ERROR(H5E_VOL, H5E_CANTCOMPARE, FAIL, "can't compare connector classes");
        if (tmp_cmp_value != 0)
            /* Set output comparison value */
            *cmp_value = tmp_cmp_value;
        else {
            /* Compare the strings */
            if (prop1->driver_info && !prop2->driver_info)
                tmp_cmp_value = 1;
            else if (!prop1->driver_info && prop2->driver_info)
                tmp_cmp_value = -1;
            else {
                if (prop1->driver_info && prop2->driver_info) {
                    assert(prop1->driver->cls->fapl_size == prop2->driver->cls->fapl_size);
                    assert(prop1->driver->cls->fapl_size > 0);
                    tmp_cmp_value =
                        memcmp(prop1->driver_info, prop2->driver_info, prop1->driver->cls->fapl_size);
                } /* end if */
            }     /* end else */

            if (0 == tmp_cmp_value) {
                if (prop1->driver_config_str && !prop2->driver_config_str)
                    tmp_cmp_value = 1;
                else if (!prop1->driver_config_str && prop2->driver_config_str)
                    tmp_cmp_value = -1;
                else {
                    if (prop1->driver_config_str && prop2->driver_config_str)
                        tmp_cmp_value = strcmp(prop1->driver_config_str, prop2->driver_config_str);
                } /* end else */
            }     /* end if*/

            /* Set output comparison value */
            *cmp_value = tmp_cmp_value;
        } /* end else */
    }     /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_prop_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_driver_prop_free
 *
 * Purpose:     Free contents of a VFL driver property
 *
 * Return:      Success:        Non-negative
 *              Failure:        Negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_driver_prop_free(H5FD_driver_prop_t *driver_prop)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    if (driver_prop) {
        if (driver_prop->driver) {
            /* Free the driver info, if it exists */
            if (driver_prop->driver_info) {
                if (H5FD_free_driver_info(driver_prop->driver, driver_prop->driver_info) < 0)
                    HGOTO_ERROR(H5E_VFL, H5E_CANTFREE, FAIL, "driver info free request failed");
                driver_prop->driver_info = NULL;
            }

            /* Free the driver configuration string, if it exists */
            H5MM_xfree_const(driver_prop->driver_config_str);
            driver_prop->driver_config_str = NULL;

            /* Decrement reference count for driver */
            if (H5FD__driver_dec_rc(driver_prop->driver) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "can't decrement reference count for driver ID");
            driver_prop->driver = NULL;
        }
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_prop_free() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_cmp_driver_cls
 *
 * Purpose:     Compare VFD class for a driver
 *
 * Note:        Sets *cmp_value positive if VALUE1 is greater than VALUE2,
 *		negative if VALUE2 is greater than VALUE1, and zero if VALUE1
 *              and VALUE2 are equal (like strcmp).
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_cmp_driver_cls(int *cmp_value, const H5FD_class_t *cls1, const H5FD_class_t *cls2)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(cls1);
    assert(cls2);

    /* If the pointers are the same the classes are the same */
    if (cls1 != cls2) {
        if (cls1->value < cls2->value) {
            *cmp_value = -1;
            HGOTO_DONE(SUCCEED);
        }
        else if (cls1->value > cls2->value) {
            *cmp_value = 1;
            HGOTO_DONE(SUCCEED);
        }

        if (!cls1->name && cls2->name) {
            *cmp_value = -1;
            HGOTO_DONE(SUCCEED);
        }
        else if (cls1->name && !cls2->name) {
            *cmp_value = 1;
            HGOTO_DONE(SUCCEED);
        }
        else {
            if (cls1->name && cls2->name)
                if (0 != (*cmp_value = strcmp(cls1->name, cls2->name)))
                    HGOTO_DONE(SUCCEED);
        }

        if (cls1->fapl_size < cls2->fapl_size) {
            *cmp_value = -1;
            HGOTO_DONE(SUCCEED);
        }
        else if (cls1->fapl_size > cls2->fapl_size) {
            *cmp_value = 1;
            HGOTO_DONE(SUCCEED);
        }
    }

    /* Set comparison value to 'equal' */
    *cmp_value = 0;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_cmp_driver_cls() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_locate_signature
 *
 * Purpose:     Finds the HDF5 superblock signature in a file.  The
 *              signature can appear at address 0, or any power of two
 *              beginning with 512.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_locate_signature(H5FD_int_t *fh, haddr_t *sig_addr)
{
    haddr_t  addr = HADDR_UNDEF;
    haddr_t  eoa  = HADDR_UNDEF;
    haddr_t  eof  = HADDR_UNDEF;
    uint8_t  buf[H5F_SIGNATURE_LEN];
    unsigned n;
    unsigned maxpow;
    herr_t   ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    assert(fh);
    assert(sig_addr);

    /* Find the least N such that 2^N is larger than the file size */
    eof  = H5FD_get_eof(fh, H5FD_MEM_SUPER);
    eoa  = H5FD_get_eoa(fh, H5FD_MEM_SUPER);
    addr = MAX(eof, eoa);
    if (HADDR_UNDEF == addr)
        HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to obtain EOF/EOA value");
    for (maxpow = 0; addr; maxpow++)
        addr >>= 1;
    maxpow = MAX(maxpow, 9);

    /* Search for the file signature at format address zero followed by
     * powers of two larger than 9.
     */
    for (n = 8; n < maxpow; n++) {
        addr = (8 == n) ? 0 : (haddr_t)1 << n;
        if (H5FD_set_eoa(fh, H5FD_MEM_SUPER, addr + H5F_SIGNATURE_LEN) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to set EOA value for file signature");
        if (H5FD_read(fh, H5FD_MEM_SUPER, addr, (size_t)H5F_SIGNATURE_LEN, buf) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to read file signature");
        if (!memcmp(buf, H5F_SIGNATURE, (size_t)H5F_SIGNATURE_LEN))
            break;
    }

    /* If the signature was not found then reset the EOA value and return
     * HADDR_UNDEF.
     */
    if (n >= maxpow) {
        if (H5FD_set_eoa(fh, H5FD_MEM_SUPER, eoa) < 0)
            HGOTO_ERROR(H5E_IO, H5E_CANTINIT, FAIL, "unable to reset EOA value");
        *sig_addr = HADDR_UNDEF;
    }
    else
        /* Set return value */
        *sig_addr = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_locate_signature() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_sb_load
 *
 * Purpose:     Validate and decode the driver information block.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_sb_load(H5FD_int_t *fh, const char *name, const uint8_t *buf)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver->cls);

    /* Check if driver matches driver information saved. Unfortunately, we can't push this
     * function to each specific driver because we're checking if the driver is correct.
     */
    if (!strncmp(name, "NCSAfami", (size_t)8) && strcmp(fh->driver->cls->name, "family") != 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "family driver should be used");
    if (!strncmp(name, "NCSAmult", (size_t)8) && strcmp(fh->driver->cls->name, "multi") != 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "multi driver should be used");

    /* Decode driver information */
    if (H5FD__sb_decode(fh, name, buf) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDECODE, FAIL, "unable to decode driver information");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sb_load() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_check_plugin_load
 *
 * Purpose:     Check if a VFD plugin matches the search criteria, and can
 *              be loaded.
 *
 * Note:        Matching the driver's name / value, but the driver having
 *              an incompatible version is not an error, but means that the
 *              driver isn't a "match".  Setting the SUCCEED value to false
 *              and not failing for that case allows the plugin framework
 *              to keep looking for other DLLs that match and have a
 *              compatible version.
 *
 * Return:      SUCCEED / FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_check_plugin_load(const H5FD_class_t *cls, const H5PL_key_t *key, bool *success)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(cls);
    assert(key);
    assert(success);

    /* Which kind of key are we looking for? */
    if (key->vfd.kind == H5FD_GET_DRIVER_BY_NAME) {
        /* Check if plugin name matches VFD class name */
        if (cls->name && !strcmp(cls->name, key->vfd.u.name))
            *success = true;
    }
    else {
        /* Sanity check */
        assert(key->vfd.kind == H5FD_GET_DRIVER_BY_VALUE);

        /* Check if plugin value matches VFD class value */
        if (cls->value == key->vfd.u.value)
            *success = true;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_check_plugin_load() */
