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
 * Created:     H5FDquery.c
 *
 * Purpose:     VFD structure(s) query routines.
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
#include "H5private.h"  /* Generic Functions                        */
#include "H5Eprivate.h" /* Error handling                           */
#include "H5FDpkg.h"    /* File Drivers                             */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Package Typedefs */
/********************/

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

/*-------------------------------------------------------------------------
 * Function: H5FD_driver_get_value
 *
 * Purpose:  Retrieve the 'value' for the file driver class.
 *
 * Return:   Success:    Non-negative, the class 'value'
 *           Failure:    (can't happen)
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE H5FD_class_value_t
H5FD_driver_get_value(const H5FD_driver_t *driver)
{
    /* Use FUNC_ENTER_NOAPI_NOINIT_NOERR here to avoid performance issues */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(driver);
    assert(driver->cls);

    FUNC_LEAVE_NOAPI(driver->cls->value)
} /* end H5FD_driver_get_value() */

/*-------------------------------------------------------------------------
 * Function: H5FD_get_fc_degree
 *
 * Purpose:  Retrieve the 'file close degree' for the file driver.
 *
 * Return:   Success:    Non-negative, the 'file close degree'
 *           Failure:    (can't happen)
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE H5F_close_degree_t
H5FD_get_fc_degree(const H5FD_int_t *fh)
{
    /* Use FUNC_ENTER_NOAPI_NOINIT_NOERR here to avoid performance issues */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);

    FUNC_LEAVE_NOAPI(fh->driver->cls->fc_degree)
} /* end H5FD_get_fc_degree() */

/*-------------------------------------------------------------------------
 * Function: H5FD_driver_has_cmp
 *
 * Purpose:  Check if a driver has a 'cmp' callback defined
 *
 * Return:   Success:    Non-negative - true or false
 *           Failure:    Negative (should not happen)
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE bool
H5FD_driver_has_cmp(const H5FD_driver_t *driver)
{
    /* Use FUNC_ENTER_NOAPI_NOINIT_NOERR here to avoid performance issues */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(driver);
    assert(driver->cls);

    FUNC_LEAVE_NOAPI(!!(driver->cls->cmp))
} /* end H5FD_driver_has_cmp() */

/*-------------------------------------------------------------------------
 * Function: H5FD_driver_has_lock
 *
 * Purpose:  Check if a driver has a 'lock' callback defined
 *
 * Return:   Success:    Non-negative - true or false
 *           Failure:    Negative (should not happen)
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE bool
H5FD_driver_has_lock(const H5FD_driver_t *driver)
{
    /* Use FUNC_ENTER_NOAPI_NOINIT_NOERR here to avoid performance issues */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(driver);
    assert(driver->cls);

    FUNC_LEAVE_NOAPI(!!(driver->cls->lock))
} /* end H5FD_driver_has_lock() */

/*-------------------------------------------------------------------------
 * Function: H5FD_driver_has_vector_select_io
 *
 * Purpose:  Determine if vector or selection I/O is supported by this file
 *
 * Return:   true/false
 *
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE bool
H5FD_driver_has_vector_select_io(const H5FD_int_t *fh, bool is_write)
{
    bool ret_value = false; /* Return value */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);

    if (is_write)
        ret_value = (fh->driver->cls->write_vector || fh->driver->cls->write_selection);
    else
        ret_value = (fh->driver->cls->read_vector || fh->driver->cls->read_selection);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_has_vector_select_io */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_fs_type_map
 *
 * Purpose:     Retrieve the free space type mapping for the VFD
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_get_fs_type_map(const H5FD_int_t *fh, H5FD_mem_t *type_map)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(type_map);

    /* Check for VFD class providing a type map retrieval routine */
    if (fh->driver->cls->get_type_map) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->get_type_map)(fh->file, type_map);
            }
        H5_AFTER_USER_CB(FAIL)
        /* Retrieve type mapping for this file */
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "driver get type map failed");
    } /* end if */
    else
        /* Copy class's default free space type mapping */
        H5MM_memcpy(type_map, fh->driver->cls->fl_map, sizeof(fh->driver->cls->fl_map));

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_fs_type_map() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_fileno
 *
 * Purpose:     Quick and dirty routine to retrieve the file's 'fileno' value
 *              (Mainly added to stop non-file routines from poking about in the
 *              H5FD_t data structure)
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_get_fileno(const H5FD_int_t *fh, unsigned long *filenum)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);
    assert(filenum);

    /* Retrieve the file's serial number */
    *filenum = fh->file->fileno;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_get_fileno() */

/*-------------------------------------------------------------------------
 * Function: H5FD_has_feature
 *
 * Purpose:  Check if a file has a particular feature enabled
 *
 * Return:   Success:    Non-negative - true or false
 *           Failure:    Negative (should not happen)
 *-------------------------------------------------------------------------
 */
H5_ATTR_PURE bool
H5FD_has_feature(const H5FD_int_t *fh, unsigned feature)
{
    /* Use FUNC_ENTER_NOAPI_NOINIT_NOERR here to avoid performance issues */
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(fh);
    assert(fh->file);

    FUNC_LEAVE_NOAPI((bool)(fh->file->feature_flags & feature))
} /* end H5FD_has_feature() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_feature_flags
 *
 * Purpose:     Retrieve the feature flags for the VFD
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_get_feature_flags(const H5FD_int_t *fh, unsigned long *feature_flags)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);
    assert(feature_flags);

    /* Set feature flags to return */
    *feature_flags = fh->file->feature_flags;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_get_feature_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_set_feature_flags
 *
 * Purpose:     Set the feature flags for the VFD
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_set_feature_flags(H5FD_int_t *fh, unsigned long feature_flags)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);

    /* Set the file's feature flags */
    fh->file->feature_flags = feature_flags;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_set_feature_flags() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_maxaddr
 *
 * Purpose:     Private version of H5FDget_maxaddr()
 *
 * Return:      Success:    The maximum address allowed in the file.
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_get_maxaddr(const H5FD_int_t *fh)
{
    haddr_t ret_value = HADDR_UNDEF; /* Return value */

    FUNC_ENTER_NOAPI(HADDR_UNDEF)

    /* Sanity checks */
    assert(fh);
    assert(fh->file);

    /* Set return value */
    ret_value = fh->file->maxaddr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_maxaddr() */

/*--------------------------------------------------------------------------
 * Function:    H5FD_get_base_addr
 *
 * Purpose:     Get the base address for the file
 *
 * Return:      Success:    The absolute base address of the file
 *                          (Can't fail)
 *
 *--------------------------------------------------------------------------
 */
H5_ATTR_PURE haddr_t
H5FD_get_base_addr(const H5FD_int_t *fh)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);

    /* Return the file's base address */
    FUNC_LEAVE_NOAPI(fh->file->base_addr)
} /* end H5FD_get_base_addr() */

/*--------------------------------------------------------------------------
 * Function:    H5FD_set_base_addr
 *
 * Purpose:     Set the base address for the file
 *
 * Return:      SUCCEED (Can't fail)
 *
 *--------------------------------------------------------------------------
 */
herr_t
H5FD_set_base_addr(H5FD_int_t *fh, haddr_t base_addr)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);
    assert(H5_addr_defined(base_addr));

    /* Set the file's base address */
    fh->file->base_addr = base_addr;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_set_base_addr() */

/*--------------------------------------------------------------------------
 * Function:    H5FD_set_paged_aggr
 *
 * Purpose:     Set "paged_aggr" for the file.
 *
 * Return:      SUCCEED (Can't fail)
 *
 *--------------------------------------------------------------------------
 */
herr_t
H5FD_set_paged_aggr(H5FD_int_t *fh, bool paged)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->file);

    /* Indicate whether paged aggregation for handling file space is enabled or not */
    fh->file->paged_aggr = paged;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD_set_paged_aggr() */
