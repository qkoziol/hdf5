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
 * Purpose:
 *      Keep a set of "psuedo-global" information for an API call.  This
 *      general corresponds to the DXPL for the call, along with cached
 *      information from them.
 */

/****************/
/* Module Setup */
/****************/

#include "H5CXmodule.h" /* This source code file is part of the H5CX module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                    */
#include "H5CXpkg.h"     /* API Contexts                         */
#include "H5Dprivate.h"  /* Datasets                             */
#include "H5Eprivate.h"  /* Error handling                       */
#include "H5FLprivate.h" /* Free Lists                           */
#include "H5Iprivate.h"  /* IDs                                  */
#include "H5Lprivate.h"  /* Links                                */
#include "H5MMprivate.h" /* Memory management                    */
#include "H5Pprivate.h"  /* Property lists                       */
#include <string.h>

/****************/
/* Local Macros */
/****************/

/* Common macro for the retrieving the pointer to a property list */
#define H5CX_RETRIEVE_PLIST(PL, ERR_RET)                                                                     \
    /* Check if the property list is already available */                                                    \
    if (NULL == (*head)->ctx.PL)                                                                             \
        /* Get the property list pointer */                                                                  \
        if (H5_UNLIKELY(NULL ==                                                                              \
                        ((*head)->ctx.PL = (H5P_genplist_t *)H5I_object((*head)->ctx.H5_GLUE(PL, _id)))))    \
            HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, ERR_RET, "can't get property list");

/* Common macro for the duplicated code to retrieve a property from a property list */
#define H5CX_RETRIEVE_PROP(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                                 \
    /* Get/peek the property */                                                                              \
    if (H5_UNLIKELY(H5_GLUE(H5P_, MTHD)((*head)->ctx.PL, (PROP_NAME),                                        \
                                        &(*head)->ctx.H5_GLUE(SUB_PL, _props).PROP_FIELD) < 0))              \
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, ERR_RET, "can't retrieve value from API context");

/* Macros to inline testing / not testing for property existence before retrieving it */
#define H5CX_TEST_YES_PROP(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                                 \
    {                                                                                                        \
        htri_t check_prop = 0; /* Whether the property exists in the API context's DXPL */                   \
                                                                                                             \
        /* Check if property exists in PL */                                                                 \
        if (H5_UNLIKELY((check_prop = H5P_exist_plist((*head)->ctx.PL, PROP_NAME)) < 0))                     \
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, ERR_RET, "error checking for property");                   \
                                                                                                             \
        /* If property exists, retrieve it */                                                                \
        if (check_prop > 0)                                                                                  \
            H5CX_RETRIEVE_PROP(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                             \
    }
#define H5CX_TEST_NO_PROP(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                                  \
    /* Get/peek the property */                                                                              \
    H5CX_RETRIEVE_PROP(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)
#define H5CX_TEST_GET_PROP(PL, TST, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                            \
    H5_GLUE3(H5CX_TEST_, TST, _PROP)(PL, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)

/* Common macro for the duplicated code to retrieve properties from a property list */
#define H5CX_RETRIEVE_PROP_COMMON(PL, TST, MTHD, SUB_PL, DEF_PL, PROP_NAME, PROP_FIELD, ERR_RET)             \
    {                                                                                                        \
        /* Check for default property list */                                                                \
        if ((*head)->ctx.H5_GLUE(PL, _id) == (DEF_PL))                                                       \
            H5MM_memcpy(&(*head)->ctx.H5_GLUE(SUB_PL, _props).PROP_FIELD,                                    \
                        &H5_GLUE3(H5CX_def_, SUB_PL, _cache).PROP_FIELD,                                     \
                        sizeof(H5_GLUE3(H5CX_def_, SUB_PL, _cache).PROP_FIELD));                             \
        else {                                                                                               \
            /* Retrieve the property list */                                                                 \
            H5CX_RETRIEVE_PLIST(PL, ERR_RET)                                                                 \
                                                                                                             \
            /* Retrieve the property, possibly testing for existence */                                      \
            H5CX_TEST_GET_PROP(PL, TST, MTHD, SUB_PL, PROP_NAME, PROP_FIELD, ERR_RET)                        \
        } /* end else */                                                                                     \
                                                                                                             \
        /* Mark the field as valid */                                                                        \
        (*head)->ctx.H5_GLUE(SUB_PL, _flags).H5_GLUE(PROP_FIELD, _valid) = true;                             \
    }

/* Macro for the duplicated code to retrieve a value from a property list if the context value is invalid */
#define H5CX_RETRIEVE_PROP_VALID(PL, DEF_PL, PROP_NAME, PROP_FIELD)                                          \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                       \
    H5CX_RETRIEVE_PROP_COMMON(PL, NO, get, PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)

/* Macro for the duplicated code to test for and retrieve a value from a property list if the context value is
 * invalid
 */
#define H5CX_TEST_RETRIEVE_PROP_VALID(PL, DEF_PL, PROP_NAME, PROP_FIELD)                                     \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                       \
    H5CX_RETRIEVE_PROP_COMMON(PL, YES, get, PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)

/* Macro for the duplicated code to "peek" a value from a property list if the context value is invalid */
#define H5CX_PEEK_PROP_VALID(PL, DEF_PL, PROP_NAME, PROP_FIELD)                                              \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                       \
    H5CX_RETRIEVE_PROP_COMMON(PL, NO, peek, PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)

/* Macro for the duplicated code to "peek" a value from a property list if the context value is invalid */
#define H5CX_PEEK_PROP_VALID_ERR(PL, DEF_PL, PROP_NAME, PROP_FIELD, ERR_RET)                                 \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                       \
    H5CX_RETRIEVE_PROP_COMMON(PL, NO, peek, PL, DEF_PL, PROP_NAME, PROP_FIELD, ERR_RET)

/* Macro for the duplicated code to retrieve a value from a property list if the context value is invalid */
#define H5CX_RETRIEVE_SUBCLS_PROP_VALID(PL, SUB_PL, DEF_PL, PROP_NAME, PROP_FIELD)                           \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(SUB_PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                   \
    H5CX_RETRIEVE_PROP_COMMON(PL, NO, get, SUB_PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)

#ifdef H5O_ENABLE_BOGUS
/* Macro for the duplicated code to retrieve a value from a property list if the context value is invalid */
#define H5CX_TEST_RETRIEVE_SUBCLS_PROP_VALID(PL, SUB_PL, DEF_PL, PROP_NAME, PROP_FIELD)                      \
    /* Check if the value has been retrieved already */                                                      \
    if (!(*head)->ctx.H5_GLUE(SUB_PL, _flags).H5_GLUE(PROP_FIELD, _valid))                                   \
    H5CX_RETRIEVE_PROP_COMMON(PL, YES, get, SUB_PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)
#endif /* H5O_ENABLE_BOGUS */

/* Macro for the duplicated code to retrieve a value from a property list if the context value is invalid, or
 * the library has previously modified the context value for return */
#define H5CX_RETRIEVE_PROP_VALID_SET(PL, DEF_PL, PROP_NAME, PROP_FIELD)                                      \
    /* Check if the value has been retrieved already */                                                      \
    if (!((*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _valid) ||                                    \
          (*head)->ctx.H5_GLUE(PL, _flags).H5_GLUE(PROP_FIELD, _set)))                                       \
    H5CX_RETRIEVE_PROP_COMMON(PL, NO, get, PL, DEF_PL, PROP_NAME, PROP_FIELD, FAIL)

#if defined(H5_HAVE_PARALLEL) && defined(H5_HAVE_INSTRUMENTED_LIBRARY)
/* Macro for the duplicated code to set a context field that may not exist as a property */
#define H5CX_TEST_SET_PROP(PROP_NAME, PROP_FIELD)                                                            \
    {                                                                                                        \
        htri_t check_prop = 0; /* Whether the property exists in the API context's DXPL */                   \
                                                                                                             \
        /* Check if property exists in DXPL */                                                               \
        if (!(*head)->ctx.dxpl_flags.H5_GLUE(PROP_FIELD, _set)) {                                            \
            /* Retrieve the dataset transfer property list */                                                \
            H5CX_RETRIEVE_PLIST(dxpl, FAIL)                                                                  \
                                                                                                             \
            if (H5_UNLIKELY((check_prop = H5P_exist_plist((*head)->ctx.dxpl, PROP_NAME)) < 0))               \
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "error checking for property");                  \
        } /* end if */                                                                                       \
                                                                                                             \
        /* If property was already set or exists (for first set), update it */                               \
        if ((*head)->ctx.dxpl_flags.H5_GLUE(PROP_FIELD, _set) || check_prop > 0) {                           \
            /* Cache the value for later, marking it to set in DXPL when context popped */                   \
            (*head)->ctx.dxpl_props.PROP_FIELD                = PROP_FIELD;                                  \
            (*head)->ctx.dxpl_flags.H5_GLUE(PROP_FIELD, _set) = true;                                        \
        } /* end if */                                                                                       \
    }
#endif /* defined(H5_HAVE_PARALLEL) && defined(H5_HAVE_INSTRUMENTED_LIBRARY) */

/* Macro for the duplicated code to test and set properties for a property list from the context */
#define H5CX_SET_PROP(PROP_NAME, PROP_FIELD)                                                                 \
    if ((*head)->ctx.dxpl_flags.H5_GLUE(PROP_FIELD, _set)) {                                                 \
        /* Retrieve the dataset transfer property list */                                                    \
        H5CX_RETRIEVE_PLIST(dxpl, FAIL)                                                                      \
                                                                                                             \
        /* Set the property */                                                                               \
        if (H5_UNLIKELY(H5P_set((*head)->ctx.dxpl, PROP_NAME, &(*head)->ctx.dxpl_props.PROP_FIELD) < 0))     \
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTSET, FAIL, "error setting data xfer property");                 \
    } /* end if */

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/
static void H5CX__reset_lapl(H5CX_node_t *head);
static void H5CX__reset_dapl(H5CX_node_t *head);
static void H5CX__reset_dxpl(H5CX_node_t *head);
static void H5CX__reset_lcpl(H5CX_node_t *head);
static void H5CX__reset_acpl(H5CX_node_t *head);
static void H5CX__reset_ocpl(H5CX_node_t *head);

/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
bool H5_PKG_INIT_VAR = false;

/*******************/
/* Local Variables */
/*******************/

#ifndef H5_HAVE_THREADSAFE_API
H5CX_node_t *H5CX_head_g = NULL; /* Pointer to head of context stack */
#endif                           /* H5_HAVE_THREADSAFE_API */

/* These are initialized to the values in each default property list during
 * package initialization and then remains constant for the rest of the library's
 * operation.  When a field in H5CX_t is retrieved from an API context that
 * uses a default property list, this value is copied instead of spending time
 * looking up the property in the property list.
 */

/* Define a "default" dataset transfer property list cache structure to use for default DXPLs */
static H5CX_dxpl_cache_t H5CX_def_dxpl_cache;

/* Define a "default" link creation property list cache structure to use for default LCPLs */
static H5CX_lcpl_cache_t H5CX_def_lcpl_cache;

/* Define a "default" link access property list cache structure to use for default LAPLs */
static H5CX_lapl_cache_t H5CX_def_lapl_cache;

/* Define a "default" object creation property list cache structure to use for default OCPLs */
static H5CX_ocpl_cache_t H5CX_def_ocpl_cache;

/* Define a "default" object copy property list cache structure to use for default OCPYPLs */
static H5CX_ocpypl_cache_t H5CX_def_ocpypl_cache;

/* Define a "default" dataset creation property list cache structure to use for default DCPLs */
static H5CX_dcpl_cache_t H5CX_def_dcpl_cache;

/* Define a "default" group creation property list cache structure to use for default GCPLs */
static H5CX_gcpl_cache_t H5CX_def_gcpl_cache;

/* Define a "default" attribute creation property list cache structure to use for default ACPLs */
static H5CX_acpl_cache_t H5CX_def_acpl_cache;

/* Define a "default" dataset access property list cache structure to use for default DAPLs */
static H5CX_dapl_cache_t H5CX_def_dapl_cache;

/* Define a "default" file access property list cache structure to use for default FAPLs */
static H5CX_fapl_cache_t H5CX_def_fapl_cache;

/* Define a "default" file creation property list cache structure to use for default FCPLs */
static H5CX_fcpl_cache_t H5CX_def_fcpl_cache;

/* Flag indicating "top" of interface has been initialized */
static bool H5CX_top_package_initialize_s = false;

/* Declare a static free list to manage H5CX_state_t structs */
H5FL_DEFINE_STATIC(H5CX_state_t);

/*--------------------------------------------------------------------------
NAME
    H5CX__init_package -- Initialize interface-specific information
USAGE
    herr_t H5CX__init_package()
RETURNS
    Non-negative on success/Negative on failure
DESCRIPTION
    Initializes any interface-specific data or routines.
--------------------------------------------------------------------------*/
herr_t
H5CX__init_package(void)
{
    H5P_genplist_t *dapl      = H5P_LST_DATASET_ACCESS_g;   /* Dataset access property list */
    H5P_genplist_t *dcpl      = H5P_LST_DATASET_CREATE_g;   /* Dataset creation property list */
    H5P_genplist_t *dxpl      = H5P_LST_DATASET_XFER_g;     /* Default data transfer property list */
    H5P_genplist_t *fapl      = H5P_LST_FILE_ACCESS_g;      /* File access property list */
    H5P_genplist_t *fcpl      = H5P_LST_FILE_CREATE_g;      /* File creation property list */
    H5P_genplist_t *gcpl      = H5P_LST_GROUP_CREATE_g;     /* Group creation property list */
    H5P_genplist_t *acpl      = H5P_LST_ATTRIBUTE_CREATE_g; /* Attribute creation property list */
    H5P_genplist_t *lcpl      = H5P_LST_LINK_CREATE_g;      /* Link creation property list */
    H5P_genplist_t *lapl      = H5P_LST_LINK_ACCESS_g;      /* Link access property list */
    H5P_genplist_t *ocpl      = H5P_LST_OBJECT_CREATE_g;    /* Object creation property list */
    H5P_genplist_t *ocpypl    = H5P_LST_OBJECT_COPY_g;      /* Object copy property list */
    herr_t          ret_value = SUCCEED;                    /* Return value */

    FUNC_ENTER_PACKAGE

    /* Reset the "default DXPL cache" information */
    memset(&H5CX_def_dxpl_cache, 0, sizeof(H5CX_dxpl_cache_t));

    /* Get the default DXPL cache information */

    /* Get B-tree split ratios */
    if (H5P_get(dxpl, H5D_XFER_BTREE_SPLIT_RATIO_NAME, &H5CX_def_dxpl_cache.btree_split_ratio) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve B-tree split ratios");

    /* Get maximum temporary buffer size value */
    if (H5P_get(dxpl, H5D_XFER_MAX_TEMP_BUF_NAME, &H5CX_def_dxpl_cache.max_temp_buf) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve maximum temporary buffer size");

    /* Get temporary buffer pointer */
    if (H5P_get(dxpl, H5D_XFER_TCONV_BUF_NAME, &H5CX_def_dxpl_cache.tconv_buf) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve temporary buffer pointer");

    /* Get background buffer pointer */
    if (H5P_get(dxpl, H5D_XFER_BKGR_BUF_NAME, &H5CX_def_dxpl_cache.bkgr_buf) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve background buffer pointer");

    /* Get background buffer type */
    if (H5P_get(dxpl, H5D_XFER_BKGR_BUF_TYPE_NAME, &H5CX_def_dxpl_cache.bkgr_buf_type) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve background buffer type");

    /* Get I/O vector size */
    if (H5P_get(dxpl, H5D_XFER_HYPER_VECTOR_SIZE_NAME, &H5CX_def_dxpl_cache.vec_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve I/O vector size");

#ifdef H5_HAVE_PARALLEL
    /* Collect Parallel I/O information for possible later use */
    if (H5P_get(dxpl, H5D_XFER_IO_XFER_MODE_NAME, &H5CX_def_dxpl_cache.io_xfer_mode) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve parallel transfer method");
    if (H5P_get(dxpl, H5D_XFER_MPIO_COLLECTIVE_OPT_NAME, &H5CX_def_dxpl_cache.mpio_coll_opt) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve collective transfer option");
    if (H5P_get(dxpl, H5D_XFER_MPIO_CHUNK_OPT_HARD_NAME, &H5CX_def_dxpl_cache.mpio_chunk_opt_mode) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve chunk optimization option");
    if (H5P_get(dxpl, H5D_XFER_MPIO_CHUNK_OPT_NUM_NAME, &H5CX_def_dxpl_cache.mpio_chunk_opt_num) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve chunk optimization threshold");
    if (H5P_get(dxpl, H5D_XFER_MPIO_CHUNK_OPT_RATIO_NAME, &H5CX_def_dxpl_cache.mpio_chunk_opt_ratio) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve chunk optimization ratio");

    /* Get the local & global reasons for breaking collective I/O values */
    if (H5P_get(dxpl, H5D_MPIO_LOCAL_NO_COLLECTIVE_CAUSE_NAME,
                &H5CX_def_dxpl_cache.mpio_local_no_coll_cause) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve local cause for breaking collective I/O");
    if (H5P_get(dxpl, H5D_MPIO_GLOBAL_NO_COLLECTIVE_CAUSE_NAME,
                &H5CX_def_dxpl_cache.mpio_global_no_coll_cause) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve global cause for breaking collective I/O");
#endif /* H5_HAVE_PARALLEL */

    /* Get error detection properties */
    if (H5P_get(dxpl, H5D_XFER_EDC_NAME, &H5CX_def_dxpl_cache.err_detect) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve error detection info");

    /* Get filter callback function */
    if (H5P_get(dxpl, H5D_XFER_FILTER_CB_NAME, &H5CX_def_dxpl_cache.filter_cb) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve filter callback function");

    /* Look at the data transform property */
    /* (Note: 'peek', not 'get' - if this turns out to be a problem, we may need
     *          to copy it and free this in the H5CX terminate routine. -QAK)
     */
    if (H5P_peek(dxpl, H5D_XFER_XFORM_NAME, &H5CX_def_dxpl_cache.data_transform) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve data transform info");

    /* Get VL datatype alloc info */
    if (H5P_get(dxpl, H5D_XFER_VLEN_ALLOC_NAME, &H5CX_def_dxpl_cache.vl_alloc_info.alloc_func) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
    if (H5P_get(dxpl, H5D_XFER_VLEN_ALLOC_INFO_NAME, &H5CX_def_dxpl_cache.vl_alloc_info.alloc_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
    if (H5P_get(dxpl, H5D_XFER_VLEN_FREE_NAME, &H5CX_def_dxpl_cache.vl_alloc_info.free_func) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
    if (H5P_get(dxpl, H5D_XFER_VLEN_FREE_INFO_NAME, &H5CX_def_dxpl_cache.vl_alloc_info.free_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");

    /* Get datatype conversion struct */
    if (H5P_get(dxpl, H5D_XFER_CONV_CB_NAME, &H5CX_def_dxpl_cache.dt_conv_cb) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve datatype conversion exception callback");

    /* Get the selection I/O mode */
    if (H5P_get(dxpl, H5D_XFER_SELECTION_IO_MODE_NAME, &H5CX_def_dxpl_cache.selection_io_mode) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve selection I/O mode");

    /* Get the local & global reasons for breaking selection I/O values */
    if (H5P_get(dxpl, H5D_XFER_NO_SELECTION_IO_CAUSE_NAME, &H5CX_def_dxpl_cache.no_selection_io_cause) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve cause for no selection I/O");

    /* Get the actual selection I/O mode */
    if (H5P_get(dxpl, H5D_XFER_ACTUAL_SELECTION_IO_MODE_NAME, &H5CX_def_dxpl_cache.actual_selection_io_mode) <
        0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve actual selection I/O mode");

    /* Get the modify write buffer property */
    if (H5P_get(dxpl, H5D_XFER_MODIFY_WRITE_BUF_NAME, &H5CX_def_dxpl_cache.modify_write_buf) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve modify write buffer property");

    /* Get dataset I/O selection property */
    if (H5P_get(dxpl, H5D_XFER_DSET_IO_SEL_NAME, &H5CX_def_dxpl_cache.dset_io_selection) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve dataset I/O selection");

    /* Reset the "default LCPL cache" information */
    memset(&H5CX_def_lcpl_cache, 0, sizeof(H5CX_lcpl_cache_t));

    /* Get the default LCPL cache information */

    /* Get link name character encoding */
    if (H5P_get(lcpl, H5P_STRCRT_CHAR_ENCODING_NAME, &H5CX_def_lcpl_cache.encoding) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve link name encoding");

    /* Get flag whether to create intermediate groups */
    if (H5P_get(lcpl, H5L_CRT_INTERMEDIATE_GROUP_NAME, &H5CX_def_lcpl_cache.intermediate_group) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve intermediate group creation flag");

    /* Reset the "default LAPL cache" information */
    memset(&H5CX_def_lapl_cache, 0, sizeof(H5CX_lapl_cache_t));

    /* Get the default LAPL cache information */

#ifdef H5_HAVE_PARALLEL
    /* Get the collective metadata read flag */
    if (H5P_get(lapl, H5_COLL_MD_READ_FLAG_NAME, &H5CX_def_lapl_cache.lapl_coll_md_read) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve collective metadata read flag property");
#endif /* H5_HAVE_PARALLEL */

    /* Get the prefix for external links */
    if (H5P_peek(lapl, H5L_ACS_ELINK_PREFIX_NAME, &H5CX_def_lapl_cache.elink_prefix) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve prefix for external links");

    /* Get the callback info for external links */
    if (H5P_get(lapl, H5L_ACS_ELINK_CB_NAME, &H5CX_def_lapl_cache.elink_cb_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve callback info for external links");

    /* Get the file access property list for external links */
    if (H5P_peek(lapl, H5L_ACS_ELINK_FAPL_NAME, &H5CX_def_lapl_cache.elink_fapl) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve file access property list for external links");

    /* Get the flags for external links */
    if (H5P_get(lapl, H5L_ACS_ELINK_FLAGS_NAME, &H5CX_def_lapl_cache.elink_flags) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve flags for external links");

    /* Get number of soft / UD links to traverse */
    if (H5P_get(lapl, H5L_ACS_NLINKS_NAME, &H5CX_def_lapl_cache.nlinks) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve number of soft / UD links to traverse");

    /* Reset the "default OCPL cache" information */
    memset(&H5CX_def_ocpl_cache, 0, sizeof(H5CX_ocpl_cache_t));

    /* Get the default OCPL cache information */

#ifdef H5O_ENABLE_BAD_MESG_COUNT
    /* Get the write a bad message count flag */
    if (H5P_get(ocpl, H5O_CRT_BAD_MESG_COUNT_NAME, &H5CX_def_ocpl_cache.bad_mesg_count) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve write bad message count");
#endif /* H5O_ENABLE_BAD_MESG_COUNT */

    /* Get maximum # of compact attributes */
    if (H5P_get(ocpl, H5O_CRT_ATTR_MAX_COMPACT_NAME, &H5CX_def_ocpl_cache.attr_max_compact) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve maximum # of compact attributes");

    /* Get minimum # of dense attributes */
    if (H5P_get(ocpl, H5O_CRT_ATTR_MIN_DENSE_NAME, &H5CX_def_ocpl_cache.attr_min_dense) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve minimum # of dense attributes");

    /* Get object header flags */
    if (H5P_get(ocpl, H5O_CRT_OHDR_FLAGS_NAME, &H5CX_def_ocpl_cache.ohdr_flags) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve object header flags");

    /* Get filter pipeline */
    if (H5P_get(ocpl, H5O_CRT_PIPELINE_NAME, &H5CX_def_ocpl_cache.pline) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve filter pipeline for object creation");

    /* Reset the "default OCPYPL cache" information */
    memset(&H5CX_def_ocpypl_cache, 0, sizeof(H5CX_ocpypl_cache_t));

    /* Get the default OCPYPL cache information */

    /* Get the committed datatype merge list for object copy */
    if (H5P_peek(ocpypl, H5O_CPY_MERGE_COMM_DT_LIST_NAME, &H5CX_def_ocpypl_cache.comm_dtype_merge_list) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve committed datatype merge list");

    /* Get the callback info for committed datatype search */
    if (H5P_get(ocpypl, H5O_CPY_MCDT_SEARCH_CB_NAME, &H5CX_def_ocpypl_cache.mcdt_cb_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve callback info for committed datatype search");

    /* Get the object copy options */
    if (H5P_get(ocpypl, H5O_CPY_OPTION_NAME, &H5CX_def_ocpypl_cache.cpy_options) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve object copy options");

    /* Reset the "default DCPL cache" information */
    memset(&H5CX_def_dcpl_cache, 0, sizeof(H5CX_dcpl_cache_t));

    /* Get the default DCPL cache information */

    /* Get flag to indicate whether to minimize dataset object header */
    if (H5P_get(dcpl, H5D_CRT_MIN_DSET_HDR_SIZE_NAME, &H5CX_def_dcpl_cache.min_dset_ohdr) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve dataset minimize flag");

    /* Get flag to indicate whether dataset allocation time state is set */
    if (H5P_get(dcpl, H5D_CRT_ALLOC_TIME_STATE_NAME, &H5CX_def_dcpl_cache.alloc_time_state) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve dataset allocation time state flag");

    /* Get storage layout */
    if (H5P_get(dcpl, H5D_CRT_LAYOUT_NAME, &H5CX_def_dcpl_cache.layout) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve storage layout for dataset creation");

    /* Get external file list */
    if (H5P_get(dcpl, H5D_CRT_EXT_FILE_LIST_NAME, &H5CX_def_dcpl_cache.efl) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve external file list for dataset creation");

    /* Get fill value */
    if (H5P_get(dcpl, H5D_CRT_FILL_VALUE_NAME, &H5CX_def_dcpl_cache.fill_value) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve fill value for dataset creation");

    /* Don't get bogus message ID and flags, they are inserted dynamically in the gen_bogus test */

    /* Reset the "default GCPL cache" information */
    memset(&H5CX_def_gcpl_cache, 0, sizeof(H5CX_gcpl_cache_t));

    /* Get the default GCPL cache information */

    /* Get the group info property */
    if (H5P_get(gcpl, H5G_CRT_GROUP_INFO_NAME, &H5CX_def_gcpl_cache.ginfo) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve group info property");

    /* Get the link info property */
    if (H5P_get(gcpl, H5G_CRT_LINK_INFO_NAME, &H5CX_def_gcpl_cache.linfo) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve link info property");

    /* Reset the "default ACPL cache" information */
    memset(&H5CX_def_acpl_cache, 0, sizeof(H5CX_acpl_cache_t));

    /* Get the default ACPL cache information */

    /* Get the attribute encoding property */
    if (H5P_get(acpl, H5P_STRCRT_CHAR_ENCODING_NAME, &H5CX_def_acpl_cache.attr_encoding) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve attribute encoding property");

    /* Reset the "default FCPL cache" information */
    memset(&H5CX_def_fcpl_cache, 0, sizeof(H5CX_fcpl_cache_t));

    /* Get the default FCPL cache information */

    /* Get the userblock size property */
    if (H5P_get(fcpl, H5F_CRT_USER_BLOCK_NAME, &H5CX_def_fcpl_cache.userblock_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve userblock size property");

    /* Get the size of address property */
    if (H5P_get(fcpl, H5F_CRT_ADDR_BYTE_NUM_NAME, &H5CX_def_fcpl_cache.sizeof_addr) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve size of addresses property");

    /* Get the size of size property */
    if (H5P_get(fcpl, H5F_CRT_OBJ_BYTE_NUM_NAME, &H5CX_def_fcpl_cache.sizeof_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve size of sizes property");

    /* Get the symbol table leaf node size property */
    if (H5P_get(fcpl, H5F_CRT_SYM_LEAF_NAME, &H5CX_def_fcpl_cache.sym_leaf_k) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve symbol table leaf node size property");

    /* Get the B-tree rank property */
    if (H5P_get(fcpl, H5F_CRT_BTREE_RANK_NAME, &H5CX_def_fcpl_cache.btree_k) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve B-tree rank property");

    /* Get the file space page size property */
    if (H5P_get(fcpl, H5F_CRT_FILE_SPACE_PAGE_SIZE_NAME, &H5CX_def_fcpl_cache.fs_page_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file space page size property");

    /* Get the file space strategy property */
    if (H5P_get(fcpl, H5F_CRT_FILE_SPACE_STRATEGY_NAME, &H5CX_def_fcpl_cache.fs_strategy) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file space strategy property");

    /* Get the file free space persist property */
    if (H5P_get(fcpl, H5F_CRT_FREE_SPACE_PERSIST_NAME, &H5CX_def_fcpl_cache.fs_persist) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file space persist property");

    /* Get the file free space threshold property */
    if (H5P_get(fcpl, H5F_CRT_FREE_SPACE_THRESHOLD_NAME, &H5CX_def_fcpl_cache.fs_threshold) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file free space threshold property");

    /* Get the number of SOHM indexes property */
    if (H5P_get(fcpl, H5F_CRT_SHMSG_NINDEXES_NAME, &H5CX_def_fcpl_cache.sohm_nindexes) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve number of SOHM indexes property");

    /* Get the SOHM btree minimum property */
    if (H5P_get(fcpl, H5F_CRT_SHMSG_BTREE_MIN_NAME, &H5CX_def_fcpl_cache.shmsg_btree_min) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve SOHM btree minimum property");

    /* Get the SOHM list max property */
    if (H5P_get(fcpl, H5F_CRT_SHMSG_LIST_MAX_NAME, &H5CX_def_fcpl_cache.shmsg_list_max) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve SOHM list max property");

    /* Get the SOHM index types property */
    if (H5P_get(fcpl, H5F_CRT_SHMSG_INDEX_TYPES_NAME, &H5CX_def_fcpl_cache.shmsg_index_types) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve SOHM index types property");

    /* Get the SOHM index min sizes property */
    if (H5P_get(fcpl, H5F_CRT_SHMSG_INDEX_MINSIZE_NAME, &H5CX_def_fcpl_cache.shmsg_index_min_sizes) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve SOHM index min sizes property");

    /* Reset the "default DAPL cache" information */
    memset(&H5CX_def_dapl_cache, 0, sizeof(H5CX_dapl_cache_t));

    /* Get the default DAPL cache information */

    /* Get the prefix for the external file */
    if (H5P_peek(dapl, H5D_ACS_EFILE_PREFIX_NAME, &H5CX_def_dapl_cache.extfile_prefix) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve prefix for external file");

    /* Get the prefix for the VDS file */
    if (H5P_peek(dapl, H5D_ACS_VDS_PREFIX_NAME, &H5CX_def_dapl_cache.vds_prefix) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve prefix for VDS");

    /* Get the append flush property */
    if (H5P_get(dapl, H5D_ACS_APPEND_FLUSH_NAME, &H5CX_def_dapl_cache.append_flush) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve append flush property");

    /* Get the number of slots in the raw data cache */
    if (H5P_get(dapl, H5D_ACS_DATA_CACHE_NUM_SLOTS_NAME, &H5CX_def_dapl_cache.dapl_rdcc_nslots) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve raw data cache number of slots property");

    /* Get the size of the raw data cache */
    if (H5P_get(dapl, H5D_ACS_DATA_CACHE_BYTE_SIZE_NAME, &H5CX_def_dapl_cache.dapl_rdcc_nbytes) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve raw data cache byte size property");

    /* Get the chunk cache preemption factor */
    if (H5P_get(dapl, H5D_ACS_PREEMPT_READ_CHUNKS_NAME, &H5CX_def_dapl_cache.dapl_rdcc_w0) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve raw data cache preemption factor property");

    /* Get the VDS printf gap */
    if (H5P_get(dapl, H5D_ACS_VDS_PRINTF_GAP_NAME, &H5CX_def_dapl_cache.vds_printf_gap) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VDS printf gap property");

    /* Get the VDS view */
    if (H5P_get(dapl, H5D_ACS_VDS_VIEW_NAME, &H5CX_def_dapl_cache.vds_view) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VDS view property");

    /* Reset the "default FAPL cache" information */
    memset(&H5CX_def_fapl_cache, 0, sizeof(H5CX_fapl_cache_t));

    /* Get the default FAPL cache information */

#ifdef H5_HAVE_PARALLEL
    /* Get MPI communicator */
    if (H5P_get(fapl, H5F_ACS_MPI_PARAMS_COMM_NAME, &H5CX_def_fapl_cache.mpi_comm) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve MPI communicator");

    /* Get MPI info */
    if (H5P_get(fapl, H5F_ACS_MPI_PARAMS_INFO_NAME, &H5CX_def_fapl_cache.mpi_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve MPI info");

    /* Get the collective metadata read flag */
    if (H5P_get(fapl, H5_COLL_MD_READ_FLAG_NAME, &H5CX_def_fapl_cache.fapl_coll_md_read) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve collective metadata read flag property");

    /* Get the collective metadata write flag */
    if (H5P_get(fapl, H5F_ACS_COLL_MD_WRITE_FLAG_NAME, &H5CX_def_fapl_cache.coll_md_write) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve collective metadata write flag property");

#ifdef H5_HAVE_SUBFILING_VFD
    /* Get the subfiling IOC parameters */
    if (H5P_get(fapl, H5F_ACS_SUBFILING_CONFIG_PROP_NAME, &H5CX_def_fapl_cache.sf_ioc_params) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve subfiling IOC parameters");
#endif /* H5_HAVE_SUBFILING_VFD */
#endif /* H5_HAVE_PARALLEL */

    /* Get file image info */
    if (H5P_get(fapl, H5F_ACS_FILE_IMAGE_INFO_NAME, &H5CX_def_fapl_cache.file_image_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file image info");

    /* Get file format lower & upper bounds */
    if (H5P_get(fapl, H5F_ACS_LIBVER_LOW_BOUND_NAME, &H5CX_def_fapl_cache.low_bound) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file format lower bound");
    if (H5P_get(fapl, H5F_ACS_LIBVER_HIGH_BOUND_NAME, &H5CX_def_fapl_cache.high_bound) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file format upper bound");

    /* Get file locking properties */
    if (H5P_get(fapl, H5F_ACS_USE_FILE_LOCKING_NAME, &H5CX_def_fapl_cache.use_file_locking) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve use file locking flag");
    if (H5P_get(fapl, H5F_ACS_IGNORE_DISABLED_FILE_LOCKS_NAME, &H5CX_def_fapl_cache.ignore_disabled_locks) <
        0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve ignore disabled file locks flag");

    /* Get alignment properties */
    if (H5P_get(fapl, H5F_ACS_ALIGN_NAME, &H5CX_def_fapl_cache.align_bound) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve alignment bound");
    if (H5P_get(fapl, H5F_ACS_ALIGN_THRHD_NAME, &H5CX_def_fapl_cache.align_threshold) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve alignment threshold");

    /* Get clear status flags property */
    if (H5P_get(fapl, H5F_ACS_CLEAR_STATUS_FLAGS_NAME, &H5CX_def_fapl_cache.clear_status_flags) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve clear status flags property");

    /* Get the garbage collection reference property */
    if (H5P_get(fapl, H5F_ACS_GARBG_COLCT_REF_NAME, &H5CX_def_fapl_cache.gc_ref) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve garbage collection reference property");

    /* Get the use metadata cache logging property */
    if (H5P_get(fapl, H5F_ACS_USE_MDC_LOGGING_NAME, &H5CX_def_fapl_cache.use_mdc_logging) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve use metadata cache logging property");

    /* Get the metadata cache log location property */
    if (H5P_peek(fapl, H5F_ACS_MDC_LOG_LOCATION_NAME, &H5CX_def_fapl_cache.mdc_log_location) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve metadata cache log location property");

    /* Get the start metadata cache logging on access property */
    if (H5P_get(fapl, H5F_ACS_START_MDC_LOG_ON_ACCESS_NAME,
                &H5CX_def_fapl_cache.start_mdc_logging_on_access) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve start metadata cache logging on access property");

    /* Get the metadata cache read attempts property */
    if (H5P_get(fapl, H5F_ACS_METADATA_READ_ATTEMPTS_NAME, &H5CX_def_fapl_cache.mdc_read_attempts) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve metadata cache read attempts property");

    /* Get the metadata allocation block size property */
    if (H5P_get(fapl, H5F_ACS_META_BLOCK_SIZE_NAME, &H5CX_def_fapl_cache.meta_alloc_block_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve metadata allocation block size property");

    /* Get the metadata cache initialization configuration property */
    if (H5P_get(fapl, H5F_ACS_META_CACHE_INIT_CONFIG_NAME, &H5CX_def_fapl_cache.mdc_init_config) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve metadata cache initial configuration property");

    /* Get the metadata cache image initial configuration property */
    if (H5P_get(fapl, H5F_ACS_META_CACHE_INIT_IMAGE_CONFIG_NAME, &H5CX_def_fapl_cache.mdc_image_config) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve metadata cache image initial configuration property");

    /* Get the object flush strategy property */
    if (H5P_get(fapl, H5F_ACS_OBJECT_FLUSH_CB_NAME, &H5CX_def_fapl_cache.object_flush_strategy) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve object flush strategy property");

    /* Get the page buffer properties */
    if (H5P_get(fapl, H5F_ACS_PAGE_BUFFER_SIZE_NAME, &H5CX_def_fapl_cache.pb_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve page buffer size property");
    if (H5P_get(fapl, H5F_ACS_PAGE_BUFFER_MIN_META_PERC_NAME, &H5CX_def_fapl_cache.pb_min_meta_perc) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve minimum metadata percentage property");
    if (H5P_get(fapl, H5F_ACS_PAGE_BUFFER_MIN_RAW_PERC_NAME, &H5CX_def_fapl_cache.pb_min_raw_perc) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve minimum raw percentage property");

    /* Get the raw data chunk cache properties */
    if (H5P_get(fapl, H5F_ACS_DATA_CACHE_NUM_SLOTS_NAME, &H5CX_def_fapl_cache.fapl_rdcc_nslots) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve raw data cache number of slots property");
    if (H5P_get(fapl, H5F_ACS_DATA_CACHE_BYTE_SIZE_NAME, &H5CX_def_fapl_cache.fapl_rdcc_nbytes) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve raw data cache byte size property");
    if (H5P_get(fapl, H5F_ACS_PREEMPT_READ_CHUNKS_NAME, &H5CX_def_fapl_cache.fapl_rdcc_w0) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL,
                    "Can't retrieve raw data cache preemption factor property");

    /* Get the size of the external link file cache */
    if (H5P_get(fapl, H5F_ACS_EFC_SIZE_NAME, &H5CX_def_fapl_cache.efc_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve external file cache size property");

    /* Get the file close degree property */
    if (H5P_get(fapl, H5F_ACS_CLOSE_DEGREE_NAME, &H5CX_def_fapl_cache.close_degree) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve file close degree property");

    /* Get the evict on close property */
    if (H5P_get(fapl, H5F_ACS_EVICT_ON_CLOSE_FLAG_NAME, &H5CX_def_fapl_cache.evict_on_close) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve evict on close property");

    /* Get the relaxed file integrity checks property */
    if (H5P_get(fapl, H5F_ACS_RFIC_FLAGS_NAME, &H5CX_def_fapl_cache.rfic_flags) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve relaxed file integrity checks property");

    /* Get the "small" raw data block size property */
    if (H5P_get(fapl, H5F_ACS_SDATA_BLOCK_SIZE_NAME, &H5CX_def_fapl_cache.sdata_block_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve 'small' raw data block size property");

    /* Get the sieve buffer size property */
    if (H5P_get(fapl, H5F_ACS_SIEVE_BUF_SIZE_NAME, &H5CX_def_fapl_cache.sieve_buf_size) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve sieve buffer size property");

    /* Get the null file space map address property */
    if (H5P_get(fapl, H5F_ACS_NULL_FSM_ADDR_NAME, &H5CX_def_fapl_cache.null_fsm_addr) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve null file space map address property");

    /* Get the skip EOF check property */
    if (H5P_get(fapl, H5F_ACS_SKIP_EOF_CHECK_NAME, &H5CX_def_fapl_cache.skip_eof_check) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve skip EOF check property");

    /* Get the family to single file property */
    if (H5P_get(fapl, H5F_ACS_FAMILY_TO_SINGLE_NAME, &H5CX_def_fapl_cache.fam_to_single) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve family to single file property");

    /* Get the family offset property */
    if (H5P_get(fapl, H5F_ACS_FAMILY_OFFSET_NAME, &H5CX_def_fapl_cache.fam_offset) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve family offset property");

    /* Get the size of the new family file */
    if (H5P_get(fapl, H5F_ACS_FAMILY_NEWSIZE_NAME, &H5CX_def_fapl_cache.fam_newsize) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve family new size property");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX__init_package() */

/*-------------------------------------------------------------------------
 * Function:	H5CX_init_phase2
 *
 * Purpose:     Initialize interface-specific information
 *
 * Return:      Success:    Non-negative
 *              Failure:    Negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_init_phase2(void)
{
    H5P_genplist_t *fapl      = H5P_LST_FILE_ACCESS_g; /* File access property list */
    herr_t          ret_value = SUCCEED;               /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Get the rest of the default FAPL cache information */

    /* Get VOL connector & info */
    if (H5P_peek(fapl, H5F_ACS_VOL_CONN_NAME, &H5CX_def_fapl_cache.vol_connector_prop) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get VOL connector info");

    if (H5CX_def_fapl_cache.vol_connector_prop.connector) {
        /* Copy connector info, if it exists */
        if (H5CX_def_fapl_cache.vol_connector_prop.connector_info) {
            void *new_connector_info = NULL; /* Copy of connector info */

            /* Allocate and copy connector info */
            if (H5VL_copy_connector_info(H5CX_def_fapl_cache.vol_connector_prop.connector,
                                         &new_connector_info,
                                         H5CX_def_fapl_cache.vol_connector_prop.connector_info) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "connector info copy failed");
            H5CX_def_fapl_cache.vol_connector_prop.connector_info = new_connector_info;
        } /* end if */

        /* Increment the refcount on the connector */
        if (H5VL_conn_inc_rc(H5CX_def_fapl_cache.vol_connector_prop.connector) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTINC, FAIL, "incrementing VOL connector refcount failed");
    } /* end if */

    /* Get driver & info */
    if (H5P_peek(fapl, H5F_ACS_FILE_DRV_NAME, &H5CX_def_fapl_cache.driver_prop) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get VOL connector info");

    /* Clone the VFD driver, info, etc.*/
    if (H5CX_def_fapl_cache.driver_prop.driver)
        if (H5FD_driver_prop_clone(&H5CX_def_fapl_cache.driver_prop) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't clone driver property");

    /* Mark "top" of interface as initialized */
    H5CX_top_package_initialize_s = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_init_phase2() */

/*-------------------------------------------------------------------------
 * Function: H5CX_top_term_package
 *
 * Purpose:  Close components from other packages
 *
 * Return:   Success:    Positive if anything was done that might
 *                affect other interfaces; zero otherwise.
 *            Failure:    Negative.
 *
 *-------------------------------------------------------------------------
 */
int
H5CX_top_term_package(void)
{
    int n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (H5CX_top_package_initialize_s) {
        /* Release the VOL connector property, if it was set */
        if (H5CX_def_fapl_cache.vol_connector_prop.connector) {
            /* Clean up any VOL connector info */
            if (H5CX_def_fapl_cache.vol_connector_prop.connector_info) {
                (void)H5VL_free_connector_info(H5CX_def_fapl_cache.vol_connector_prop.connector,
                                               H5CX_def_fapl_cache.vol_connector_prop.connector_info);
                H5CX_def_fapl_cache.vol_connector_prop.connector_info = NULL;
            }

            /* Decrement connector refcount */
            (void)H5VL_conn_dec_rc(H5CX_def_fapl_cache.vol_connector_prop.connector);
            H5CX_def_fapl_cache.vol_connector_prop.connector = NULL;

            n++; /*H5VL*/
        }        /* end if */

        /* Release the VFD property, if it was set */
        if (H5CX_def_fapl_cache.driver_prop.driver) {
            /* Free the file driver & info */
            (void)H5FD_driver_prop_free(&H5CX_def_fapl_cache.driver_prop);

            n++; /*H5FD*/
        }        /* end if */

        /* Mark closed */
        if (0 == n)
            H5CX_top_package_initialize_s = false;
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5CX_top_term_package() */

/*-------------------------------------------------------------------------
 * Function: H5CX_term_package
 *
 * Purpose:  Terminate this interface.
 *
 * Return:   Success:    Positive if anything was done that might
 *                affect other interfaces; zero otherwise.
 *            Failure:    Negative.
 *
 *-------------------------------------------------------------------------
 */
int
H5CX_term_package(void)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (H5_PKG_INIT_VAR) {
        H5CX_node_t **head = NULL; /* Pointer to head of API context list */

        /* Sanity checks */
        assert(false == H5CX_top_package_initialize_s);

        /* Get the pointer to the head of the API context, for this thread */
        head = H5CX_get_my_context();
        assert(head);

        /* Reset head of context list */
        *head = NULL;

        H5_PKG_INIT_VAR = false;
    } /* end if */

    FUNC_LEAVE_NOAPI(0)
} /* end H5CX_term_package() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_pushed
 *
 * Purpose:     Returns whether or not an API context has been pushed.
 *
 * Return:      true/false
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_pushed(void)
{
    H5CX_node_t **head      = NULL;  /* Pointer to head of API context list */
    bool          is_pushed = false; /* Flag to indicate context is pushed */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head);

    /* Set return value */
    is_pushed = (*head != NULL);

    FUNC_LEAVE_NOAPI(is_pushed)
}

/*-------------------------------------------------------------------------
 * Function:    H5CX_push
 *
 * Purpose:     Pushes a context for an API call.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_push(H5CX_node_t *cnode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(cnode);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head);

    /* Set non-zero context info */
    cnode->ctx.dxpl_id   = H5P_DATASET_XFER_DEFAULT;
    cnode->ctx.ocpl_id   = H5P_OBJECT_CREATE_DEFAULT;
    cnode->ctx.ocpypl_id = H5P_OBJECT_COPY_DEFAULT;
    cnode->ctx.dapl_id   = H5P_DATASET_ACCESS_DEFAULT;
    cnode->ctx.lcpl_id   = H5P_LINK_CREATE_DEFAULT;
    cnode->ctx.lapl_id   = H5P_LINK_ACCESS_DEFAULT;
    cnode->ctx.fapl_id   = H5P_FILE_ACCESS_DEFAULT;
    cnode->ctx.acpl_id   = H5P_ATTRIBUTE_CREATE_DEFAULT;
    cnode->ctx.tag       = H5AC__INVALID_TAG;
    cnode->ctx.ring      = H5AC_RING_USER;

#ifdef H5_HAVE_PARALLEL
    cnode->ctx.btype = MPI_BYTE;
    cnode->ctx.ftype = MPI_BYTE;
#ifdef H5_HAVE_SUBFILING_VFD
    cnode->ctx.sf_stub_file_id = H5FD_SUBFILING_BAD_FILE_ID;
#endif
#endif

    /* Push context node onto stack */
    cnode->next = *head;
    *head       = cnode;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_push() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_retrieve_state
 *
 * Purpose:     Retrieve the state of an API context, for later resumption.
 *
 * Note:	This routine _only_ tracks the state of API context information
 *		set before the VOL callback is invoked, not values that are
 *		set internal to the library.  It's main purpose is to provide
 *		API context state to VOL connectors.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_retrieve_state(H5CX_state_t **api_state)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(api_state);

    /* Allocate & clear API context state */
    if (NULL == (*api_state = H5FL_CALLOC(H5CX_state_t)))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTALLOC, FAIL, "unable to allocate new API context state");

    /* Check for non-default OCPL */
    if (H5P_OBJECT_CREATE_DEFAULT != (*head)->ctx.ocpl_id) {
        /* Retrieve the OCPL property list */
        H5CX_RETRIEVE_PLIST(ocpl, FAIL)

        /* Copy the OCPL ID */
        if (((*api_state)->ocpl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.ocpl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->ocpl_id = H5P_OBJECT_CREATE_DEFAULT;

    /* Check for non-default OCPYPL */
    if (H5P_OBJECT_COPY_DEFAULT != (*head)->ctx.ocpypl_id) {
        /* Retrieve the OCPYPL property list */
        H5CX_RETRIEVE_PLIST(ocpypl, FAIL)

        /* Copy the OCPYPL ID */
        if (((*api_state)->ocpypl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.ocpypl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->ocpypl_id = H5P_OBJECT_COPY_DEFAULT;

    /* Check for non-default DXPL */
    if (H5P_DATASET_XFER_DEFAULT != (*head)->ctx.dxpl_id) {
        /* Retrieve the DXPL property list */
        H5CX_RETRIEVE_PLIST(dxpl, FAIL)

        /* Copy the DXPL ID */
        if (((*api_state)->dxpl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.dxpl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->dxpl_id = H5P_DATASET_XFER_DEFAULT;

    /* Check for non-default FAPL */
    if (H5P_FILE_ACCESS_DEFAULT != (*head)->ctx.fapl_id) {
        /* Retrieve the FAPL property list */
        H5CX_RETRIEVE_PLIST(fapl, FAIL)

        /* Copy the FAPL ID */
        if (((*api_state)->fapl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.fapl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->fapl_id = H5P_FILE_ACCESS_DEFAULT;

    /* Check for non-default LAPL */
    if (H5P_LINK_ACCESS_DEFAULT != (*head)->ctx.lapl_id) {
        /* Retrieve the LAPL property list */
        H5CX_RETRIEVE_PLIST(lapl, FAIL)

        /* Copy the LAPL ID */
        if (((*api_state)->lapl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.lapl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->lapl_id = H5P_LINK_ACCESS_DEFAULT;

    /* Check for non-default LCPL */
    if (H5P_LINK_CREATE_DEFAULT != (*head)->ctx.lcpl_id) {
        /* Retrieve the LCPL property list */
        H5CX_RETRIEVE_PLIST(lcpl, FAIL)

        /* Copy the LCPL ID */
        if (((*api_state)->lcpl_id = H5P_copy_plist_id((H5P_genplist_t *)(*head)->ctx.lcpl, false)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy property list");
    } /* end if */
    else
        (*api_state)->lcpl_id = H5P_LINK_CREATE_DEFAULT;

    /* Keep a reference to the current VOL wrapping context */
    (*api_state)->vol_wrap_ctx = (*head)->ctx.vol_wrap_ctx;
    if (NULL != (*api_state)->vol_wrap_ctx) {
        assert((*head)->ctx.vol_wrap_ctx_valid);
        if (H5VL_inc_vol_wrapper((*api_state)->vol_wrap_ctx) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTINC, FAIL, "can't increment refcount on VOL wrapping context");
    } /* end if */

#ifdef H5_HAVE_PARALLEL
    /* Save parallel I/O settings */
    (*api_state)->coll_metadata_read = (*head)->ctx.coll_metadata_read;
#endif /* H5_HAVE_PARALLEL */

done:
    /* Cleanup on error */
    if (ret_value < 0) {
        if (*api_state) {
            /* Release the (possibly partially allocated) API state struct */
            if (H5CX_free_state(*api_state) < 0)
                HDONE_ERROR(H5E_CONTEXT, H5E_CANTRELEASE, FAIL, "unable to release API state");
            *api_state = NULL;
        } /* end if */
    }     /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_retrieve_state() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_restore_state
 *
 * Purpose:     Restore an API context, from a previously retrieved state.
 *
 * Note:	This routine _only_ resets the state of API context information
 *		set before the VOL callback is invoked, not values that are
 *		set internal to the library.  It's main purpose is to restore
 *		API context state from VOL connectors.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_restore_state(const H5CX_state_t *api_state)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(api_state);

    /* Restore the OCPL info */
    (*head)->ctx.ocpl_id = api_state->ocpl_id;
    (*head)->ctx.ocpl    = NULL;

    /* Restore the OCPYPL info */
    (*head)->ctx.ocpypl_id = api_state->ocpypl_id;
    (*head)->ctx.ocpypl    = NULL;

    /* Restore the DXPL info */
    (*head)->ctx.dxpl_id = api_state->dxpl_id;
    (*head)->ctx.dxpl    = NULL;

    /* Restore the FAPL info */
    (*head)->ctx.fapl_id = api_state->fapl_id;
    (*head)->ctx.fapl    = NULL;

    /* Restore the LAPL info */
    (*head)->ctx.lapl_id = api_state->lapl_id;
    (*head)->ctx.lapl    = NULL;

    /* Restore the LCPL info */
    (*head)->ctx.lcpl_id = api_state->lcpl_id;
    (*head)->ctx.lcpl    = NULL;

    /* Restore the VOL wrapper context */
    (*head)->ctx.vol_wrap_ctx = api_state->vol_wrap_ctx;
    if (NULL != (*head)->ctx.vol_wrap_ctx)
        (*head)->ctx.vol_wrap_ctx_valid = true;

#ifdef H5_HAVE_PARALLEL
    /* Restore parallel I/O settings */
    (*head)->ctx.coll_metadata_read = api_state->coll_metadata_read;
#endif /* H5_HAVE_PARALLEL */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5CX_restore_state() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_free_state
 *
 * Purpose:     Free a previously retrieved API context state
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_free_state(H5CX_state_t *api_state)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(api_state);

    /* Release the OCPL */
    if (0 != api_state->ocpl_id && H5P_OBJECT_CREATE_DEFAULT != api_state->ocpl_id)
        if (H5I_dec_ref(api_state->ocpl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on OCPL");

    /* Release the OCPYPL */
    if (0 != api_state->ocpypl_id && H5P_OBJECT_COPY_DEFAULT != api_state->ocpypl_id)
        if (H5I_dec_ref(api_state->ocpypl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on OCPYPL");

    /* Release the DXPL */
    if (0 != api_state->dxpl_id && H5P_DATASET_XFER_DEFAULT != api_state->dxpl_id)
        if (H5I_dec_ref(api_state->dxpl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on DXPL");

    /* Release the FAPL */
    if (0 != api_state->fapl_id && H5P_FILE_ACCESS_DEFAULT != api_state->fapl_id)
        if (H5I_dec_ref(api_state->fapl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on FAPL");

    /* Release the LAPL */
    if (0 != api_state->lapl_id && H5P_LINK_ACCESS_DEFAULT != api_state->lapl_id)
        if (H5I_dec_ref(api_state->lapl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on LAPL");

    /* Release the LCPL */
    if (0 != api_state->lcpl_id && H5P_LINK_CREATE_DEFAULT != api_state->lcpl_id)
        if (H5I_dec_ref(api_state->lcpl_id) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on LCPL");

    /* Release the VOL wrapper context */
    if (api_state->vol_wrap_ctx)
        if (H5VL_dec_vol_wrapper(api_state->vol_wrap_ctx) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTDEC, FAIL, "can't decrement refcount on VOL wrapping context");

    /* Free the state */
    api_state = H5FL_FREE(H5CX_state_t, api_state);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_free_state() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_is_def_dxpl
 *
 * Purpose:     Checks if the API context is using the library's default DXPL
 *
 * Return:      true / false (can't fail)
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_is_def_dxpl(void)
{
    H5CX_node_t **head        = NULL;  /* Pointer to head of API context list */
    bool          is_def_dxpl = false; /* Flag to indicate DXPL is default */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    is_def_dxpl = ((*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT);

    FUNC_LEAVE_NOAPI(is_def_dxpl)
} /* end H5CX_is_def_dxpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_dxpl
 *
 * Purpose:     Resets the cached DXPL info for the current API call context.
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_dxpl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset the DXPL flags to force the properties to be retrieved again */
    memset(&head->ctx.dxpl_flags, 0, sizeof(head->ctx.dxpl_flags));

    /* Retrieve the DXPL pointer again also */
    head->ctx.dxpl    = NULL;
    head->ctx.dxpl_id = H5P_DATASET_XFER_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_dxpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_dxpl
 *
 * Purpose:     Sets the DXPL for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_dxpl(hid_t dxpl_id)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != dxpl_id);

    /* Reset the cached data */
    H5CX__reset_dxpl(*head);

    /* Set the API context's DXPL to a new value */
    (*head)->ctx.dxpl_id = dxpl_id;

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_dxpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_libver_bounds
 *
 * Purpose:     Sets the low/high bounds according to "f" for the current API call context.
 *              When "f" is NULL, the low/high bounds are set to latest format.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_libver_bounds(H5F_t *f)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.fapl_props.low_bound  = (f == NULL) ? H5F_LIBVER_LATEST : H5F_LOW_BOUND(f);
    (*head)->ctx.fapl_props.high_bound = (f == NULL) ? H5F_LIBVER_LATEST : H5F_HIGH_BOUND(f);

    /* Mark the values as valid */
    (*head)->ctx.fapl_flags.low_bound_valid  = true;
    (*head)->ctx.fapl_flags.high_bound_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_libver_bounds() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_lcpl
 *
 * Purpose:     Resets the cached LCPL info for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_lcpl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset the LCPL flags to force the properties to be retrieved again */
    memset(&head->ctx.lcpl_flags, 0, sizeof(head->ctx.lcpl_flags));

    /* Retrieve the LCPL pointer again also */
    head->ctx.lcpl    = NULL;
    head->ctx.lcpl_id = H5P_LINK_CREATE_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_lcpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_lcpl
 *
 * Purpose:     Sets the LCPL for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_lcpl(hid_t lcpl_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != lcpl_id);

    /* Reset the cached data */
    H5CX__reset_lcpl(*head);

    /* Set the API context's LCPL to a new value */
    (*head)->ctx.lcpl_id = lcpl_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_lcpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_acpl
 *
 * Purpose:     Resets the cached ACPL info for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_acpl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset the ACPL flags to force the properties to be retrieved again */
    memset(&head->ctx.acpl_flags, 0, sizeof(head->ctx.acpl_flags));

    /* Retrieve the ACPL pointer again also */
    head->ctx.acpl    = NULL;
    head->ctx.acpl_id = H5P_ATTRIBUTE_CREATE_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_acpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_acpl
 *
 * Purpose:     Sets the ACPL for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_acpl(hid_t acpl_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != acpl_id);

    /* Reset the cached data */
    H5CX__reset_acpl(*head);

    /* Set the API context's ACPL to a new value */
    (*head)->ctx.acpl_id = acpl_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_acpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_cpl
 *
 * Purpose:     Validates and sets a creation property list
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_cpl(hid_t crtpl_id)
{
    H5CX_node_t   **head  = NULL; /* Pointer to head of API context list */
    H5P_genplist_t *crtpl = NULL; /* Property list for the ID */
    htri_t is_dcpl = false; /* Whether the creation property list is (or is derived from) a dataset creation
                               property list */
    htri_t is_fcpl =
        false; /* Whether the creation property list is (or is derived from) a file creation property list */
    htri_t is_gcpl =
        false; /* Whether the creation property list is (or is derived from) a group creation property list */
    htri_t is_tcpl = false; /* Whether the creation property list is (or is derived from) a datatype creation
                               property list */
    htri_t is_ocpl = false; /* Whether the creation property list is (or is derived from) an object creation
                               property list */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != crtpl_id);

    /* Get the property list for the ID */
    if (NULL == (crtpl = H5I_object_verify(crtpl_id, H5I_GENPROP_LST)))
        HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL, "invalid ID for property list");

    /* Check for dataset creation property */
    if ((is_dcpl = H5P_class_isa(H5P_CLASS(crtpl), H5P_CLS_DATASET_CREATE_g)) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for dataset creation class");
    else if (!is_dcpl) {
        /* Check for file creation property */
        /* (Must be before group creation property, as the file creation property is a sub-class of it) */
        if ((is_fcpl = H5P_class_isa(H5P_CLASS(crtpl), H5P_CLS_FILE_CREATE_g)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for file creation class");
        else if (!is_fcpl) {
            /* Check for group creation property */
            if ((is_gcpl = H5P_class_isa(H5P_CLASS(crtpl), H5P_CLS_GROUP_CREATE_g)) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for group creation class");
            else if (!is_gcpl) {
                /* Check for datatype creation property */
                if ((is_tcpl = H5P_class_isa(H5P_CLASS(crtpl), H5P_CLS_DATATYPE_CREATE_g)) < 0)
                    HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for datatype creation class");
                else if (!is_tcpl) {
                    /* Check for object creation property */
                    /* (Must be last, as other object creation property classes are
                     *      sub-classes of it)
                     */
                    if ((is_ocpl = H5P_class_isa(H5P_CLASS(crtpl), H5P_CLS_OBJECT_CREATE_g)) < 0)
                        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for object creation class");
                    else if (!is_ocpl)
                        HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL, "unknown creation property class");
                }
            }
        }
    }
    assert(is_dcpl || is_fcpl || is_gcpl || is_tcpl || is_ocpl);

    /* Reset any cached data */
    H5CX__reset_ocpl(*head);

    /* Set object creation property list ID */
    (*head)->ctx.ocpl_id = crtpl_id;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_cpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_apl
 *
 * Purpose:     Validates an access property list, and sanity checking &
 *              setting up collective operations.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_apl(hid_t *acspl_id,
             hid_t
#ifndef H5_HAVE_PARALLEL
                 H5_ATTR_UNUSED
#endif /* H5_HAVE_PARALLEL */
                     loc_id,
             bool
#ifndef H5_HAVE_PARALLEL
                 H5_ATTR_UNUSED
#endif /* H5_HAVE_PARALLEL */
                     is_collective)
{
    H5CX_node_t   **head  = NULL; /* Pointer to head of API context list */
    H5P_genplist_t *acspl = NULL; /* Property list for the ID */
    htri_t          is_lapl =
        false; /* Whether the access property list is (or is derived from) a link access property list */
#ifdef H5_HAVE_PARALLEL
    bool is_default = false;    /* Whether the access property list is the default */
#endif                          /* H5_HAVE_PARALLEL */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(acspl_id);
    assert(H5P_DEFAULT != *acspl_id);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Check for link access property and set API context if so */
    if (H5P_LST_LINK_ACCESS_ID_g == *acspl_id || H5P_LST_DATASET_ACCESS_ID_g == *acspl_id ||
        H5P_LST_ATTRIBUTE_ACCESS_ID_g == *acspl_id || H5P_LST_DATATYPE_ACCESS_ID_g == *acspl_id ||
        H5P_LST_GROUP_ACCESS_ID_g == *acspl_id || H5P_LST_MAP_ACCESS_ID_g == *acspl_id) {
#ifdef H5_HAVE_PARALLEL
        is_default = true;
#endif /* H5_HAVE_PARALLEL */
        H5CX__reset_lapl(*head);
        (*head)->ctx.lapl_id = *acspl_id;
    }
    else {
        /* Get the property list for the ID */
        if (NULL == (acspl = H5I_object_verify(*acspl_id, H5I_GENPROP_LST)))
            HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL, "invalid ID for property list");

        /* Check for link access property */
        if ((is_lapl = H5P_class_isa(H5P_CLASS(acspl), H5P_CLS_LINK_ACCESS_g)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for link access class");
        else if (is_lapl) {
            H5CX__reset_lapl(*head);
            (*head)->ctx.lapl_id = *acspl_id;
        }
    }

    /* Check for dataset access property and set API context if so */
    /* Note: DAPL's are a subclass of LAPLs, so this might set both the lapl_id and the dapl_id */
    if (H5P_LST_DATASET_ACCESS_ID_g == *acspl_id) {
#ifdef H5_HAVE_PARALLEL
        is_default = true;
#endif /* H5_HAVE_PARALLEL */
        H5CX__reset_dapl(*head);
        (*head)->ctx.dapl_id = *acspl_id;
    }
    else {
        htri_t is_dapl; /* Whether the access property list is (or is derived from) a dataset access property
                           list */

        /* Get the property list for the ID, if we don't have it already */
        if (!acspl && NULL == (acspl = H5I_object_verify(*acspl_id, H5I_GENPROP_LST)))
            HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL, "invalid ID for property list");

        if ((is_dapl = H5P_class_isa(H5P_CLASS(acspl), H5P_CLS_DATASET_ACCESS_g)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for dataset access class");
        else if (is_dapl) {
            H5CX__reset_dapl(*head);
            (*head)->ctx.dapl_id = *acspl_id;
        }
    }

    /* Check for file access property and set API context if so */
    if (H5P_LST_FILE_ACCESS_ID_g == *acspl_id) {
#ifdef H5_HAVE_PARALLEL
        is_default = true;
#endif /* H5_HAVE_PARALLEL */
        H5CX__reset_fapl(*head);
        (*head)->ctx.fapl_id = *acspl_id;
    }
    else {
        htri_t is_fapl; /* Whether the access property list is (or is derived from) a file access property
                           list */

        /* Get the property list for the ID, if we don't have it already */
        if (!acspl && NULL == (acspl = H5I_object_verify(*acspl_id, H5I_GENPROP_LST)))
            HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL, "invalid ID for property list");

        if ((is_fapl = H5P_class_isa(H5P_CLASS(acspl), H5P_CLS_FILE_ACCESS_g)) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't check for file access class");
        else if (is_fapl) {
            H5CX__reset_fapl(*head);
            (*head)->ctx.fapl_id = *acspl_id;
        }
    }

#ifdef H5_HAVE_PARALLEL
    /* If this routine is not guaranteed to be collective (i.e. it doesn't
     * modify the structural metadata in a file), check if the application
     * specified a collective metadata read for just this operation.
     */
    if (!is_collective && !is_default) {
        H5P_coll_md_read_flag_t md_coll_read; /* Collective metadata read flag */

        /* LAPL's (and their derived subclasses) take precedence over FAPL's */
        if (is_lapl) {
            if (H5CX_get_lapl_coll_md_read(&md_coll_read) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get LAPL's collective metadata read flag");
        }
        else {
            if (H5CX_get_fapl_coll_md_read(&md_coll_read) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get FAPL's collective metadata read flag");
        }

        /* If collective metadata read requested, set collective metadata read flag */
        if (H5P_USER_TRUE == md_coll_read)
            is_collective = true;
    } /* end if */

    /* Check for collective operation */
    if (is_collective) {
        /* Set collective metadata read flag */
        (*head)->ctx.coll_metadata_read = true;

        /* If parallel is enabled and the file driver used is the MPI-IO
         * VFD, issue an MPI barrier for easier debugging if the API function
         * calling this is supposed to be called collectively.
         */
        if (H5_coll_api_sanity_check_g) {
            MPI_Comm mpi_comm; /* File communicator */

            /* Retrieve the MPI communicator from the loc_id or the fapl_id
             * just pushed in the API context.
             */
            if (H5F_mpi_retrieve_comm(loc_id, &mpi_comm) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get MPI communicator");

            /* issue the barrier */
            if (mpi_comm != MPI_COMM_NULL)
                MPI_Barrier(mpi_comm);
        } /* end if */
    }     /* end if */
#endif    /* H5_HAVE_PARALLEL */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_apl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_fapl
 *
 * Purpose:     Resets the cached FAPL info for the current API call context.
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
void
H5CX__reset_fapl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset cached FAPL data */
#ifdef H5_HAVE_PARALLEL
    if (head->ctx.fapl_flags.mpi_comm_valid)
        (void)H5_mpi_comm_free(&head->ctx.fapl_props.mpi_comm);
    if (head->ctx.fapl_flags.mpi_info_valid)
        (void)H5_mpi_info_free(&head->ctx.fapl_props.mpi_info);
#endif /* H5_HAVE_PARALLEL */

    /* Reset the FAPL flags to force the properties to be retrieved again */
    memset(&head->ctx.fapl_flags, 0, sizeof(head->ctx.fapl_flags));

    /* Retrieve the FAPL pointer again also */
    head->ctx.fapl    = NULL;
    head->ctx.fapl_id = H5P_FILE_ACCESS_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_fapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_fapl
 *
 * Purpose:     Sets the FAPL for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_fapl(hid_t fapl_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Reset the cached data */
    H5CX__reset_fapl(*head);

    /* Set the API context's FAPL to a new value */
    (*head)->ctx.fapl_id = fapl_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_fapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_fcpl
 *
 * Purpose:     Sets the FCPL for the current API call context.
 *
 * Note:        All creation property lists are OCPLs.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_fcpl(hid_t fcpl_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Reset the cached data */
    H5CX__reset_ocpl(*head);

    /* Set the API context's OCPL to a new value */
    (*head)->ctx.ocpl_id = fcpl_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_fcpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_ocpypl
 *
 * Purpose:     Sets the OCPYPL for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_ocpypl(hid_t ocpypl_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context's OCPYPL to a new value */
    (*head)->ctx.ocpypl_id = ocpypl_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_ocpypl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_loc
 *
 * Purpose:     Sanity checks and sets up collective operations.
 *
 * Note:        Should be called for all API routines that modify file
 *              metadata but don't pass in an access property list.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_loc(hid_t
#ifndef H5_HAVE_PARALLEL
                 H5_ATTR_UNUSED
#endif /* H5_HAVE_PARALLEL */
                     loc_id)
{
#ifdef H5_HAVE_PARALLEL
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set collective metadata read flag */
    (*head)->ctx.coll_metadata_read = true;

    /* If parallel is enabled and the file driver used is the MPI-IO
     * VFD, issue an MPI barrier for easier debugging if the API function
     * calling this is supposed to be called collectively.
     */
    if (H5_coll_api_sanity_check_g) {
        MPI_Comm mpi_comm; /* File communicator */

        /* Retrieve the MPI communicator from the loc_id or the fapl_id */
        if (H5F_loc_mpi_retrieve_comm(loc_id, &mpi_comm) < 0)
            HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "can't get MPI communicator");

        /* issue the barrier */
        if (mpi_comm != MPI_COMM_NULL)
            MPI_Barrier(mpi_comm);
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
#else  /* H5_HAVE_PARALLEL */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    FUNC_LEAVE_NOAPI(SUCCEED)
#endif /* H5_HAVE_PARALLEL */
} /* end H5CX_set_loc() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_vol_wrap_ctx
 *
 * Purpose:     Sets the VOL object wrapping context for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_vol_wrap_ctx(void *vol_wrap_ctx)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.vol_wrap_ctx = vol_wrap_ctx;

    /* Mark the value as valid */
    (*head)->ctx.vol_wrap_ctx_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_vol_wrap_ctx() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_dxpl
 *
 * Purpose:     Retrieves the DXPL ID for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CX_get_dxpl(void)
{
    H5CX_node_t **head    = NULL;            /* Pointer to head of API context list */
    hid_t         dxpl_id = H5I_INVALID_HID; /* DXPL ID for API operation */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    dxpl_id = (*head)->ctx.dxpl_id;

    FUNC_LEAVE_NOAPI(dxpl_id)
} /* end H5CX_get_dxpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_fapl
 *
 * Purpose:     Retrieves the FAPL ID for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CX_get_fapl(void)
{
    H5CX_node_t **head    = NULL;            /* Pointer to head of API context list */
    hid_t         fapl_id = H5I_INVALID_HID; /* FAPL ID for API operation */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    fapl_id = (*head)->ctx.fapl_id;

    FUNC_LEAVE_NOAPI(fapl_id)
} /* end H5CX_get_fapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_fcpl
 *
 * Purpose:     Retrieves the FCPL ID for the current API call context.
 *
 * Note:        All creation property lists are OCPLs.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CX_get_fcpl(void)
{
    H5CX_node_t **head    = NULL;            /* Pointer to head of API context list */
    hid_t         fcpl_id = H5I_INVALID_HID; /* FCPL ID for API operation */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    fcpl_id = (*head)->ctx.ocpl_id;

    FUNC_LEAVE_NOAPI(fcpl_id)
} /* end H5CX_get_fcpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_ocpl
 *
 * Purpose:     Retrieves the OCPL ID for the current API call context.
 *
 * Note:        All creation property lists are OCPLs.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CX_get_ocpl(void)
{
    H5CX_node_t **head    = NULL;            /* Pointer to head of API context list */
    hid_t         ocpl_id = H5I_INVALID_HID; /* OCPL ID for API operation */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    ocpl_id = (*head)->ctx.ocpl_id;

    FUNC_LEAVE_NOAPI(ocpl_id)
} /* end H5CX_get_ocpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_lapl
 *
 * Purpose:     Retrieves the LAPL ID for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5CX_get_lapl(void)
{
    H5CX_node_t **head    = NULL;            /* Pointer to head of API context list */
    hid_t         lapl_id = H5I_INVALID_HID; /* LAPL ID for API operation */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    lapl_id = (*head)->ctx.lapl_id;

    FUNC_LEAVE_NOAPI(lapl_id)
} /* end H5CX_get_lapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_vol_wrap_ctx
 *
 * Purpose:     Retrieves the VOL object wrapping context for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_vol_wrap_ctx(void **vol_wrap_ctx)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vol_wrap_ctx);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */

    /* No error is expected at this point.  But in case an application calls H5VLwrap_register
     * which doesn't reset the API context and there is no context, returns a relevant error here
     */
    if (!head)
        HGOTO_ERROR(H5E_CONTEXT, H5E_UNINITIALIZED, FAIL, "the API context isn't available");

    if (!(*head))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "unable to get the current API context");

    /* Check for value that was set */
    if ((*head)->ctx.vol_wrap_ctx_valid)
        /* Get the value */
        *vol_wrap_ctx = (*head)->ctx.vol_wrap_ctx;
    else
        *vol_wrap_ctx = NULL;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_vol_wrap_ctx() */

#ifdef H5_HAVE_PARALLEL
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpi_comm
 *
 * Purpose:     Retrieves the MPI communicator for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpi_comm(MPI_Comm *mpi_comm)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpi_comm);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Get the MPI communicator */
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_MPI_PARAMS_COMM_NAME, mpi_comm)

    /* Make a copy of the MPI communicator */
    if (H5_mpi_comm_dup((*head)->ctx.fapl_props.mpi_comm, mpi_comm) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "unable to duplicate MPI communicator");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpi_comm() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_mpi_comm
 *
 * Purpose:     Shallow copy the MPI communicator for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_mpi_comm(MPI_Comm *mpi_comm)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpi_comm);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_MPI_PARAMS_COMM_NAME, mpi_comm)

    /* Get the MPI communicator */
    *mpi_comm = (*head)->ctx.fapl_props.mpi_comm;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_mpi_comm() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpi_info
 *
 * Purpose:     Retrieves the MPI info object for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpi_info(MPI_Info *mpi_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpi_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Get the MPI info object */
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_MPI_PARAMS_INFO_NAME, mpi_info)

    /* Make a copy of the MPI info object */
    if (H5_mpi_info_dup((*head)->ctx.fapl_props.mpi_info, mpi_info) < 0)
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "unable to duplicate MPI info object");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpi_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_mpi_info
 *
 * Purpose:     Shallow copy the MPI info object for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_mpi_info(MPI_Info *mpi_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpi_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_MPI_PARAMS_INFO_NAME, mpi_info)

    /* Get the MPI info object */
    *mpi_info = (*head)->ctx.fapl_props.mpi_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_mpi_info() */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_vol_connector_prop
 *
 * Purpose:     Retrieves the VOL connector ID & info for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_vol_connector_prop(H5VL_connector_prop_t *vol_connector_prop)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vol_connector_prop);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_VOL_CONN_NAME, vol_connector_prop)

    /* Get the VOL connector & info */
    H5MM_memcpy(vol_connector_prop, &(*head)->ctx.fapl_props.vol_connector_prop,
                sizeof(H5VL_connector_prop_t));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_vol_connector_prop() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_driver_prop
 *
 * Purpose:     Retrieves the VFD driver property for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_driver_prop(H5FD_driver_prop_t *driver_prop)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(driver_prop);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FILE_DRV_NAME, driver_prop)

    /* Get the VOL connector & info */
    H5MM_memcpy(driver_prop, &(*head)->ctx.fapl_props.driver_prop, sizeof(H5FD_driver_prop_t));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_driver_prop() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_driver
 *
 * Purpose:     Retrieves the VFD driver struct for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
H5FD_driver_t *
H5CX_peek_driver(void)
{
    H5CX_node_t  **head      = NULL; /* Pointer to head of API context list */
    H5FD_driver_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID_ERR(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FILE_DRV_NAME, driver_prop, NULL)

    /* Set the return value */
    ret_value = (*head)->ctx.fapl_props.driver_prop.driver;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_driver() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_driver_info
 *
 * Purpose:     Retrieves the VFD driver info for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
const void *
H5CX_peek_driver_info(void)
{
    H5CX_node_t **head      = NULL; /* Pointer to head of API context list */
    const void   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID_ERR(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FILE_DRV_NAME, driver_prop, NULL)

    /* Set the return value */
    ret_value = (*head)->ctx.fapl_props.driver_prop.driver_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_driver_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_driver_config_str
 *
 * Purpose:     Retrieves the VFD driver config string for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
const char *
H5CX_peek_driver_config_str(void)
{
    H5CX_node_t **head      = NULL; /* Pointer to head of API context list */
    const char   *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID_ERR(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FILE_DRV_NAME, driver_prop, NULL)

    /* Set the return value */
    ret_value = (*head)->ctx.fapl_props.driver_prop.driver_config_str;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_driver_config_str() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_file_image_info
 *
 * Purpose:     Shallow copy the file image info for an operation.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_file_image_info(H5FD_file_image_info_t *file_image_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(file_image_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FILE_IMAGE_INFO_NAME, file_image_info)

    /* Get the VOL connector & info */
    H5MM_memcpy(file_image_info, &(*head)->ctx.fapl_props.file_image_info, sizeof(H5FD_file_image_info_t));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_file_image_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_tag
 *
 * Purpose:     Retrieves the object tag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5CX_get_tag(void)
{
    H5CX_node_t **head = NULL;        /* Pointer to head of API context list */
    haddr_t       tag  = HADDR_UNDEF; /* Current object's tag (ohdr chunk #0 address) */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    tag = (*head)->ctx.tag;

    FUNC_LEAVE_NOAPI(tag)
} /* end H5CX_get_tag() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_ring
 *
 * Purpose:     Retrieves the metadata cache ring for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
H5AC_ring_t
H5CX_get_ring(void)
{
    H5CX_node_t **head = NULL;          /* Pointer to head of API context list */
    H5AC_ring_t   ring = H5AC_RING_INV; /* Current metadata cache ring for entries */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    ring = (*head)->ctx.ring;

    FUNC_LEAVE_NOAPI(ring)
} /* end H5CX_get_ring() */

#ifdef H5_HAVE_SUBFILING_VFD
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sf_stub_file_id
 *
 * Purpose:     Retrieves the stub file ID for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
uint64_t
H5CX_get_sf_stub_file_id(void)
{
    H5CX_node_t **head            = NULL;                       /* Pointer to head of API context list */
    uint64_t      sf_stub_file_id = H5FD_SUBFILING_BAD_FILE_ID; /* Current metadata cache ring for entries */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    sf_stub_file_id = (*head)->ctx.sf_stub_file_id;

    FUNC_LEAVE_NOAPI(sf_stub_file_id)
} /* end H5CX_get_sf_stub_file_id() */
#endif /* H5_HAVE_SUBFILING_VFD */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_want_posix_fd
 *
 * Purpose:     Retrieves the want POSIX file descriptor flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_get_want_posix_fd(void)
{
    H5CX_node_t **head          = NULL;  /* Pointer to head of API context list */
    bool          want_posix_fd = false; /* Current metadata cache ring for entries */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    want_posix_fd = (*head)->ctx.want_posix_fd;

    FUNC_LEAVE_NOAPI(want_posix_fd)
} /* end H5CX_get_want_posix_fd() */

#ifdef H5_HAVE_PARALLEL
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_coll_metadata_read
 *
 * Purpose:     Retrieves the "do collective metadata reads" flag for the current API call context.
 *
 * Return:      true / false on success / <can't fail>
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_get_coll_metadata_read(void)
{
    H5CX_node_t **head         = NULL; /* Pointer to head of API context list */
    bool          coll_md_read = false;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    coll_md_read = (*head)->ctx.coll_metadata_read;

    FUNC_LEAVE_NOAPI(coll_md_read)
} /* end H5CX_get_coll_metadata_read() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpi_coll_datatypes
 *
 * Purpose:     Retrieves the MPI datatypes for collective I/O for the current API call context.
 *
 * Note:	This is only a shallow copy, the datatypes are not duplicated.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpi_coll_datatypes(MPI_Datatype *btype, MPI_Datatype *ftype)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(btype);
    assert(ftype);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context values */
    *btype = (*head)->ctx.btype;
    *ftype = (*head)->ctx.ftype;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpi_coll_datatypes() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpi_file_flushing
 *
 * Purpose:     Retrieves the "flushing an MPI-opened file" flag for the current API call context.
 *
 * Return:      true / false on success / <can't fail>
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_get_mpi_file_flushing(void)
{
    H5CX_node_t **head     = NULL; /* Pointer to head of API context list */
    bool          flushing = false;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    flushing = (*head)->ctx.mpi_file_flushing;

    FUNC_LEAVE_NOAPI(flushing)
} /* end H5CX_get_mpi_file_flushing() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_rank0_bcast
 *
 * Purpose:     Retrieves if the dataset meets read-with-rank0-and-bcast requirements for the current API call
 *context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_get_mpio_rank0_bcast(void)
{
    H5CX_node_t **head           = NULL; /* Pointer to head of API context list */
    bool          do_rank0_bcast = false;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    do_rank0_bcast = (*head)->ctx.rank0_bcast;

    FUNC_LEAVE_NOAPI(do_rank0_bcast)
} /* end H5CX_get_mpio_rank0_bcast() */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_btree_split_ratios
 *
 * Purpose:     Retrieves the B-tree split ratios for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_btree_split_ratios(double split_ratio[3])
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(split_ratio);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_BTREE_SPLIT_RATIO_NAME,
                             btree_split_ratio)

    /* Get the B-tree split ratio values */
    H5MM_memcpy(split_ratio, &(*head)->ctx.dxpl_props.btree_split_ratio,
                sizeof((*head)->ctx.dxpl_props.btree_split_ratio));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_btree_split_ratios() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_max_temp_buf
 *
 * Purpose:     Retrieves the maximum temporary buffer size for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_max_temp_buf(size_t *max_temp_buf)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(max_temp_buf);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MAX_TEMP_BUF_NAME, max_temp_buf)

    /* Get the value */
    *max_temp_buf = (*head)->ctx.dxpl_props.max_temp_buf;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_max_temp_buf() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_tconv_buf
 *
 * Purpose:     Retrieves the temporary buffer pointer for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_tconv_buf(void **tconv_buf)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(tconv_buf);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_TCONV_BUF_NAME, tconv_buf)

    /* Get the value */
    *tconv_buf = (*head)->ctx.dxpl_props.tconv_buf;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_tconv_buf() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_bkgr_buf
 *
 * Purpose:     Retrieves the background buffer pointer for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_bkgr_buf(void **bkgr_buf)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(bkgr_buf);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_BKGR_BUF_NAME, bkgr_buf)

    /* Get the value */
    *bkgr_buf = (*head)->ctx.dxpl_props.bkgr_buf;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_bkgr_buf() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_bkgr_buf_type
 *
 * Purpose:     Retrieves the background buffer type for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_bkgr_buf_type(H5T_bkg_t *bkgr_buf_type)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(bkgr_buf_type);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_BKGR_BUF_TYPE_NAME, bkgr_buf_type)

    /* Get the value */
    *bkgr_buf_type = (*head)->ctx.dxpl_props.bkgr_buf_type;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_bkgr_buf_type() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_vec_size
 *
 * Purpose:     Retrieves the hyperslab vector size for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_vec_size(size_t *vec_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vec_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_HYPER_VECTOR_SIZE_NAME, vec_size)

    /* Get the value */
    *vec_size = (*head)->ctx.dxpl_props.vec_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_vec_size() */

#ifdef H5_HAVE_PARALLEL

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_io_xfer_mode
 *
 * Purpose:     Retrieves the parallel transfer mode for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_io_xfer_mode(H5FD_mpio_xfer_t *io_xfer_mode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(io_xfer_mode);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_IO_XFER_MODE_NAME, io_xfer_mode)

    /* Get the value */
    *io_xfer_mode = (*head)->ctx.dxpl_props.io_xfer_mode;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_io_xfer_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_coll_opt
 *
 * Purpose:     Retrieves the collective / independent parallel I/O option for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_coll_opt(H5FD_mpio_collective_opt_t *mpio_coll_opt)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_coll_opt);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MPIO_COLLECTIVE_OPT_NAME, mpio_coll_opt)

    /* Get the value */
    *mpio_coll_opt = (*head)->ctx.dxpl_props.mpio_coll_opt;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_coll_opt() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_local_no_coll_cause
 *
 * Purpose:     Retrieves the local cause for breaking collective I/O for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_local_no_coll_cause(uint32_t *mpio_local_no_coll_cause)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_local_no_coll_cause);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID_SET(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_MPIO_LOCAL_NO_COLLECTIVE_CAUSE_NAME,
                                 mpio_local_no_coll_cause)

    /* Get the value */
    *mpio_local_no_coll_cause = (*head)->ctx.dxpl_props.mpio_local_no_coll_cause;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_local_no_coll_cause() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_global_no_coll_cause
 *
 * Purpose:     Retrieves the global cause for breaking collective I/O for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_global_no_coll_cause(uint32_t *mpio_global_no_coll_cause)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_global_no_coll_cause);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID_SET(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_MPIO_GLOBAL_NO_COLLECTIVE_CAUSE_NAME,
                                 mpio_global_no_coll_cause)

    /* Get the value */
    *mpio_global_no_coll_cause = (*head)->ctx.dxpl_props.mpio_global_no_coll_cause;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_global_no_coll_cause() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_chunk_opt_mode
 *
 * Purpose:     Retrieves the collective chunk optimization mode for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_chunk_opt_mode(H5FD_mpio_chunk_opt_t *mpio_chunk_opt_mode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_chunk_opt_mode);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MPIO_CHUNK_OPT_HARD_NAME,
                             mpio_chunk_opt_mode)

    /* Get the value */
    *mpio_chunk_opt_mode = (*head)->ctx.dxpl_props.mpio_chunk_opt_mode;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_chunk_opt_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_chunk_opt_num
 *
 * Purpose:     Retrieves the collective chunk optimization threshold for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_chunk_opt_num(unsigned *mpio_chunk_opt_num)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_chunk_opt_num);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MPIO_CHUNK_OPT_NUM_NAME,
                             mpio_chunk_opt_num)

    /* Get the value */
    *mpio_chunk_opt_num = (*head)->ctx.dxpl_props.mpio_chunk_opt_num;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_chunk_opt_num() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mpio_chunk_opt_ratio
 *
 * Purpose:     Retrieves the collective chunk optimization ratio for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mpio_chunk_opt_ratio(unsigned *mpio_chunk_opt_ratio)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mpio_chunk_opt_ratio);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MPIO_CHUNK_OPT_RATIO_NAME,
                             mpio_chunk_opt_ratio)

    /* Get the value */
    *mpio_chunk_opt_ratio = (*head)->ctx.dxpl_props.mpio_chunk_opt_ratio;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mpio_chunk_opt_ratio() */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_err_detect
 *
 * Purpose:     Retrieves the error detection info for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_err_detect(H5Z_EDC_t *err_detect)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(err_detect);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_EDC_NAME, err_detect)

    /* Get the value */
    *err_detect = (*head)->ctx.dxpl_props.err_detect;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_err_detect() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_filter_cb
 *
 * Purpose:     Retrieves the I/O filter callback function for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_filter_cb(H5Z_cb_t *filter_cb)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(filter_cb);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_FILTER_CB_NAME, filter_cb)

    /* Get the value */
    *filter_cb = (*head)->ctx.dxpl_props.filter_cb;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_filter_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_data_transform
 *
 * Purpose:     Retrieves the pointer to the data transformation expression for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_data_transform(H5Z_data_xform_t **data_transform)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(data_transform);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_XFORM_NAME, data_transform)

    /* Get the value */
    *data_transform = (*head)->ctx.dxpl_props.data_transform;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_data_transform() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_vlen_alloc_info
 *
 * Purpose:     Retrieves the VL datatype alloc info for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_vlen_alloc_info(H5T_vlen_alloc_info_t *vl_alloc_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vl_alloc_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    /* Check if the value has been retrieved already */
    if (!(*head)->ctx.dxpl_flags.vl_alloc_info_valid) {
        /* Check for default DXPL */
        if ((*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT)
            (*head)->ctx.dxpl_props.vl_alloc_info = H5CX_def_dxpl_cache.vl_alloc_info;
        else {
            /* Check if the property list is already available */
            if (NULL == (*head)->ctx.dxpl)
                /* Get the dataset transfer property list pointer */
                if (NULL == ((*head)->ctx.dxpl =
                                 H5P_object_verify((*head)->ctx.dxpl_id, H5P_TYPE_DATASET_XFER, true)))
                    HGOTO_ERROR(H5E_CONTEXT, H5E_BADTYPE, FAIL,
                                "can't get default dataset transfer property list");

            /* Get VL datatype alloc info values */
            if (H5P_get((*head)->ctx.dxpl, H5D_XFER_VLEN_ALLOC_NAME,
                        &(*head)->ctx.dxpl_props.vl_alloc_info.alloc_func) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
            if (H5P_get((*head)->ctx.dxpl, H5D_XFER_VLEN_ALLOC_INFO_NAME,
                        &(*head)->ctx.dxpl_props.vl_alloc_info.alloc_info) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
            if (H5P_get((*head)->ctx.dxpl, H5D_XFER_VLEN_FREE_NAME,
                        &(*head)->ctx.dxpl_props.vl_alloc_info.free_func) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
            if (H5P_get((*head)->ctx.dxpl, H5D_XFER_VLEN_FREE_INFO_NAME,
                        &(*head)->ctx.dxpl_props.vl_alloc_info.free_info) < 0)
                HGOTO_ERROR(H5E_CONTEXT, H5E_CANTGET, FAIL, "Can't retrieve VL datatype alloc info");
        } /* end else */

        /* Mark the value as valid */
        (*head)->ctx.dxpl_flags.vl_alloc_info_valid = true;
    } /* end if */

    /* Get the value */
    *vl_alloc_info = (*head)->ctx.dxpl_props.vl_alloc_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_vlen_alloc_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_dt_conv_cb
 *
 * Purpose:     Retrieves the datatype conversion exception callback for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_dt_conv_cb(H5T_conv_cb_t *dt_conv_cb)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(dt_conv_cb);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_CONV_CB_NAME, dt_conv_cb)

    /* Get the value */
    *dt_conv_cb = (*head)->ctx.dxpl_props.dt_conv_cb;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_dt_conv_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_selection_io_mode
 *
 * Purpose:     Retrieves the selection I/O mode for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_selection_io_mode(H5D_selection_io_mode_t *selection_io_mode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(selection_io_mode);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_SELECTION_IO_MODE_NAME,
                             selection_io_mode)

    /* Get the value */
    *selection_io_mode = (*head)->ctx.dxpl_props.selection_io_mode;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_selection_io_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_no_selection_io_cause
 *
 * Purpose:     Retrieves the cause for not performing selection I/O
 *              for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_no_selection_io_cause(uint32_t *no_selection_io_cause)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(no_selection_io_cause);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID_SET(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_NO_SELECTION_IO_CAUSE_NAME,
                                 no_selection_io_cause)

    /* Get the value */
    *no_selection_io_cause = (*head)->ctx.dxpl_props.no_selection_io_cause;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_no_selection_io_cause() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_actual_selection_io_mode
 *
 * Purpose:     Retrieves the actual I/O mode (scalar, vector, and/or selection) for the current API call
 *context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_actual_selection_io_mode(uint32_t *actual_selection_io_mode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(actual_selection_io_mode);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    /* This property is a special case - we want to wipe out any previous setting.  Copy the default setting
     * if it has not been set yet. */
    if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT &&
        !(*head)->ctx.dxpl_flags.actual_selection_io_mode_set &&
        !(*head)->ctx.dxpl_flags.actual_selection_io_mode_valid) {
        (*head)->ctx.dxpl_props.actual_selection_io_mode     = H5CX_def_dxpl_cache.actual_selection_io_mode;
        (*head)->ctx.dxpl_flags.actual_selection_io_mode_set = true;
    }
    H5CX_RETRIEVE_PROP_VALID_SET(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_ACTUAL_SELECTION_IO_MODE_NAME,
                                 actual_selection_io_mode)

    /* Get the value */
    *actual_selection_io_mode = (*head)->ctx.dxpl_props.actual_selection_io_mode;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_actual_selection_io_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_modify_write_buf
 *
 * Purpose:     Retrieves the modify write buffer property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_modify_write_buf(bool *modify_write_buf)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(modify_write_buf);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_MODIFY_WRITE_BUF_NAME, modify_write_buf)

    /* Get the value */
    *modify_write_buf = (*head)->ctx.dxpl_props.modify_write_buf;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_selection_io_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_dset_io_selection
 *
 * Purpose:     Retrieves the dataset I/O selection's dataspace for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_dset_io_selection(H5S_t **space)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(space);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(dxpl, H5P_DATASET_XFER_DEFAULT, H5D_XFER_DSET_IO_SEL_NAME, dset_io_selection)

    /* Get the value */
    *space = (*head)->ctx.dxpl_props.dset_io_selection;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_dset_io_selection() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_encoding
 *
 * Purpose:     Retrieves the character encoding for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_encoding(H5T_cset_t *encoding)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(encoding);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lcpl_id);

    H5CX_RETRIEVE_PROP_VALID(lcpl, H5P_LINK_CREATE_DEFAULT, H5P_STRCRT_CHAR_ENCODING_NAME, encoding)

    /* Get the value */
    *encoding = (*head)->ctx.lcpl_props.encoding;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_encoding() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_intermediate_group
 *
 * Purpose:     Retrieves the create intermediate group flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_intermediate_group(unsigned *crt_intermed_group)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(crt_intermed_group);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lcpl_id);

    H5CX_RETRIEVE_PROP_VALID(lcpl, H5P_LINK_CREATE_DEFAULT, H5L_CRT_INTERMEDIATE_GROUP_NAME,
                             intermediate_group)

    /* Get the value */
    *crt_intermed_group = (*head)->ctx.lcpl_props.intermediate_group;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_create_intermediate_group() */

#ifdef H5_HAVE_PARALLEL
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_lapl_coll_md_read
 *
 * Purpose:     Retrieves the LAPL'scollective metadata read flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_lapl_coll_md_read(H5P_coll_md_read_flag_t *coll_md_read)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(coll_md_read);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lapl_id);

    H5CX_RETRIEVE_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5_COLL_MD_READ_FLAG_NAME, lapl_coll_md_read)

    /* Get the value */
    *coll_md_read = (*head)->ctx.lapl_props.lapl_coll_md_read;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_lapl_coll_md_read() */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_elink_prefix
 *
 * Purpose:     Retrieves the pointer to the prefix for external link
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_elink_prefix(const char **elink_prefix)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(elink_prefix);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lapl_id);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5L_ACS_ELINK_PREFIX_NAME, elink_prefix)

    /* Get the value */
    *elink_prefix = (*head)->ctx.lapl_props.elink_prefix;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_elink_prefix() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_elink_cb_info
 *
 * Purpose:     Retrieves the callback info for external links
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_elink_cb_info(H5L_elink_cb_t *elink_cb_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(elink_cb_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lapl_id);

    H5CX_RETRIEVE_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5L_ACS_ELINK_CB_NAME, elink_cb_info)

    /* Get the value */
    *elink_cb_info = (*head)->ctx.lapl_props.elink_cb_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_elink_cb_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_elink_fapl
 *
 * Purpose:     Shallow copy the file access property list for external links for the current API call
 *context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_elink_fapl(H5P_genplist_t **elink_fapl)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(elink_fapl);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.lapl_id);

    H5CX_PEEK_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5L_ACS_ELINK_FAPL_NAME, elink_fapl)

    /* Get the value */
    *elink_fapl = (*head)->ctx.lapl_props.elink_fapl;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_peek_elink_fapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_elink_flags
 *
 * Purpose:     Retrieves the file access flags for external links for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_elink_flags(unsigned *elink_flags)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(elink_flags);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5L_ACS_ELINK_FLAGS_NAME, elink_flags)

    /* Get the value */
    *elink_flags = (*head)->ctx.lapl_props.elink_flags;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_elink_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_nlinks
 *
 * Purpose:     Retrieves the # of soft / UD links to traverse for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_nlinks(size_t *nlinks)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(nlinks);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dxpl_id);

    H5CX_RETRIEVE_PROP_VALID(lapl, H5P_LINK_ACCESS_DEFAULT, H5L_ACS_NLINKS_NAME, nlinks)

    /* Get the value */
    *nlinks = (*head)->ctx.lapl_props.nlinks;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_nlinks() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_lapl
 *
 * Purpose:     Resets the cached LAPL info for the current API call context.
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_lapl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset the LAPL flags to force the properties to be retrieved again */
    memset(&head->ctx.lapl_flags, 0, sizeof(head->ctx.lapl_flags));

    /* Retrieve the LAPL pointer again also */
    head->ctx.lapl    = NULL;
    head->ctx.lapl_id = H5P_LINK_ACCESS_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_lapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_libver_bounds
 *
 * Purpose:     Retrieves the low/high bounds for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_libver_bounds(H5F_libver_t *low_bound, H5F_libver_t *high_bound)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(low_bound);
    assert(high_bound);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_LIBVER_LOW_BOUND_NAME, low_bound)
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_LIBVER_HIGH_BOUND_NAME, high_bound)

    /* Get the values */
    *low_bound  = (*head)->ctx.fapl_props.low_bound;
    *high_bound = (*head)->ctx.fapl_props.high_bound;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_libver_bounds() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_use_file_locking
 *
 * Purpose:     Retrieves the use file locking flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_use_file_locking(bool *use_file_locking)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(use_file_locking);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_USE_FILE_LOCKING_NAME, use_file_locking)

    /* Get the value */
    *use_file_locking = (*head)->ctx.fapl_props.use_file_locking;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_use_file_locking() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_ignore_disabled_locks
 *
 * Purpose:     Retrieves the ignore disabled locks flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_ignore_disabled_locks(bool *ignore_disabled_locks)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(ignore_disabled_locks);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_IGNORE_DISABLED_FILE_LOCKS_NAME,
                             ignore_disabled_locks)

    /* Get the value */
    *ignore_disabled_locks = (*head)->ctx.fapl_props.ignore_disabled_locks;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_ignore_disabled_locks() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_alignment
 *
 * Purpose:     Retrieves the alignment properties for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_alignment(hsize_t *align_bound, hsize_t *align_threshold)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(align_bound);
    assert(align_threshold);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_ALIGN_NAME, align_bound)
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_ALIGN_THRHD_NAME, align_threshold)

    /* Get the values */
    *align_bound     = (*head)->ctx.fapl_props.align_bound;
    *align_threshold = (*head)->ctx.fapl_props.align_threshold;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_alignment() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_clear_status_flags
 *
 * Purpose:     Retrieves the clear status flags for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_get_clear_status_flags(bool *clear_status_flags)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(clear_status_flags);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_TEST_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_CLEAR_STATUS_FLAGS_NAME,
                                  clear_status_flags)

    /* Get the value */
    *clear_status_flags = (*head)->ctx.fapl_props.clear_status_flags;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_clear_status_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_gc_ref
 *
 * Purpose:     Retrieves the garbage collection reference properties for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_gc_ref(unsigned *gc_ref)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(gc_ref);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_GARBG_COLCT_REF_NAME, gc_ref)

    /* Get the value */
    *gc_ref = (*head)->ctx.fapl_props.gc_ref;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_gc_ref() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_use_mdc_logging
 *
 * Purpose:     Retrieves the use metadata cache logging flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_use_mdc_logging(bool *use_mdc_logging)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(use_mdc_logging);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_USE_MDC_LOGGING_NAME, use_mdc_logging)

    /* Get the value */
    *use_mdc_logging = (*head)->ctx.fapl_props.use_mdc_logging;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_use_mdc_logging() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_mdc_log_location
 *
 * Purpose:     Retrieves the metadata cache log location for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_mdc_log_location(char **mdc_log_location)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mdc_log_location);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_PEEK_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_MDC_LOG_LOCATION_NAME, mdc_log_location)

    /* Get the value */
    *mdc_log_location = (*head)->ctx.fapl_props.mdc_log_location;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_mdc_log_location() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_start_mdc_logging_on_access
 *
 * Purpose:     Retrieves the start metadata cache logging on access flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_start_mdc_logging_on_access(bool *start_mdc_logging_on_access)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(start_mdc_logging_on_access);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_START_MDC_LOG_ON_ACCESS_NAME,
                             start_mdc_logging_on_access)

    /* Get the value */
    *start_mdc_logging_on_access = (*head)->ctx.fapl_props.start_mdc_logging_on_access;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_start_mdc_logging_on_access() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_metadata_read_attempts
 *
 * Purpose:     Retrieves the metadata cache read attempts for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_metadata_read_attempts(unsigned *mdc_read_attempts)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mdc_read_attempts);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_METADATA_READ_ATTEMPTS_NAME,
                             mdc_read_attempts)

    /* Get the value */
    *mdc_read_attempts = (*head)->ctx.fapl_props.mdc_read_attempts;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_metadata_read_attempts() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_meta_alloc_block_size
 *
 * Purpose:     Retrieves the metadata allocation block size for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_meta_alloc_block_size(hsize_t *meta_alloc_block_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(meta_alloc_block_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_META_BLOCK_SIZE_NAME,
                             meta_alloc_block_size)

    /* Get the value */
    *meta_alloc_block_size = (*head)->ctx.fapl_props.meta_alloc_block_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_meta_alloc_block_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mdc_init_config
 *
 * Purpose:     Retrieves the metadata cache initialization configuration for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mdc_init_config(H5AC_cache_config_t *mdc_init_config)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mdc_init_config);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_META_CACHE_INIT_CONFIG_NAME,
                             mdc_init_config)

    /* Get the value */
    H5MM_memcpy(mdc_init_config, &(*head)->ctx.fapl_props.mdc_init_config, sizeof(*mdc_init_config));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mdc_init_config() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mdc_image_config
 *
 * Purpose:     Retrieves the metadata cache image initial configuration for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mdc_image_config(H5AC_cache_image_config_t *mdc_image_config)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mdc_image_config);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_META_CACHE_INIT_IMAGE_CONFIG_NAME,
                             mdc_image_config)

    /* Get the value */
    H5MM_memcpy(mdc_image_config, &(*head)->ctx.fapl_props.mdc_image_config, sizeof(*mdc_image_config));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mdc_image_config() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_object_flush_strategy
 *
 * Purpose:     Retrieves the object flush strategy for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_object_flush_strategy(H5F_object_flush_t *object_flush_strategy)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(object_flush_strategy);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_OBJECT_FLUSH_CB_NAME,
                             object_flush_strategy)

    /* Get the value */
    H5MM_memcpy(object_flush_strategy, &(*head)->ctx.fapl_props.object_flush_strategy,
                sizeof(*object_flush_strategy));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_object_flush_strategy() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_page_buffer_size
 *
 * Purpose:     Retrieves the page buffer size for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_page_buffer_size(size_t *page_buf_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(page_buf_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_PAGE_BUFFER_SIZE_NAME, pb_size)

    /* Get the values */
    *page_buf_size = (*head)->ctx.fapl_props.pb_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_page_buffer_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_page_buffer_percs
 *
 * Purpose:     Retrieves the page buffer percentages for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_page_buffer_percs(unsigned *min_meta_perc, unsigned *min_raw_perc)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(min_meta_perc);
    assert(min_raw_perc);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_PAGE_BUFFER_MIN_META_PERC_NAME,
                             pb_min_meta_perc)
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_PAGE_BUFFER_MIN_RAW_PERC_NAME,
                             pb_min_raw_perc)

    /* Get the values */
    *min_meta_perc = (*head)->ctx.fapl_props.pb_min_meta_perc;
    *min_raw_perc  = (*head)->ctx.fapl_props.pb_min_raw_perc;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_page_buffer_percs() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_rdcc_info
 *
 * Purpose:     Retrieves the raw data chunk cache info for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_rdcc_info(size_t *nslots, size_t *nbytes, double *w0)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(nslots);
    assert(nbytes);
    assert(w0);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_DATA_CACHE_NUM_SLOTS_NAME,
                             fapl_rdcc_nslots)
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_DATA_CACHE_BYTE_SIZE_NAME,
                             fapl_rdcc_nbytes)
    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_PREEMPT_READ_CHUNKS_NAME, fapl_rdcc_w0)

    /* Get the values */
    *nslots = (*head)->ctx.fapl_props.fapl_rdcc_nslots;
    *nbytes = (*head)->ctx.fapl_props.fapl_rdcc_nbytes;
    *w0     = (*head)->ctx.fapl_props.fapl_rdcc_w0;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_rdcc_info() */

#ifdef H5_HAVE_PARALLEL
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_fapl_coll_md_read
 *
 * Purpose:     Retrieves the FAPL's collective metadata read flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_fapl_coll_md_read(H5P_coll_md_read_flag_t *coll_md_read)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(coll_md_read);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5_COLL_MD_READ_FLAG_NAME, fapl_coll_md_read)

    /* Get the value */
    *coll_md_read = (*head)->ctx.fapl_props.fapl_coll_md_read;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_fapl_coll_md_read() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_coll_md_write
 *
 * Purpose:     Retrieves the collective metadata write flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_coll_md_write(bool *coll_md_write)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(coll_md_write);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_COLL_MD_WRITE_FLAG_NAME, coll_md_write)

    /* Get the value */
    *coll_md_write = (*head)->ctx.fapl_props.coll_md_write;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_coll_md_write() */

#ifdef H5_HAVE_SUBFILING_VFD
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sf_ioc_params
 *
 * Purpose:     Retrieves the subfiling IOC parameters for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sf_ioc_params(H5FD_subfiling_params_t *sf_ioc_params)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sf_ioc_params);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_SUBFILING_CONFIG_PROP_NAME, sf_ioc_params)

    /* Get the value */
    *sf_ioc_params = (*head)->ctx.fapl_props.sf_ioc_params;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_sf_ioc_params() */
#endif /* H5_HAVE_SUBFILING_VFD */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_efc_size
 *
 * Purpose:     Retrieves the size of the external file cache for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_efc_size(unsigned *efc_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(efc_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_EFC_SIZE_NAME, efc_size)

    /* Get the value */
    *efc_size = (*head)->ctx.fapl_props.efc_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_efc_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_close_degree
 *
 * Purpose:     Retrieves the file close degree for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_close_degree(H5F_close_degree_t *close_degree)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(close_degree);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_CLOSE_DEGREE_NAME, close_degree)

    /* Get the value */
    *close_degree = (*head)->ctx.fapl_props.close_degree;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_close_degree() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_evict_on_close
 *
 * Purpose:     Retrieves the evict on close property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_evict_on_close(bool *evict_on_close)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(evict_on_close);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_EVICT_ON_CLOSE_FLAG_NAME, evict_on_close)

    /* Get the value */
    *evict_on_close = (*head)->ctx.fapl_props.evict_on_close;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_evict_on_close() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_rfic_flags
 *
 * Purpose:     Retrieves the relaxed file integrity checks property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_rfic_flags(uint64_t *rfic_flags)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(rfic_flags);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_RFIC_FLAGS_NAME, rfic_flags)

    /* Get the value */
    *rfic_flags = (*head)->ctx.fapl_props.rfic_flags;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_rfic_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sdata_block_size
 *
 * Purpose:     Retrieves the "small" raw data block size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sdata_block_size(hsize_t *sdata_block_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sdata_block_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_SDATA_BLOCK_SIZE_NAME, sdata_block_size)

    /* Get the value */
    *sdata_block_size = (*head)->ctx.fapl_props.sdata_block_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_sdata_block_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sieve_buf_size
 *
 * Purpose:     Retrieves the sieve buffer size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sieve_buf_size(size_t *sieve_buf_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sieve_buf_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_SIEVE_BUF_SIZE_NAME, sieve_buf_size)

    /* Get the value */
    *sieve_buf_size = (*head)->ctx.fapl_props.sieve_buf_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_sieve_buf_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_null_fsm_addr
 *
 * Purpose:     Retrieves the null file space map address property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_null_fsm_addr(bool *null_fsm_addr)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(null_fsm_addr);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_TEST_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_NULL_FSM_ADDR_NAME, null_fsm_addr)

    /* Get the value */
    *null_fsm_addr = (*head)->ctx.fapl_props.null_fsm_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_null_fsm_addr() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_skip_eof_check
 *
 * Purpose:     Retrieves the skip EOF check property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_skip_eof_check(bool *skip_eof_check)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(skip_eof_check);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_TEST_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_SKIP_EOF_CHECK_NAME, skip_eof_check)

    /* Get the value */
    *skip_eof_check = (*head)->ctx.fapl_props.skip_eof_check;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_skip_eof_check() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_family_to_single
 *
 * Purpose:     Retrieves the family to single file property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_family_to_single(bool *fam_to_single)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fam_to_single);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_TEST_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FAMILY_TO_SINGLE_NAME, fam_to_single)

    /* Get the value */
    *fam_to_single = (*head)->ctx.fapl_props.fam_to_single;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_family_to_single() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_family_offset
 *
 * Purpose:     Retrieves the family offset property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_family_offset(hsize_t *fam_offset)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fam_offset);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FAMILY_OFFSET_NAME, fam_offset)

    /* Get the value */
    *fam_offset = (*head)->ctx.fapl_props.fam_offset;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_family_offset() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_family_newsize
 *
 * Purpose:     Retrieves the size of the new family file property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_family_newsize(hsize_t *fam_newsize)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fam_newsize);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.fapl_id);

    H5CX_TEST_RETRIEVE_PROP_VALID(fapl, H5P_FILE_ACCESS_DEFAULT, H5F_ACS_FAMILY_NEWSIZE_NAME, fam_newsize)

    /* Get the value */
    *fam_newsize = (*head)->ctx.fapl_props.fam_newsize;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_family_newsize() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_min_dset_hdr
 *
 * Purpose:     Retrieves the flag that indicates whether the dataset object
 *		header should be minimized
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_min_dset_hdr(bool *min_dset_hdr)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(min_dset_hdr);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_CRT_MIN_DSET_HDR_SIZE_NAME,
                                    min_dset_ohdr)

    /* Get the value */
    *min_dset_hdr = (*head)->ctx.dcpl_props.min_dset_ohdr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_min_dset_hdr() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_alloc_time_state
 *
 * Purpose:     Retrieves the flag that indicates whether the dataset allocation
 *		time state is set
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_alloc_time_state(unsigned *alloc_time_state)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(alloc_time_state);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_CRT_ALLOC_TIME_STATE_NAME,
                                    alloc_time_state)

    /* Get the value */
    *alloc_time_state = (*head)->ctx.dcpl_props.alloc_time_state;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_alloc_time_state() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_layout
 *
 * Purpose:     Retrieves the storage layout for dataset creation
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_layout(H5O_layout_t *layout)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(layout);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);
    assert(H5P_isa_class((*head)->ctx.ocpl_id, H5P_DATASET_CREATE));

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_CRT_LAYOUT_NAME, layout)

    /* Make copy of layout */
    if (NULL == H5O_msg_copy(H5O_LAYOUT_ID, &(*head)->ctx.dcpl_props.layout, layout))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy layout");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_layout() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_efl
 *
 * Purpose:     Retrieves the external file list for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_efl(H5O_efl_t *efl)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(efl);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_CRT_EXT_FILE_LIST_NAME, efl)

    /* Make copy of external file list */
    if (NULL == H5O_msg_copy(H5O_EFL_ID, &(*head)->ctx.dcpl_props.efl, efl))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy external file list");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_efl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_fill_value
 *
 * Purpose:     Retrieves the fill value for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_fill_value(H5O_fill_t *fill_value)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fill_value);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_CRT_FILL_VALUE_NAME,
                                    fill_value)

    /* Make copy of fill value */
    if (NULL == H5O_msg_copy(H5O_FILL_ID, &(*head)->ctx.dcpl_props.fill_value, fill_value))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy fill value");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_fill_value() */

#ifdef H5O_ENABLE_BOGUS
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_bogus_msg_id
 *
 * Purpose:     Retrieves the bogus message ID for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_bogus_msg_id(unsigned *bogus_msg_id)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(bogus_msg_id);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_TEST_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_BOGUS_MSG_ID_NAME,
                                         bogus_msg_id)

    /* Get the value */
    *bogus_msg_id = (*head)->ctx.dcpl_props.bogus_msg_id;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_bogus_msg_id() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_bogus_msg_flags
 *
 * Purpose:     Retrieves the bogus message flags for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_bogus_msg_flags(uint8_t *bogus_msg_flags)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(bogus_msg_flags);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_TEST_RETRIEVE_SUBCLS_PROP_VALID(ocpl, dcpl, H5P_OBJECT_CREATE_DEFAULT, H5D_BOGUS_MSG_FLAGS_NAME,
                                         bogus_msg_flags)

    /* Get the value */
    *bogus_msg_flags = (*head)->ctx.dcpl_props.bogus_msg_flags;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_bogus_msg_flags() */
#endif /* H5O_ENABLE_BOGUS */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_ginfo
 *
 * Purpose:     Retrieves the group info property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_ginfo(H5O_ginfo_t *ginfo)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(ginfo);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, gcpl, H5P_OBJECT_CREATE_DEFAULT, H5G_CRT_GROUP_INFO_NAME, ginfo)

    /* Get the value */
    *ginfo = (*head)->ctx.gcpl_props.ginfo;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_ginfo() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_linfo
 *
 * Purpose:     Retrieves the link info property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_linfo(H5O_linfo_t *linfo)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(linfo);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, gcpl, H5P_OBJECT_CREATE_DEFAULT, H5G_CRT_LINK_INFO_NAME, linfo)

    /* Get the value */
    *linfo = (*head)->ctx.gcpl_props.linfo;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_linfo() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_userblock_size
 *
 * Purpose:     Retrieves the userblock size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_userblock_size(hsize_t *userblock_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(userblock_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_USER_BLOCK_NAME,
                                    userblock_size)

    /* Get the value */
    *userblock_size = (*head)->ctx.fcpl_props.userblock_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_userblock_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sizeof_addr
 *
 * Purpose:     Retrieves the size of address property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sizeof_addr(uint8_t *sizeof_addr)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sizeof_addr);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_ADDR_BYTE_NUM_NAME,
                                    sizeof_addr)

    /* Get the value */
    *sizeof_addr = (*head)->ctx.fcpl_props.sizeof_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_sizeof_addr() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sizeof_size
 *
 * Purpose:     Retrieves the size of size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sizeof_size(uint8_t *sizeof_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sizeof_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_OBJ_BYTE_NUM_NAME,
                                    sizeof_size)

    /* Get the value */
    *sizeof_size = (*head)->ctx.fcpl_props.sizeof_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_sizeof_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_sym_leaf_k
 *
 * Purpose:     Retrieves the symbol table leaf node size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_sym_leaf_k(unsigned *sym_leaf_k)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sym_leaf_k);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SYM_LEAF_NAME, sym_leaf_k)

    /* Get the value */
    *sym_leaf_k = (*head)->ctx.fcpl_props.sym_leaf_k;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_sym_leaf_k() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_btree_k
 *
 * Purpose:     Retrieves the B-tree rank property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_btree_k(unsigned *btree_k)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(btree_k);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_BTREE_RANK_NAME, btree_k)

    /* Get the value */
    memcpy(btree_k, (*head)->ctx.fcpl_props.btree_k, H5B_NUM_BTREE_ID * sizeof(unsigned));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_btree_k() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_file_space_page_size
 *
 * Purpose:     Retrieves the file space page size property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_file_space_page_size(hsize_t *fs_page_size)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fs_page_size);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_FILE_SPACE_PAGE_SIZE_NAME,
                                    fs_page_size)

    /* Get the value */
    *fs_page_size = (*head)->ctx.fcpl_props.fs_page_size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_file_space_page_size() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_file_space_strategy
 *
 * Purpose:     Retrieves the file space strategy property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_file_space_strategy(H5F_fspace_strategy_t *fs_strategy)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fs_strategy);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_FILE_SPACE_STRATEGY_NAME,
                                    fs_strategy)

    /* Get the value */
    *fs_strategy = (*head)->ctx.fcpl_props.fs_strategy;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_file_space_strategy() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_file_space_persist
 *
 * Purpose:     Retrieves the file space persist property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_file_space_persist(bool *fs_persist)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fs_persist);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_FREE_SPACE_PERSIST_NAME,
                                    fs_persist)

    /* Get the value */
    *fs_persist = (*head)->ctx.fcpl_props.fs_persist;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_file_space_persist() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_file_space_threshold
 *
 * Purpose:     Retrieves the file space threshold property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_file_space_threshold(hsize_t *fs_threshold)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(fs_threshold);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_FREE_SPACE_THRESHOLD_NAME,
                                    fs_threshold)

    /* Get the value */
    *fs_threshold = (*head)->ctx.fcpl_props.fs_threshold;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_file_space_threshold() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_shared_mesg_nindexes
 *
 * Purpose:     Retrieves the number of SOHM indexes property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_shared_mesg_nindexes(unsigned *sohm_nindexes)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(sohm_nindexes);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SHMSG_NINDEXES_NAME,
                                    sohm_nindexes)

    /* Get the value */
    *sohm_nindexes = (*head)->ctx.fcpl_props.sohm_nindexes;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_shared_mesg_nindexes() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_shared_mesg_btree_min
 *
 * Purpose:     Retrieves the SOHM btree minimum property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_shared_mesg_btree_min(unsigned *shmsg_btree_min)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(shmsg_btree_min);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SHMSG_BTREE_MIN_NAME,
                                    shmsg_btree_min)

    /* Get the value */
    *shmsg_btree_min = (*head)->ctx.fcpl_props.shmsg_btree_min;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_shared_mesg_btree_min() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_shared_mesg_list_max
 *
 * Purpose:     Retrieves the SOHM list max property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_shared_mesg_list_max(unsigned *shmsg_list_max)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(shmsg_list_max);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SHMSG_LIST_MAX_NAME,
                                    shmsg_list_max)

    /* Get the value */
    *shmsg_list_max = (*head)->ctx.fcpl_props.shmsg_list_max;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_shared_mesg_list_max() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_shared_mesg_index_types
 *
 * Purpose:     Retrieves the SOHM index types property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_shared_mesg_index_types(unsigned *shmsg_index_types)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(shmsg_index_types);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SHMSG_INDEX_TYPES_NAME,
                                    shmsg_index_types)

    /* Get the value */
    memcpy(shmsg_index_types, (*head)->ctx.fcpl_props.shmsg_index_types,
           H5O_SHMESG_MAX_NINDEXES * sizeof(unsigned));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_shared_mesg_index_types() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_shared_mesg_index_min_sizes
 *
 * Purpose:     Retrieves the SOHM index min sizes property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_shared_mesg_index_min_sizes(unsigned *shmsg_index_min_sizes)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(shmsg_index_min_sizes);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_SUBCLS_PROP_VALID(ocpl, fcpl, H5P_OBJECT_CREATE_DEFAULT, H5F_CRT_SHMSG_INDEX_MINSIZE_NAME,
                                    shmsg_index_min_sizes)

    /* Get the value */
    memcpy(shmsg_index_min_sizes, (*head)->ctx.fcpl_props.shmsg_index_min_sizes,
           H5O_SHMESG_MAX_NINDEXES * sizeof(unsigned));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_shared_mesg_index_min_sizes() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_attr_encoding
 *
 * Purpose:     Retrieves the attribute encoding property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_attr_encoding(H5T_cset_t *attr_encoding)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(attr_encoding);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.acpl_id);

    H5CX_RETRIEVE_PROP_VALID(acpl, H5P_ATTRIBUTE_CREATE_DEFAULT, H5P_STRCRT_CHAR_ENCODING_NAME, attr_encoding)

    /* Get the value */
    *attr_encoding = (*head)->ctx.acpl_props.attr_encoding;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_attr_encoding() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_ext_file_prefix
 *
 * Purpose:     Retrieves the pointer to the prefix for external file
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_ext_file_prefix(const char **extfile_prefix)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(extfile_prefix);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_EFILE_PREFIX_NAME, extfile_prefix)

    /* Get the value */
    *extfile_prefix = (*head)->ctx.dapl_props.extfile_prefix;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_ext_file_prefix() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_vds_prefix
 *
 * Purpose:     Retrieves the pointer to the prefix for VDS
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_vds_prefix(const char **vds_prefix)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vds_prefix);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_VDS_PREFIX_NAME, vds_prefix)

    /* Get the value */
    *vds_prefix = (*head)->ctx.dapl_props.vds_prefix;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_vds_prefix() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_append_flush
 *
 * Purpose:     Retrieves the append flush property for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_append_flush(H5D_append_flush_t *append_flush)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(append_flush);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_APPEND_FLUSH_NAME, append_flush)

    /* Get the value */
    *append_flush = (*head)->ctx.dapl_props.append_flush;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_append_flush() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_rdcc_nbytes
 *
 * Purpose:     Retrieves the size of the raw data cache for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_rdcc_nbytes(size_t *rdcc_nbytes)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(rdcc_nbytes);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_DATA_CACHE_BYTE_SIZE_NAME,
                             dapl_rdcc_nbytes)

    /* Get the value */
    *rdcc_nbytes = (*head)->ctx.dapl_props.dapl_rdcc_nbytes;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_rdcc_nbytes() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_rdcc_nslots
 *
 * Purpose:     Retrieves the number of slots in the raw data cache for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_rdcc_nslots(size_t *rdcc_nslots)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(rdcc_nslots);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_DATA_CACHE_NUM_SLOTS_NAME,
                             dapl_rdcc_nslots)

    /* Get the value */
    *rdcc_nslots = (*head)->ctx.dapl_props.dapl_rdcc_nslots;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_rdcc_nslots() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_rdcc_w0
 *
 * Purpose:     Retrieves the chunk cache preemption factor for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_rdcc_w0(double *rdcc_w0)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(rdcc_w0);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_PREEMPT_READ_CHUNKS_NAME, dapl_rdcc_w0)

    /* Get the value */
    *rdcc_w0 = (*head)->ctx.dapl_props.dapl_rdcc_w0;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_rdcc_w0() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_vds_printf_gap
 *
 * Purpose:     Retrieves the VDS printf gap for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_vds_printf_gap(hsize_t *vds_printf_gap)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vds_printf_gap);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_VDS_PRINTF_GAP_NAME, vds_printf_gap)

    /* Get the value */
    *vds_printf_gap = (*head)->ctx.dapl_props.vds_printf_gap;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_vds_printf_gap() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_vds_view
 *
 * Purpose:     Retrieves the VDS view for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_vds_view(H5D_vds_view_t *vds_view)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(vds_view);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.dapl_id);

    H5CX_RETRIEVE_PROP_VALID(dapl, H5P_DATASET_ACCESS_DEFAULT, H5D_ACS_VDS_VIEW_NAME, vds_view)

    /* Get the value */
    *vds_view = (*head)->ctx.dapl_props.vds_view;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_vds_view() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_dapl
 *
 * Purpose:     Resets the cached DAPL info for the current API call context.
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_dapl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset the DAPL flags to force the properties to be retrieved again */
    memset(&head->ctx.dapl_flags, 0, sizeof(head->ctx.dapl_flags));

    /* Retrieve the DAPL pointer again also */
    head->ctx.dapl    = NULL;
    head->ctx.dapl_id = H5P_DATASET_ACCESS_DEFAULT;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_dapl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_tag
 *
 * Purpose:     Sets the object tag for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_tag(haddr_t tag)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.tag = tag;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_tag() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_ring
 *
 * Purpose:     Sets the metadata cache ring for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_ring(H5AC_ring_t ring)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.ring = ring;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_ring() */

#ifdef H5_HAVE_SUBFILING_VFD
/*-------------------------------------------------------------------------
 * Function:    H5CX_set_sf_stub_file_id
 *
 * Purpose:     Sets the stub file ID for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_sf_stub_file_id(uint64_t sf_stub_file_id)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.sf_stub_file_id = sf_stub_file_id;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_sf_stub_file_id() */
#endif /* H5_HAVE_SUBFILING_VFD */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_want_posix_fd
 *
 * Purpose:     Sets the want POSIX file descriptor flag for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_want_posix_fd(bool want_posix_fd)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.want_posix_fd = want_posix_fd;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_want_posix_fd() */

#ifdef H5_HAVE_PARALLEL
/*-------------------------------------------------------------------------
 * Function:    H5CX_set_coll_metadata_read
 *
 * Purpose:     Sets the "do collective metadata reads" flag for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_coll_metadata_read(bool cmdr)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.coll_metadata_read = cmdr;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_coll_metadata_read() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpi_coll_datatypes
 *
 * Purpose:     Sets the MPI datatypes for collective I/O for the current API call context.
 *
 * Note:	This is only a shallow copy, the datatypes are not duplicated.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_mpi_coll_datatypes(MPI_Datatype btype, MPI_Datatype ftype)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context values */
    (*head)->ctx.btype = btype;
    (*head)->ctx.ftype = ftype;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_mpi_coll_datatypes() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_io_xfer_mode
 *
 * Purpose:     Sets the parallel transfer mode for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_io_xfer_mode(H5FD_mpio_xfer_t io_xfer_mode)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.dxpl_props.io_xfer_mode = io_xfer_mode;

    /* Mark the value as valid */
    (*head)->ctx.dxpl_flags.io_xfer_mode_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_io_xfer_mode() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_coll_opt
 *
 * Purpose:     Sets the parallel transfer mode for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_mpio_coll_opt(H5FD_mpio_collective_opt_t mpio_coll_opt)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.dxpl_props.mpio_coll_opt = mpio_coll_opt;

    /* Mark the value as valid */
    (*head)->ctx.dxpl_flags.mpio_coll_opt_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_mpio_coll_opt() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpi_file_flushing
 *
 * Purpose:     Sets the "flushing an MPI-opened file" flag for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpi_file_flushing(bool flushing)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.mpi_file_flushing = flushing;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpi_file_flushing() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_rank0_bcast
 *
 * Purpose:     Sets the "dataset meets read-with-rank0-and-bcast requirements" flag for the current API call
 *context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpio_rank0_bcast(bool rank0_bcast)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    (*head)->ctx.rank0_bcast = rank0_bcast;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpio_rank0_bcast() */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_vlen_alloc_info
 *
 * Purpose:     Sets the VL datatype alloc info for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_vlen_alloc_info(H5MM_allocate_t alloc_func, void *alloc_info, H5MM_free_t free_func, void *free_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.dxpl_props.vl_alloc_info.alloc_func = alloc_func;
    (*head)->ctx.dxpl_props.vl_alloc_info.alloc_info = alloc_info;
    (*head)->ctx.dxpl_props.vl_alloc_info.free_func  = free_func;
    (*head)->ctx.dxpl_props.vl_alloc_info.free_info  = free_info;

    /* Mark the value as valid */
    (*head)->ctx.dxpl_flags.vl_alloc_info_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_vlen_alloc_info() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_nlinks
 *
 * Purpose:     Sets the # of soft / UD links to traverse for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_nlinks(size_t nlinks)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.lapl_props.nlinks = nlinks;

    /* Mark the value as valid */
    (*head)->ctx.lapl_flags.nlinks_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_nlinks() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mdc_init_config
 *
 * Purpose:     Sets the initial metadata cache configuration for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_mdc_init_config(H5AC_cache_config_t *mdc_init_config)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    H5MM_memcpy(&(*head)->ctx.fapl_props.mdc_init_config, mdc_init_config, sizeof(*mdc_init_config));

    /* Mark the value as valid */
    (*head)->ctx.fapl_flags.mdc_init_config_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_mdc_init_config() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_close_degree
 *
 * Purpose:     Sets the file close degree for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_set_close_degree(H5F_close_degree_t close_degree)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set the API context value */
    (*head)->ctx.fapl_props.close_degree = close_degree;

    /* Mark the value as valid */
    (*head)->ctx.fapl_flags.close_degree_valid = true;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_set_close_degree() */

#ifdef H5_HAVE_PARALLEL

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_actual_chunk_opt
 *
 * Purpose:     Sets the actual chunk optimization used for parallel I/O for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpio_actual_chunk_opt(H5D_mpio_actual_chunk_opt_mode_t mpio_actual_chunk_opt)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    /* Cache the value for later, marking it to set in DXPL when context popped */
    (*head)->ctx.dxpl_props.mpio_actual_chunk_opt     = mpio_actual_chunk_opt;
    (*head)->ctx.dxpl_flags.mpio_actual_chunk_opt_set = true;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpio_actual_chunk_opt() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_actual_io_mode
 *
 * Purpose:     Sets the actual I/O mode used for parallel I/O for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpio_actual_io_mode(H5D_mpio_actual_io_mode_t mpio_actual_io_mode)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    /* Cache the value for later, marking it to set in DXPL when context popped */
    (*head)->ctx.dxpl_props.mpio_actual_io_mode     = mpio_actual_io_mode;
    (*head)->ctx.dxpl_flags.mpio_actual_io_mode_set = true;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpio_actual_chunk_opt() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_local_no_coll_cause
 *
 * Purpose:     Sets the local reason for breaking collective I/O for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpio_local_no_coll_cause(uint32_t mpio_local_no_coll_cause)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert((*head)->ctx.dxpl_id != H5P_DEFAULT);

    /* If we're using the default DXPL, don't modify it */
    if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT) {
        /* Cache the value for later, marking it to set in DXPL when context popped */
        (*head)->ctx.dxpl_props.mpio_local_no_coll_cause     = mpio_local_no_coll_cause;
        (*head)->ctx.dxpl_flags.mpio_local_no_coll_cause_set = true;
    } /* end if */

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpio_local_no_coll_cause() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_mpio_global_no_coll_cause
 *
 * Purpose:     Sets the global reason for breaking collective I/O for the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_mpio_global_no_coll_cause(uint32_t mpio_global_no_coll_cause)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert((*head)->ctx.dxpl_id != H5P_DEFAULT);

    /* If we're using the default DXPL, don't modify it */
    if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT) {
        /* Cache the value for later, marking it to set in DXPL when context popped */
        (*head)->ctx.dxpl_props.mpio_global_no_coll_cause     = mpio_global_no_coll_cause;
        (*head)->ctx.dxpl_flags.mpio_global_no_coll_cause_set = true;
    } /* end if */

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_mpio_global_no_coll_cause() */

#ifdef H5_HAVE_INSTRUMENTED_LIBRARY

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_link_hard
 *
 * Purpose:     Sets the instrumented "collective chunk link hard" value for the current API call context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_link_hard(int mpio_coll_chunk_link_hard)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_HARD_NAME, mpio_coll_chunk_link_hard)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_link_hard() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_multi_hard
 *
 * Purpose:     Sets the instrumented "collective chunk multi hard" value for the current API call context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_multi_hard(int mpio_coll_chunk_multi_hard)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_HARD_NAME, mpio_coll_chunk_multi_hard)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_multi_hard() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_link_num_true
 *
 * Purpose:     Sets the instrumented "collective chunk link num true" value for the current API call context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_link_num_true(int mpio_coll_chunk_link_num_true)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_NUM_TRUE_NAME, mpio_coll_chunk_link_num_true)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_link_num_true() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_link_num_false
 *
 * Purpose:     Sets the instrumented "collective chunk link num false" value for the current API call
 *context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_link_num_false(int mpio_coll_chunk_link_num_false)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_NUM_FALSE_NAME, mpio_coll_chunk_link_num_false)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_link_num_false() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_multi_ratio_coll
 *
 * Purpose:     Sets the instrumented "collective chunk multi ratio coll" value for the current API call
 *context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_multi_ratio_coll(int mpio_coll_chunk_multi_ratio_coll)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_RATIO_COLL_NAME, mpio_coll_chunk_multi_ratio_coll)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_multi_ratio_coll() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_chunk_multi_ratio_ind
 *
 * Purpose:     Sets the instrumented "collective chunk multi ratio ind" value for the current API call
 *context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_chunk_multi_ratio_ind(int mpio_coll_chunk_multi_ratio_ind)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_RATIO_IND_NAME, mpio_coll_chunk_multi_ratio_ind)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_chunk_multi_ratio_ind() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_test_set_mpio_coll_rank0_bcast
 *
 * Purpose:     Sets the instrumented "read-with-rank0-bcast" flag for the current API call context.
 *
 * Note:        Only sets value if property set in DXPL
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_test_set_mpio_coll_rank0_bcast(bool mpio_coll_rank0_bcast)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(!((*head)->ctx.dxpl_id == H5P_DEFAULT || (*head)->ctx.dxpl_id == H5P_DATASET_XFER_DEFAULT));

    H5CX_TEST_SET_PROP(H5D_XFER_COLL_RANK0_BCAST_NAME, mpio_coll_rank0_bcast)

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_test_set_mpio_coll_rank0_bcast() */
#endif /* H5_HAVE_INSTRUMENTED_LIBRARY */
#endif /* H5_HAVE_PARALLEL */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_no_selection_io_cause
 *
 * Purpose:     Sets the reason for not performing selection I/O for
 *              the current API call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_no_selection_io_cause(uint32_t no_selection_io_cause)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert((*head)->ctx.dxpl_id != H5P_DEFAULT);

    /* If we're using the default DXPL, don't modify it */
    if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT) {
        /* Cache the value for later, marking it to set in DXPL when context popped */
        (*head)->ctx.dxpl_props.no_selection_io_cause     = no_selection_io_cause;
        (*head)->ctx.dxpl_flags.no_selection_io_cause_set = true;
    } /* end if */

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_no_selection_io_cause() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_set_actual_selection_io_mode
 *
 * Purpose:     Sets the actual selection I/O mode for the current API
 *              call context.
 *
 * Return:      <none>
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_set_actual_selection_io_mode(uint32_t actual_selection_io_mode)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert((*head)->ctx.dxpl_id != H5P_DEFAULT);

    /* If we're using the default DXPL, don't modify it */
    if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT) {
        /* Cache the value for later, marking it to set in DXPL when context popped */
        (*head)->ctx.dxpl_props.actual_selection_io_mode     = actual_selection_io_mode;
        (*head)->ctx.dxpl_flags.actual_selection_io_mode_set = true;
    }

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_set_actual_selection_io_mode() */

#ifdef H5O_ENABLE_BAD_MESG_COUNT
/*-------------------------------------------------------------------------
 * Function:    H5CX_get_bad_mesg_count
 *
 * Purpose:     Retrieves the write a bad message count flag for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_bad_mesg_count(bool *bad_mesg_count)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(bad_mesg_count);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_BAD_MESG_COUNT_NAME, bad_mesg_count)

    /* Get the value */
    *bad_mesg_count = (*head)->ctx.ocpl_props.bad_mesg_count;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_bad_mesg_count() */
#endif /* H5O_ENABLE_BAD_MESG_COUNT */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_attr_max_compact
 *
 * Purpose:     Retrieves the maximum # of compact attributes for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_attr_max_compact(unsigned *attr_max_compact)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(attr_max_compact);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_ATTR_MAX_COMPACT_NAME, attr_max_compact)

    /* Get the value */
    *attr_max_compact = (*head)->ctx.ocpl_props.attr_max_compact;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_attr_max_compact() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_attr_min_dense
 *
 * Purpose:     Retrieves the minimum # of dense attributes for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_attr_min_dense(unsigned *attr_min_dense)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(attr_min_dense);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_ATTR_MIN_DENSE_NAME, attr_min_dense)

    /* Get the value */
    *attr_min_dense = (*head)->ctx.ocpl_props.attr_min_dense;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_attr_min_dense() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_ohdr_flags
 *
 * Purpose:     Retrieves the object header flags for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_ohdr_flags(uint8_t *ohdr_flags)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(ohdr_flags);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_OHDR_FLAGS_NAME, ohdr_flags)

    /* Get the value */
    *ohdr_flags = (*head)->ctx.ocpl_props.ohdr_flags;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_ohdr_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_pline
 *
 * Purpose:     Shallow copy the filter pipeline for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_pline(H5O_pline_t *pline)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(pline);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_PIPELINE_NAME, pline)

    /* Get the value */
    *pline = (*head)->ctx.ocpl_props.pline;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_peek_pline() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_pline
 *
 * Purpose:     Retrieves the filter pipeline for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_pline(H5O_pline_t *pline)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(pline);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpl, H5P_OBJECT_CREATE_DEFAULT, H5O_CRT_PIPELINE_NAME, pline)

    /* Make copy of filter pipeline */
    if (NULL == H5O_msg_copy(H5O_PLINE_ID, &(*head)->ctx.ocpl_props.pline, pline))
        HGOTO_ERROR(H5E_CONTEXT, H5E_CANTCOPY, FAIL, "can't copy filter pipeline");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* End H5CX_get_pline() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_is_def_ocpl
 *
 * Purpose:     Checks if the API context is using a library's default OCPL
 *
 * Return:      true / false (can't fail)
 *
 *-------------------------------------------------------------------------
 */
bool
H5CX_is_def_ocpl(void)
{
    H5CX_node_t **head        = NULL;  /* Pointer to head of API context list */
    bool          is_def_ocpl = false; /* Flag to indicate OCPL is default */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Set return value */
    is_def_ocpl = ((*head)->ctx.ocpl_id == H5P_LST_FILE_CREATE_ID_g ||
                   (*head)->ctx.ocpl_id == H5P_LST_DATASET_CREATE_ID_g ||
                   (*head)->ctx.ocpl_id == H5P_LST_GROUP_CREATE_ID_g ||
                   (*head)->ctx.ocpl_id == H5P_LST_DATATYPE_CREATE_ID_g ||
                   (*head)->ctx.ocpl_id == H5P_LST_MAP_CREATE_ID_g ||
                   (*head)->ctx.ocpl_id == H5P_LST_OBJECT_CREATE_ID_g);

    FUNC_LEAVE_NOAPI(is_def_ocpl)
} /* end H5CX_is_def_ocpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX__reset_ocpl
 *
 * Purpose:     Resets the cached OCPL info for the current API call context.
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
static void
H5CX__reset_ocpl(H5CX_node_t *head)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    assert(head);

    /* Reset cached DCPL/FCPL/GCPL/OCPL data */
    if (head->ctx.dcpl_flags.layout_valid)
        H5O_msg_reset(H5O_LAYOUT_ID, &head->ctx.dcpl_props.layout);
    if (head->ctx.ocpl_flags.pline_valid)
        H5O_msg_reset(H5O_PLINE_ID, &head->ctx.ocpl_props.pline);
    if (head->ctx.dcpl_flags.efl_valid)
        H5O_msg_reset(H5O_EFL_ID, &head->ctx.dcpl_props.efl);
    if (head->ctx.dcpl_flags.fill_value_valid)
        H5O_msg_reset(H5O_FILL_ID, &head->ctx.dcpl_props.fill_value);

    /* Reset the DCPL/FCPL/GCPL/OCPL flags to force the properties to be retrieved again */
    memset(&head->ctx.dcpl_flags, 0, sizeof(head->ctx.dcpl_flags));
    memset(&head->ctx.fcpl_flags, 0, sizeof(head->ctx.fcpl_flags));
    memset(&head->ctx.gcpl_flags, 0, sizeof(head->ctx.gcpl_flags));
    memset(&head->ctx.ocpl_flags, 0, sizeof(head->ctx.ocpl_flags));

    /* Retrieve the OCPL pointer again also */
    head->ctx.ocpl = NULL;

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX__reset_ocpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_reset_ocpl
 *
 * Purpose:     Reset the property cache for the API context's OCPL
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_reset_ocpl(void)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Reset the cached data */
    H5CX__reset_ocpl(*head);

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_reset_ocpl() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_peek_comm_dtype_merge_list
 *
 * Purpose:     Retrieves the pointer to the committed datatype merge list for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_peek_comm_dtype_merge_list(H5O_copy_dtype_merge_list_t **comm_dtype_merge_list)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(comm_dtype_merge_list);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpypl_id);

    /* This getter does not use H5CX_RETRIEVE_PROP_VALID in order to use
     * H5P_peek instead of H5P_get.  This prevents invocation of the property's
     * library-defined copy callback
     */
    H5CX_PEEK_PROP_VALID(ocpypl, H5P_OBJECT_COPY_DEFAULT, H5O_CPY_MERGE_COMM_DT_LIST_NAME,
                         comm_dtype_merge_list)

    /* Get the value */
    *comm_dtype_merge_list = (*head)->ctx.ocpypl_props.comm_dtype_merge_list;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_peek_comm_dtype_merge_list() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_mcdt_search_cb
 *
 * Purpose:     Retrieves the callback info for committed datatype search for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_mcdt_search_cb(H5O_mcdt_cb_info_t *mcdt_cb_info)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(mcdt_cb_info);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpypl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpypl, H5P_OBJECT_COPY_DEFAULT, H5O_CPY_MCDT_SEARCH_CB_NAME, mcdt_cb_info)

    /* Get the value */
    *mcdt_cb_info = (*head)->ctx.ocpypl_props.mcdt_cb_info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_mcdt_search_cb() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_get_cpy_options
 *
 * Purpose:     Retrieves the object copy options for the current API call context.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_get_cpy_options(unsigned *cpy_options)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    assert(cpy_options);
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);
    assert(H5P_DEFAULT != (*head)->ctx.ocpypl_id);

    H5CX_RETRIEVE_PROP_VALID(ocpypl, H5P_OBJECT_COPY_DEFAULT, H5O_CPY_OPTION_NAME, cpy_options)

    /* Get the value */
    *cpy_options = (*head)->ctx.ocpypl_props.cpy_options;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_get_cpy_options() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_pop
 *
 * Purpose:     Pops the context for an API call.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5CX_pop(bool update_dxpl_props)
{
    H5CX_node_t **head      = NULL;    /* Pointer to head of API context list */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Check for cached DXPL properties to return to application */
    if (update_dxpl_props) {
        /* actual_selection_io_mode is a special case - we always want to set it in the property list even if
         * it was never set by the library, in that case it indicates no I/O was performed and we don't want
         * to leave the (possibly incorrect) old value in the property list, so set from the default property
         * list */
        if ((*head)->ctx.dxpl_id != H5P_DATASET_XFER_DEFAULT &&
            !(*head)->ctx.dxpl_flags.actual_selection_io_mode_set) {
            (*head)->ctx.dxpl_props.actual_selection_io_mode = H5CX_def_dxpl_cache.actual_selection_io_mode;
            (*head)->ctx.dxpl_flags.actual_selection_io_mode_set = true;
        }

        H5CX_SET_PROP(H5D_XFER_NO_SELECTION_IO_CAUSE_NAME, no_selection_io_cause)
        H5CX_SET_PROP(H5D_XFER_ACTUAL_SELECTION_IO_MODE_NAME, actual_selection_io_mode)
#ifdef H5_HAVE_PARALLEL
        H5CX_SET_PROP(H5D_MPIO_ACTUAL_CHUNK_OPT_MODE_NAME, mpio_actual_chunk_opt)
        H5CX_SET_PROP(H5D_MPIO_ACTUAL_IO_MODE_NAME, mpio_actual_io_mode)
        H5CX_SET_PROP(H5D_MPIO_LOCAL_NO_COLLECTIVE_CAUSE_NAME, mpio_local_no_coll_cause)
        H5CX_SET_PROP(H5D_MPIO_GLOBAL_NO_COLLECTIVE_CAUSE_NAME, mpio_global_no_coll_cause)
#ifdef H5_HAVE_INSTRUMENTED_LIBRARY
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_HARD_NAME, mpio_coll_chunk_link_hard)
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_HARD_NAME, mpio_coll_chunk_multi_hard)
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_NUM_TRUE_NAME, mpio_coll_chunk_link_num_true)
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_LINK_NUM_FALSE_NAME, mpio_coll_chunk_link_num_false)
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_RATIO_COLL_NAME, mpio_coll_chunk_multi_ratio_coll)
        H5CX_SET_PROP(H5D_XFER_COLL_CHUNK_MULTI_RATIO_IND_NAME, mpio_coll_chunk_multi_ratio_ind)
        H5CX_SET_PROP(H5D_XFER_COLL_RANK0_BCAST_NAME, mpio_coll_rank0_bcast)
#endif /* H5_HAVE_INSTRUMENTED_LIBRARY */
#endif /* H5_HAVE_PARALLEL */
    }  /* end if */

    /* Reset any non-default property lists in the current context that have cached values that
     * need to be reset when the context is popped.
     */
    if (H5P_DATASET_ACCESS_DEFAULT != (*head)->ctx.dapl_id)
        H5CX__reset_dapl(*head);
    if (H5P_LINK_ACCESS_DEFAULT != (*head)->ctx.lapl_id)
        H5CX__reset_lapl(*head);
    if (H5P_OBJECT_CREATE_DEFAULT != (*head)->ctx.ocpl_id)
        H5CX__reset_ocpl(*head);
    if (H5P_FILE_ACCESS_DEFAULT != (*head)->ctx.fapl_id)
        H5CX__reset_fapl(*head);

    /* Pop the top context node from the stack */
    (*head) = (*head)->next;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5CX_pop() */
