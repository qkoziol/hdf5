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
 * Purpose:	This file contains declarations which are visible only within
 *          the H5VL package.  Source files outside the H5VL package should
 *          include H5VLprivate.h instead.
 */

#include "H5Pprivate.h"
#if !(defined H5VL_FRIEND || defined H5VL_MODULE)
#error "Do not include this file outside the H5VL package!"
#endif

#ifndef H5VLpkg_H
#define H5VLpkg_H

/* Get package's private header */
#include "H5VLprivate.h" /* Generic Functions                    */

/* Other private headers needed by this file */
#include "H5TSprivate.h" /* Threadsafety                         */

/**************************/
/* Package Private Macros */
/**************************/

/****************************/
/* Package Private Typedefs */
/****************************/

/* Define portable atomic types */
H5TS_DEF_ATOMIC_TYPE(size_t)
H5TS_DEF_ATOMIC_TYPE(int64_t)

/* Internal struct to track VOL connectors */
/* NOTE: From a concurrency standpoint, the cls field in H5VL_connector_t is
 *      constant from the point they are created.  The refcount field changes
 *      as an atomic counter, which ultimately controls when the struct is
 *      destroyed.  So, the cls field is '<foo> * const' (i.e. immutable after
 *      initialization) and the refcount is atomic.  The next & prev fields
 *      are part of the connector list and are guarded by the lock on that
 *      list (H5VL_conn_list_lock_g, in src/H5VLint.c).  Therefore no locking
 *      on the H5VL_connector_t struct itself is required.
 */
struct H5VL_connector_t {
    /* Pointer to connector class struct   */
    union {
        H5VL_class_t *non_c_cls; /* Write-only, during struct init */
        H5VL_class_t *const cls; /* Read-only, at all other times */
    };
    H5TS_ATOMIC_TYPE(int64_t) nrefs;      /* Number of references to this struct */
    struct H5VL_connector_t *next, *prev; /* Pointers to the next & previous */
                                          /* connectors in global list of active connectors */
};

/* Internal VOL object structure returned to the API */
/* NOTE: From a concurrency standpoint, H5VL_object_t's are constant from the
 *      point they are created.  The only field that changes is the refcount,
 *      which ultimately controls only when it is destroyed.  So, the pointer
 *      fields are '<foo> * const' (i.e. immutable after initialization) and
 *      the refcount is atomic.  Therefore no locking on the structure itself
 *      is required.
 */
struct H5VL_object_t {
    /* Pointer to connector-managed data for this object */
    union {
        void *non_c_data; /* Write-only, during struct init */
        void *const data; /* Read-only, at all other times */
    };
    /* Pointer to VOL connector used by this object */
    union {
        H5VL_connector_t *non_c_connector; /* Write-only, during struct init */
        H5VL_connector_t *const connector; /* Read-only, at all other times */
    };
    H5TS_ATOMIC_TYPE(size_t) rc;       /* Reference count */
};

/*****************************/
/* Package Private Variables */
/*****************************/

/******************************/
/* Package Private Prototypes */
/******************************/
H5_DLL herr_t            H5VL__set_def_conn(void);
H5_DLL H5VL_connector_t *H5VL__register_connector(const H5VL_class_t *cls, H5P_genplist_t *vipl);
H5_DLL H5VL_connector_t *H5VL__register_connector_by_class(const H5VL_class_t *cls, H5P_genplist_t *vipl);
H5_DLL H5VL_connector_t *H5VL__register_connector_by_name(const char *name, H5P_genplist_t *vipl);
H5_DLL H5VL_connector_t *H5VL__register_connector_by_value(H5VL_class_value_t value, H5P_genplist_t *vipl);
H5_DLL htri_t            H5VL__is_connector_registered_by_name(const char *name);
H5_DLL htri_t            H5VL__is_connector_registered_by_value(H5VL_class_value_t value);
H5_DLL H5VL_connector_t *H5VL__get_connector_by_name(const char *name);
H5_DLL H5VL_connector_t *H5VL__get_connector_by_value(H5VL_class_value_t value);
H5_DLL herr_t H5VL__connector_str_to_info(const char *str, H5VL_connector_t *connector, void **info);
H5_DLL size_t H5VL__get_connector_name(const H5VL_connector_t *connector, char *name /*out*/, size_t size);
H5_DLL herr_t H5VL__register_opt_operation(H5VL_subclass_t subcls, const char *op_name, int *op_val);
H5_DLL size_t H5VL__num_opt_operation(void);
H5_DLL herr_t H5VL__find_opt_operation(H5VL_subclass_t subcls, const char *op_name, int *op_val);
H5_DLL herr_t H5VL__unregister_opt_operation(H5VL_subclass_t subcls, const char *op_name);
H5_DLL herr_t H5VL__term_opt_operation(void);

/* Register the internal VOL connectors */
H5_DLL herr_t H5VL__native_register(void);
H5_DLL herr_t H5VL__native_unregister(void);
H5_DLL herr_t H5VL__passthru_register(void);
H5_DLL herr_t H5VL__passthru_unregister(void);

/* Testing functions */
#ifdef H5VL_TESTING
H5_DLL herr_t H5VL__reparse_def_vol_conn_variable_test(void);
H5_DLL htri_t H5VL__is_native_connector_test(hid_t vol_id);
H5_DLL hid_t  H5VL__register_using_vol_id_test(H5I_type_t type, void *object, hid_t vol_id);
#endif /* H5VL_TESTING */

#endif /* H5VLpkg_H */
