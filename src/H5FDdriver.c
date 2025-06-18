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
 * Created:     H5FDdriver.c
 *
 * Purpose:     Internal VFD callback equivalents
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5CXprivate.h"
#include "H5FDmodule.h" /* This source code file is part of the H5FD module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5FDpkg.h"     /* File Drivers                             */
#include "H5FLprivate.h" /* Free Lists                               */
#include "H5Iprivate.h"  /* IDs                                      */
#include "H5Pprivate.h"  /* Property lists                           */

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

/* Declare a free list to manage the H5FD_int_t struct */
H5FL_DEFINE(H5FD_int_t);

/*-------------------------------------------------------------------------
 * Function:    H5FD_sb_size
 *
 * Purpose:     Obtains the number of bytes required to store the driver file
 *              access data in the HDF5 superblock.
 *
 * Return:      Success:    Number of bytes required. May be zero if the
 *                          driver has no data to store in the superblock.
 *
 *              Failure:    This function cannot indicate errors.
 *
 *-------------------------------------------------------------------------
 */
hsize_t
H5FD_sb_size(H5FD_int_t *fh)
{
    hsize_t ret_value = 0;

    FUNC_ENTER_NOAPI_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->sb_size) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB_NOERR(0)
            {
                ret_value = (fh->driver->cls->sb_size)(fh->file);
            }
        H5_AFTER_USER_CB_NOERR(0)
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sb_size() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_sb_encode
 *
 * Purpose:     Encode driver-specific data into the output arguments. The
 *              NAME is a nine-byte buffer which should get an
 *              eight-character driver name and/or version followed by a null
 *              terminator. The BUF argument is a buffer to receive the
 *              encoded driver-specific data. The size of the BUF array is
 *              the size returned by the H5FD_sb_size() call.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_sb_encode(H5FD_int_t *fh, char *name /*out*/, uint8_t *buf)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->sb_encode) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->sb_encode)(fh->file, name /*out*/, buf /*out*/);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver sb_encode request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sb_encode() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sb_decode
 *
 * Purpose:     Decodes the driver information block.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__sb_decode(H5FD_int_t *fh, const char *name, const uint8_t *buf)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->sb_decode) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->sb_decode)(fh->file, name, buf);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver sb_decode request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sb_decode() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_fapl_get
 *
 * Purpose:     Gets the file access property list associated with a file.
 *              Usually the file will copy what it needs from the original
 *              file access property list when the file is created. The
 *              purpose of this function is to create a new file access
 *              property list based on the settings in the file, which may
 *              have been modified from the original file access property
 *              list.
 *
 * Return:      Success:    Pointer to a new file access property list
 *                          with all members copied.  If the file is
 *                          closed then this property list lives on, and
 *                          vice versa.
 *
 *                          This can be NULL if the file has no properties.
 *
 *              Failure:    This function cannot indicate errors.
 *
 *-------------------------------------------------------------------------
 */
void *
H5FD_fapl_get(H5FD_int_t *fh)
{
    void *ret_value = NULL;

    FUNC_ENTER_NOAPI_NOERR

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->fapl_get) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB_NOERR(NULL)
            {
                ret_value = (fh->driver->cls->fapl_get)(fh->file);
            }
        H5_AFTER_USER_CB_NOERR(NULL)
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_fapl_get() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_free_driver_info
 *
 * Purpose:     Frees a driver's info
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_free_driver_info(const H5FD_driver_t *driver, const void *driver_info)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    if (driver && driver_info) {
        /* Allow driver to free info or do it ourselves */
        if (driver->cls->fapl_free) {
            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(FAIL)
                {
                    /* Free the const pointer */
                    /* (Cast through uintptr_t to de-const memory) */
                    ret_value = (driver->cls->fapl_free)((void *)(uintptr_t)driver_info);
                }
            H5_AFTER_USER_CB(FAIL)
            if (ret_value < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTFREE, FAIL, "driver free request failed");
        }
        else
            driver_info = H5MM_xfree_const(driver_info);
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_free_driver_info() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_open_wrap
 *
 * Purpose:     Wrapper around H5FD_open that saves and restores the current
 *              API context state.  Must be used by a routine that passes a
 *              different FAPL to H5FD_open than the routine was called with.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_open_wrap(bool try, H5FD_int_t **_fh, const char *name, unsigned flags, H5P_genplist_t *fapl,
               haddr_t maxaddr)
{
    hid_t              old_fapl_id = H5I_INVALID_HID; /* ID for old FAPL in API context */
    H5F_close_degree_t old_fc_degree;                 /* file close degree        */
    herr_t             ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Retrieve the current FAPL in the API context */
    if ((old_fapl_id = H5CX_get_fapl()) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file access property list");
    H5CX_get_close_degree(&old_fc_degree);

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(H5P_PLIST_ID(fapl), H5I_INVALID_HID, false) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set access property list info");

    /* Call actual H5FD_open routine */
    if (H5FD_open(try, _fh, name, flags, fapl, maxaddr) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "can't open file");

done:
    /* Restore previous FAPL in the API context */
    if (old_fapl_id > 0) {
        H5CX_set_fapl(old_fapl_id);
        H5CX_set_close_degree(old_fc_degree);
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_open_wrap() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_open
 *
 * Purpose:     Opens a file named NAME for the type(s) of access described
 *              by the bit vector FLAGS according to a file access
 *              property list FAPL_ID (which may be the constant H5P_DEFAULT).
 *              The file should expect to handle format addresses in the range
 *              [0, MAXADDR] (if MAXADDR is the undefined address then the
 *              caller doesn't care about the address range).
 *
 *              If the 'try' flag is true, the VFD 'open' callback is called
 *              with errors paused and not opening the file is not treated as
 *              an error; SUCCEED is returned, with the file ptr set to NULL.
 *              If 'try' is false, the VFD 'open' callback is made with errors
 *              unpaused and a failure generates an error.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_open(bool try, H5FD_int_t **_fh, const char *name, unsigned flags, H5P_genplist_t *fapl, haddr_t maxaddr)
{
    H5FD_int_t            *fh   = NULL;         /* File handle */
    H5FD_t                *file = NULL;         /* File opened */
    H5FD_driver_t         *driver;              /* VFD for file */
    unsigned long          driver_flags = 0;    /* File-inspecific driver feature flags */
    H5FD_file_image_info_t file_image_info;     /* Initial file image */
    herr_t                 ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Reset 'out' parameter */
    *_fh = NULL;

    /* Sanity checks */
    if (0 == maxaddr)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "zero format address range");

    /* Get the VFD to open the file with */
    if (NULL == (driver = H5CX_peek_driver()))
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to retrieve VFL driver");
    if (NULL == driver->cls->open)
        HGOTO_ERROR(H5E_VFL, H5E_UNSUPPORTED, FAIL, "file driver has no `open' method");

    /* Query driver flag */
    if (H5FD_driver_query(driver, &driver_flags) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "can't query VFD flags");

    /* Get initial file image info */
    if (H5CX_peek_file_image_info(&file_image_info) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, FAIL, "can't get file image info");

    /* If an image is provided, make sure the driver supports this feature */
    assert((file_image_info.buffer && file_image_info.size > 0) ||
           (!file_image_info.buffer && file_image_info.size == 0));
    if (file_image_info.buffer && !(driver_flags & H5FD_FEAT_ALLOW_FILE_IMAGE))
        HGOTO_ERROR(H5E_VFL, H5E_UNSUPPORTED, FAIL, "file image set, but not supported.");

    if (HADDR_UNDEF == maxaddr)
        maxaddr = driver->cls->maxaddr;

    /* clang-format off */

    /* Try dispatching to file driver */
    if (try) {
        H5E_PAUSE_ERRORS
            {/* Prepare & restore library for user callback */
                 H5_BEFORE_USER_CB(FAIL)
                    {
                        file = (driver->cls->open)(name, flags, H5P_PLIST_ID(fapl), maxaddr);
                    }
                H5_AFTER_USER_CB(FAIL)
            }
        H5E_RESUME_ERRORS

        /* Check if file was not opened */
        if (NULL == file)
            HGOTO_DONE(SUCCEED);
    }
    else
    {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                file = (driver->cls->open)(name, flags, H5P_PLIST_ID(fapl), maxaddr);
            }
        H5_AFTER_USER_CB(FAIL)
        if (NULL == file)
            HGOTO_ERROR(H5E_VFL, H5E_CANTOPENFILE, FAIL, "can't open file");
    }

    /* Set the file access flags */
    file->access_flags = flags;

    /* Fill in public fields. We must increment the reference count on the
     * driver to prevent it from being freed while this file is open.
     */
    if (H5FD__driver_inc_rc(driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINC, FAIL, "unable to increment ref count on VFL driver");
    file->driver_id = H5I_INVALID_HID;
    file->cls     = driver->cls;
    file->maxaddr = maxaddr;
    if (H5CX_get_alignment(&file->alignment, &file->threshold) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't get alignment info");

    /* Increment the global serial number & assign it to this H5FD_t object */
    if (++H5FD_file_serial_no_p == 0)
        /* (Just error out if we wrap around for now...) */
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to get file serial number");
    file->fileno = H5FD_file_serial_no_p;

    /* Start with base address set to 0 */
    /* (This will be changed later, when the superblock is located) */
    file->base_addr = 0;

    /* Set up internal file handle */
    if (NULL == (fh = H5FL_MALLOC(H5FD_int_t)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, FAIL, "unable to allocate internal file handle");
    fh->driver = driver;
    fh->file = file;

    /* Retrieve the VFL driver feature flags */
    if (H5FD__query(fh, &file->feature_flags) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to query file driver");

    /* Set 'out' parameter */
    *_fh = fh;

/* clang-format on */

done :
    /* Can't cleanup 'file' information, since we don't know what type it is */
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_open() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_close
 *
 * Purpose:     Private version of H5FDclose()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_close(H5FD_int_t *fh)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->driver->cls->close);
    assert(fh->file);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to the driver for actual close. If the driver fails to
             * close the file then the file will be in an unusable state.
             */
            ret_value = (fh->driver->cls->close)(fh->file);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEFILE, FAIL, "close failed");

    /* Decrement files using the driver */
    if (H5FD__driver_dec_rc(fh->driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "can't decrement driver ref count");

    /* Release internal file handle */
    H5FL_FREE(H5FD_int_t, fh);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_close() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_cmp
 *
 * Purpose:     Private version of H5FDcmp()
 *
 * Return:      Success:    A value like strcmp()
 *
 *              Failure:    Must never fail.
 *
 *-------------------------------------------------------------------------
 */
int
H5FD_cmp(const H5FD_int_t *fh1, const H5FD_int_t *fh2)
{
    int ret_value = -1; /* Return value */

    FUNC_ENTER_NOAPI_NOERR

    if ((!fh1 || !fh1->driver->cls) && (!fh2 || !fh2->driver->cls))
        HGOTO_DONE(0);
    if (!fh1 || !fh1->driver->cls)
        HGOTO_DONE(-1);
    if (!fh2 || !fh2->driver->cls)
        HGOTO_DONE(1);
    if (fh1->driver->cls < fh2->driver->cls)
        HGOTO_DONE(-1);
    if (fh1->driver->cls > fh2->driver->cls)
        HGOTO_DONE(1);

    /* Files are same driver; no cmp callback */
    if (!fh1->driver->cls->cmp) {
        if (fh1 < fh2)
            HGOTO_DONE(-1);
        if (fh1 > fh2)
            HGOTO_DONE(1);
        HGOTO_DONE(0);
    }

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB_NOCHECK
        {
            /* Dispatch to driver */
            ret_value = (fh1->driver->cls->cmp)(fh1->file, fh2->file);
        }
    H5_AFTER_USER_CB_NOCHECK

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__query
 *
 * Purpose:     Private version of H5FDquery()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__query(const H5FD_int_t *fh, unsigned long *flags /*out*/)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(flags);

    /* Dispatch to driver (if available) */
    if (fh->driver->cls->query) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->query)(fh->file, flags);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to query feature flags");
    }
    else
        *flags = 0;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__query() */

/*-------------------------------------------------------------------------
 * Function:     H5FD_driver_query
 *
 * Purpose:      Similar to H5FD_query(), but intended for cases when we don't
 *               have a file available (e.g. before one is opened). Since we
 *               can't use the file to get the driver, the driver is passed in
 *               as a parameter.
 *
 * Return:       SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_driver_query(const H5FD_driver_t *driver, unsigned long *flags /*out*/)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(driver);
    assert(driver->cls);
    assert(flags);

    /* Check for the driver to query and then query it */
    if (driver->cls->query) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (driver->cls->query)(NULL, flags);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to query feature flags");
    }
    else
        *flags = 0;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_driver_query() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_eoa
 *
 * Purpose:     Private version of H5FDget_eoa()
 *
 *              This function returns the EOA as a RELATIVE address, i.e.
 *              relative to the base address.  This is NOT the same as the
 *              EOA stored in the superblock, which is an absolute
 *              address.  Object addresses are relative.
 *
 * Return:      Success:    First byte after allocated memory
 *
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_get_eoa(const H5FD_int_t *fh, H5FD_mem_t type)
{
    haddr_t ret_value = HADDR_UNDEF; /* Return value */

    FUNC_ENTER_NOAPI(HADDR_UNDEF)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(HADDR_UNDEF)
        {
            /* Dispatch to driver */
            ret_value = (fh->driver->cls->get_eoa)(fh->file, type);
        }
    H5_AFTER_USER_CB(HADDR_UNDEF)
    if (!H5_addr_defined(ret_value))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, HADDR_UNDEF, "driver get_eoa request failed");

    /* Adjust for base address in file (convert to relative address) */
    ret_value -= fh->file->base_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_set_eoa
 *
 * Purpose:     Private version of H5FDset_eoa()
 *
 *              This function expects the EOA is a RELATIVE address, i.e.
 *              relative to the base address.  This is NOT the same as the
 *              EOA stored in the superblock, which is an absolute
 *              address.  Object addresses are relative.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_set_eoa(H5FD_int_t *fh, H5FD_mem_t type, haddr_t addr)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(H5_addr_defined(addr) && addr <= fh->file->maxaddr);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to driver, convert to absolute address */
            ret_value = (fh->driver->cls->set_eoa)(fh->file, type, addr + fh->file->base_addr);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver set_eoa request failed");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_set_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_eof
 *
 * Purpose:     Private version of H5FDget_eof()
 *
 *              This function returns the EOF as a RELATIVE address, i.e.
 *              relative to the base address.  This will be different
 *              from  the end of the physical file if there is a user
 *              block.
 *
 * Return:      Success:    The EOF address.
 *
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_get_eof(const H5FD_int_t *fh, H5FD_mem_t type)
{
    haddr_t ret_value = HADDR_UNDEF; /* Return value */

    FUNC_ENTER_NOAPI(HADDR_UNDEF)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->get_eof) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(HADDR_UNDEF)
            {
                ret_value = (fh->driver->cls->get_eof)(fh->file, type);
            }
        H5_AFTER_USER_CB(HADDR_UNDEF)
        if (!H5_addr_defined(ret_value))
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "driver get_eof request failed");
    }
    else
        ret_value = fh->file->maxaddr;

    /* Adjust for base address in file (convert to relative address)  */
    ret_value -= fh->file->base_addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_eof() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_get_vfd_handle_wrap
 *
 * Purpose:     Wrapper around H5FD_get_vfd_handle that saves and restores the
 *              current API context state.  Must be used by a routine that passes
 *              a different FAPL to H5FD_get_vfd_handle than the routine was
 *              called with.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_get_vfd_handle_wrap(H5FD_int_t *fh, const H5P_genplist_t *fapl, void **file_handle)
{
    hid_t  old_fapl_id = H5I_INVALID_HID; /* ID for old FAPL in API context */
    herr_t ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Retrieve the current FAPL in the API context */
    if ((old_fapl_id = H5CX_get_fapl()) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file access property list");

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(H5P_PLIST_ID(fapl), H5I_INVALID_HID, false) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set access property list info");

    /* Call actual H5FD_get_vfd_handle routine */
    if (H5FD_get_vfd_handle(fh, fapl, file_handle) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file handle");

done:
    /* Restore previous FAPL in the API context */
    if (old_fapl_id > 0)
        H5CX_set_fapl(old_fapl_id);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_vfd_handle_wrap() */

/*--------------------------------------------------------------------------
 * Function:    H5FD_get_vfd_handle
 *
 * Purpose:     Private version of H5FDget_vfd_handle()
 *
 * Return:      SUCCEED/FAIL
 *
 *--------------------------------------------------------------------------
 */
herr_t
H5FD_get_vfd_handle(H5FD_int_t *fh, const H5P_genplist_t *fapl, void **file_handle)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(file_handle);

    /* Dispatch to driver */
    if (NULL == fh->driver->cls->get_handle)
        HGOTO_ERROR(H5E_VFL, H5E_UNSUPPORTED, FAIL, "file driver has no `get_vfd_handle' method");

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            ret_value = (fh->driver->cls->get_handle)(fh->file, H5P_PLIST_ID(fapl), file_handle);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file handle for file driver");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_get_vfd_handle() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_read
 *
 * Purpose:     Private version of H5FDread()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_read(H5FD_int_t *fh, H5FD_mem_t type, haddr_t addr, size_t size, void *buf /*out*/)
{
    H5FD_t  *file;
    uint32_t actual_selection_io_mode;
    herr_t   ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(buf);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == size)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    /* If the file is open for SWMR read access, allow access to data past
     * the end of the allocated space (the 'eoa').  This is done because the
     * eoa stored in the file's superblock might be out of sync with the
     * objects being written within the file by the application performing
     * SWMR write operations.
     */
    if (!(file->access_flags & H5F_ACC_SWMR_READ)) {
        haddr_t eoa;

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                eoa = (fh->driver->cls->get_eoa)(file, type);
            }
        H5_AFTER_USER_CB(FAIL)
        if (!H5_addr_defined(eoa))
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");

        if ((addr + file->base_addr + size) > eoa)
            HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size = %llu, eoa = %llu",
                        (unsigned long long)(addr + file->base_addr), (unsigned long long)size,
                        (unsigned long long)eoa);
    }

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to driver */
            ret_value =
                (fh->driver->cls->read)(file, type, H5CX_get_dxpl(), addr + file->base_addr, size, buf);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver read request failed");

    /* Set actual selection I/O, if this is a raw data operation */
    if (type == H5FD_MEM_DRAW) {
        H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
        actual_selection_io_mode |= H5D_SCALAR_IO;
        H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_read() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_write
 *
 * Purpose:     Private version of H5FDwrite()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_write(H5FD_int_t *fh, H5FD_mem_t type, haddr_t addr, size_t size, const void *buf)
{
    H5FD_t  *file;
    uint32_t actual_selection_io_mode;
    haddr_t  eoa       = HADDR_UNDEF; /* EOA for file */
    herr_t   ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(buf);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == size)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            eoa = (fh->driver->cls->get_eoa)(file, type);
        }
    H5_AFTER_USER_CB(FAIL)
    if (!H5_addr_defined(eoa))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");
    if ((addr + file->base_addr + size) > eoa)
        HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size=%llu, eoa=%llu",
                    (unsigned long long)(addr + file->base_addr), (unsigned long long)size,
                    (unsigned long long)eoa);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to driver */
            ret_value =
                (fh->driver->cls->write)(file, type, H5CX_get_dxpl(), addr + file->base_addr, size, buf);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "driver write request failed");

    /* Set actual selection I/O, if this is a raw data operation */
    if (type == H5FD_MEM_DRAW) {
        H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
        actual_selection_io_mode |= H5D_SCALAR_IO;
        H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_write() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_read_vector
 *
 * Purpose:     Private version of H5FDread_vector()
 *
 *              Perform count reads from the specified file at the offsets
 *              provided in the addrs array, with the lengths and memory
 *              types provided in the sizes and types arrays.  Data read
 *              is returned in the buffers provided in the bufs array.
 *
 *              If i > 0 and sizes[i] == 0, presume sizes[n] = sizes[i-1]
 *              for all n >= i and < count.
 *
 *              Similarly, if i > 0 and types[i] == H5FD_MEM_NOLIST,
 *              presume types[n] = types[i-1] for all n >= i and < count.
 *
 *              If the underlying VFD supports vector reads, pass the
 *              call through directly.
 *
 *              If it doesn't, convert the vector read into a sequence
 *              of individual reads.
 *
 *              Note that it is not in general possible to convert a
 *              vector read into a selection read, because each element
 *              in the vector read may have a different memory type.
 *              In contrast, selection reads are of a single type.
 *
 * Return:      Success:    SUCCEED
 *                          All reads have completed successfully, and
 *                          the results havce been into the supplied
 *                          buffers.
 *
 *              Failure:    FAIL
 *                          The contents of supplied buffers are undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_read_vector(H5FD_int_t *fh, uint32_t count, H5FD_mem_t types[], haddr_t addrs[], size_t sizes[],
                 void *bufs[] /* out */)
{
    H5FD_t    *file;
    H5FD_mem_t type         = H5FD_MEM_DEFAULT;
    size_t     size         = 0;
    bool       is_raw       = false; /* Does this include raw data */
    bool       addrs_cooked = false;
    bool       extend_sizes = false;
    bool       extend_types = false;
    uint32_t   i;
    herr_t     ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(types || count == 0);
    assert(addrs || count == 0);
    assert(sizes || count == 0);
    assert(bufs || count == 0);

    /* Verify that the first elements of the sizes and types arrays are
     * valid.
     */
    assert(count == 0 || sizes[0] != 0);
    assert(count == 0 || types[0] != H5FD_MEM_NOLIST);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == count)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    if (file->base_addr > 0) {
        /* apply the base_addr offset to the addrs array.  Must undo before
         * we return.
         */
        for (i = 0; i < count; i++)
            addrs[i] += file->base_addr;
        addrs_cooked = true;
    }

    /* If the file is open for SWMR read access, allow access to data past
     * the end of the allocated space (the 'eoa').  This is done because the
     * eoa stored in the file's superblock might be out of sync with the
     * objects being written within the file by the application performing
     * SWMR write operations.
     */
    if (!(file->access_flags & H5F_ACC_SWMR_READ) && count > 0) {
        haddr_t eoa;

        extend_sizes = false;
        extend_types = false;
        for (i = 0; i < count; i++) {
            if (!extend_sizes) {
                if (sizes[i] == 0) {
                    size         = sizes[i - 1];
                    extend_sizes = true;
                }
                else
                    size = sizes[i];
            }

            if (!extend_types) {
                if (types[i] == H5FD_MEM_NOLIST) {
                    type         = types[i - 1];
                    extend_types = true;
                }
                else {
                    type = types[i];

                    /* Check for raw data operation */
                    if (type == H5FD_MEM_DRAW)
                        is_raw = true;
                }
            }

            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(FAIL)
                {
                    eoa = (fh->driver->cls->get_eoa)(file, type);
                }
            H5_AFTER_USER_CB(FAIL)
            if (!H5_addr_defined(eoa))
                HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");

            if ((addrs[i] + size) > eoa)
                HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL,
                            "addr overflow, addrs[%d] = %llu, sizes[%d] = %llu, eoa = %llu", (int)i,
                            (unsigned long long)(addrs[i]), (int)i, (unsigned long long)size,
                            (unsigned long long)eoa);
        }
    }
    else
        /* We must still check if this is a raw data read */
        for (i = 0; i < count && types[i] != H5FD_MEM_NOLIST; i++)
            if (types[i] == H5FD_MEM_DRAW) {
                is_raw = true;
                break;
            }

    /* if the underlying VFD supports vector read, make the call */
    if (fh->driver->cls->read_vector) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value =
                    (fh->driver->cls->read_vector)(file, H5CX_get_dxpl(), count, types, addrs, sizes, bufs);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver read vector request failed");

        /* Set actual selection I/O mode, if this is a raw data operation */
        if (is_raw) {
            uint32_t actual_selection_io_mode;

            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_VECTOR_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }
    else {
        /* Otherwise, implement the vector read as a sequence of regular
         * read calls.
         */
        uint32_t no_selection_io_cause;
        uint32_t actual_selection_io_mode;

        extend_sizes = false;
        extend_types = false;
        for (i = 0; i < count; i++) {
            /* we have already verified that sizes[0] != 0 and
             * types[0] != H5FD_MEM_NOLIST
             */
            if (!extend_sizes) {
                if (sizes[i] == 0) {
                    size         = sizes[i - 1];
                    extend_sizes = true;
                }
                else
                    size = sizes[i];
            }

            if (!extend_types) {
                if (types[i] == H5FD_MEM_NOLIST) {
                    type         = types[i - 1];
                    extend_types = true;
                }
                else
                    type = types[i];
            }

            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(FAIL)
                {
                    ret_value = (fh->driver->cls->read)(file, type, H5CX_get_dxpl(), addrs[i], size, bufs[i]);
                }
            H5_AFTER_USER_CB(FAIL)
            if (ret_value < 0)
                HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver read request failed");
        }

        /* Add H5D_SEL_IO_NO_VECTOR_OR_SELECTION_IO_CB to no selection I/O cause */
        H5CX_get_no_selection_io_cause(&no_selection_io_cause);
        no_selection_io_cause |= H5D_SEL_IO_NO_VECTOR_OR_SELECTION_IO_CB;
        H5CX_set_no_selection_io_cause(no_selection_io_cause);

        /* Set actual selection I/O mode, if this is a raw data operation */
        if (is_raw) {
            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_SCALAR_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }

done:
    /* undo the base addr offset to the addrs array if necessary */
    if (addrs_cooked) {
        assert(file->base_addr > 0);
        for (i = 0; i < count; i++)
            addrs[i] -= file->base_addr;
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_read_vector() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_write_vector
 *
 * Purpose:     Private version of H5FDwrite_vector()
 *
 *              Perform count writes to the specified file at the offsets
 *              provided in the addrs array, with the lengths and memory
 *              types provided in the sizes and types arrays.  Data written
 *              is taken from the buffers provided in the bufs array.
 *
 *              If i > 0 and sizes[i] == 0, presume sizes[n] = sizes[i-1]
 *              for all n >= i and < count.
 *
 *              Similarly, if i > 0 and types[i] == H5FD_MEM_NOLIST,
 *              presume types[n] = types[i-1] for all n >= i and < count.
 *
 *              If the underlying VFD supports vector writes, pass the
 *              call through directly.
 *
 *              If it doesn't, convert the vector write into a sequence
 *              of individual writes.
 *
 *              Note that it is not in general possible to convert a
 *              vector write into a selection write, because each element
 *              in the vector write may have a different memory type.
 *              In contrast, selection writes are of a single type.
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully.
 *
 *              Failure:    FAIL
 *                          One or more writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_write_vector(H5FD_int_t *fh, uint32_t count, H5FD_mem_t types[], haddr_t addrs[], size_t sizes[],
                  const void *bufs[])
{
    H5FD_t    *file;
    H5FD_mem_t type         = H5FD_MEM_DEFAULT;
    size_t     size         = 0;
    bool       is_raw       = false; /* Does this include raw data */
    bool       addrs_cooked = false;
    bool       extend_sizes = false;
    bool       extend_types = false;
    uint32_t   i;
    herr_t     ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(types || count == 0);
    assert(addrs || count == 0);
    assert(sizes || count == 0);
    assert(bufs || count == 0);

    /* verify that the first elements of the sizes and types arrays are
     * valid.
     */
    assert(count == 0 || sizes[0] != 0);
    assert(count == 0 || types[0] != H5FD_MEM_NOLIST);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == count)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    if (file->base_addr > 0) {
        /* apply the base_addr offset to the addrs array.  Must undo before
         * we return.
         */
        for (i = 0; i < count; i++)
            addrs[i] += file->base_addr;
        addrs_cooked = true;
    }

    extend_sizes = false;
    extend_types = false;
    for (i = 0; i < count; i++) {
        haddr_t eoa; /* EOA for file */

        if (!extend_sizes) {
            if (sizes[i] == 0) {
                size         = sizes[i - 1];
                extend_sizes = true;
            }
            else
                size = sizes[i];
        }

        if (!extend_types) {
            if (types[i] == H5FD_MEM_NOLIST) {
                type         = types[i - 1];
                extend_types = true;
            }
            else {
                type = types[i];

                /* Check for raw data operation */
                if (type == H5FD_MEM_DRAW)
                    is_raw = true;
            }
        }

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                eoa = (fh->driver->cls->get_eoa)(file, type);
            }
        H5_AFTER_USER_CB(FAIL)
        if (!H5_addr_defined(eoa))
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");

        if ((addrs[i] + size) > eoa)
            HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL,
                        "addr overflow, addrs[%d] = %llu, sizes[%d] = %llu, eoa = %llu", (int)i,
                        (unsigned long long)(addrs[i]), (int)i, (unsigned long long)size,
                        (unsigned long long)eoa);
    }

    /* if the underlying VFD supports vector write, make the call */
    if (file->cls->write_vector) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value =
                    (fh->driver->cls->write_vector)(file, H5CX_get_dxpl(), count, types, addrs, sizes, bufs);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "driver write vector request failed");

        /* Set actual selection I/O mode, if this is a raw data operation */
        if (is_raw) {
            uint32_t actual_selection_io_mode;

            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_VECTOR_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }
    else {
        /* Otherwise, implement the vector write as a sequence of regular
         * write calls.
         */
        uint32_t no_selection_io_cause;
        uint32_t actual_selection_io_mode;

        extend_sizes = false;
        extend_types = false;
        for (i = 0; i < count; i++) {
            /* we have already verified that sizes[0] != 0 and
             * types[0] != H5FD_MEM_NOLIST
             */
            if (!extend_sizes) {
                if (sizes[i] == 0) {
                    size         = sizes[i - 1];
                    extend_sizes = true;
                }
                else
                    size = sizes[i];
            }

            if (!extend_types) {
                if (types[i] == H5FD_MEM_NOLIST) {
                    type         = types[i - 1];
                    extend_types = true;
                }
                else
                    type = types[i];
            }

            /* Prepare & restore library for user callback */
            H5_BEFORE_USER_CB(FAIL)
                {
                    ret_value =
                        (fh->driver->cls->write)(file, type, H5CX_get_dxpl(), addrs[i], size, bufs[i]);
                }
            H5_AFTER_USER_CB(FAIL)
            if (ret_value < 0)
                HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver write request failed");
        }

        /* Add H5D_SEL_IO_NO_VECTOR_OR_SELECTION_IO_CB to no selection I/O cause */
        H5CX_get_no_selection_io_cause(&no_selection_io_cause);
        no_selection_io_cause |= H5D_SEL_IO_NO_VECTOR_OR_SELECTION_IO_CB;
        H5CX_set_no_selection_io_cause(no_selection_io_cause);

        /* Set actual selection I/O mode, if this is a raw data operation */
        if (is_raw) {
            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_SCALAR_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }

done:
    /* undo the base addr offset to the addrs array if necessary */
    if (addrs_cooked) {
        assert(file->base_addr > 0);
        for (i = 0; i < count; i++)
            addrs[i] -= file->base_addr;
    }

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_write_vector() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_read_selection
 *
 * Purpose:     Private version of H5FDread_selection()
 *
 *              Perform count reads from the specified file at the
 *              locations selected in the dataspaces in the file_spaces
 *              array, with each of those dataspaces starting at the file
 *              address specified by the corresponding element of the
 *              offsets array, and with the size of each element in the
 *              dataspace specified by the corresponding element of the
 *              element_sizes array.  The memory type provided by type is
 *              the same for all selections.  Data read is returned in
 *              the locations selected in the dataspaces in the
 *              mem_spaces array, within the buffers provided in the
 *              corresponding elements of the bufs array.
 *
 *              If i > 0 and element_sizes[i] == 0, presume
 *              element_sizes[n] = element_sizes[i-1] for all n >= i and
 *              < count.
 *
 *              If the underlying VFD supports selection reads, pass the
 *              call through directly.
 *
 *              If it doesn't, convert the selection read into a sequence
 *              of vector or scalar reads.
 *
 * Return:      Success:    SUCCEED
 *                          All reads have completed successfully, and
 *                          the results havce been into the supplied
 *                          buffers.
 *
 *              Failure:    FAIL
 *                          The contents of supplied buffers are undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_read_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count, H5S_t **mem_spaces, H5S_t **file_spaces,
                    haddr_t offsets[], size_t element_sizes[], void *bufs[] /* out */)
{
    H5FD_t  *file;
    hid_t    mem_space_ids_local[H5FD_LOCAL_SEL_ARR_LEN];
    hid_t   *mem_space_ids = mem_space_ids_local;
    hid_t    file_space_ids_local[H5FD_LOCAL_SEL_ARR_LEN];
    hid_t   *file_space_ids = file_space_ids_local;
    uint32_t num_spaces     = 0;
    bool     offsets_cooked = false;
    uint32_t i;
    herr_t   ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(mem_spaces || count == 0);
    assert(file_spaces || count == 0);
    assert(offsets || count == 0);
    assert(element_sizes || count == 0);
    assert(bufs || count == 0);

    /* Verify that the first elements of the element_sizes and bufs arrays are
     * valid. */
    assert(count == 0 || element_sizes[0] != 0);
    assert(count == 0 || bufs[0] != NULL);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == count)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    if (file->base_addr > 0) {
        /* apply the base_addr offset to the offsets array.  Must undo before
         * we return.
         */
        for (i = 0; i < count; i++)
            offsets[i] += file->base_addr;
        offsets_cooked = true;
    }

    /* If the file is open for SWMR read access, allow access to data past
     * the end of the allocated space (the 'eoa').  This is done because the
     * eoa stored in the file's superblock might be out of sync with the
     * objects being written within the file by the application performing
     * SWMR write operations.
     */
    /* For now at least, only check that the offset is not past the eoa, since
     * looking into the highest offset in the selection (different from the
     * bounds) is potentially expensive.
     */
    if (!(file->access_flags & H5F_ACC_SWMR_READ)) {
        haddr_t eoa;

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                eoa = (fh->driver->cls->get_eoa)(file, type);
            }
        H5_AFTER_USER_CB(FAIL)
        if (!H5_addr_defined(eoa))
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");

        for (i = 0; i < count; i++)
            if (offsets[i] > eoa)
                HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, offsets[%d] = %llu, eoa = %llu",
                            (int)i, (unsigned long long)(offsets[i]), (unsigned long long)eoa);
    }

    /* If the underlying VFD supports selection read, make the call */
    if (file->cls->read_selection) {
        uint32_t actual_selection_io_mode;

        /* Allocate array of space IDs if necessary, otherwise use local
         * buffers */
        if (count > sizeof(mem_space_ids_local) / sizeof(mem_space_ids_local[0])) {
            if (NULL == (mem_space_ids = H5MM_malloc(count * sizeof(hid_t))))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "memory allocation failed for dataspace list");
            if (NULL == (file_space_ids = H5MM_malloc(count * sizeof(hid_t))))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "memory allocation failed for dataspace list");
        }

        /* Create IDs for all dataspaces */
        for (; num_spaces < count; num_spaces++) {
            if ((mem_space_ids[num_spaces] = H5I_register(H5I_DATASPACE, mem_spaces[num_spaces], true)) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register dataspace ID");

            if ((file_space_ids[num_spaces] = H5I_register(H5I_DATASPACE, file_spaces[num_spaces], true)) <
                0) {
                if (NULL == H5I_remove(mem_space_ids[num_spaces]))
                    HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
                HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register dataspace ID");
            }
        }

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value =
                    (fh->driver->cls->read_selection)(file, type, H5CX_get_dxpl(), count, mem_space_ids,
                                                      file_space_ids, offsets, element_sizes, bufs);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "driver read selection request failed");

        /* Set actual selection I/O, if this is a raw data operation */
        if (type == H5FD_MEM_DRAW) {
            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_SELECTION_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }
    else
        /* Otherwise, implement the selection read as a sequence of regular
         * or vector read calls.
         */
        if (H5FD__read_selection_translate(false, fh, type, count, mem_spaces, file_spaces, offsets,
                                           element_sizes, bufs) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "translation to vector or scalar read failed");

done:
    /* Undo the base addr offset to the offsets array if necessary */
    if (offsets_cooked) {
        assert(file->base_addr > 0);
        for (i = 0; i < count; i++)
            offsets[i] -= file->base_addr;
    }

    /* Cleanup dataspace arrays.  Use H5I_remove() so we only close the IDs and
     * not the underlying dataspaces, which were not created by this function.
     */
    for (i = 0; i < num_spaces; i++) {
        if (NULL == H5I_remove(mem_space_ids[i]))
            HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
        if (NULL == H5I_remove(file_space_ids[i]))
            HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
    }
    if (mem_space_ids != mem_space_ids_local)
        mem_space_ids = H5MM_xfree(mem_space_ids);
    if (file_space_ids != file_space_ids_local)
        file_space_ids = H5MM_xfree(file_space_ids);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_read_selection() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_write_selection
 *
 * Purpose:     Private version of H5FDwrite_selection()
 *
 *              Perform count writes to the specified file at the
 *              locations selected in the dataspaces in the file_spaces
 *              array, with each of those dataspaces starting at the file
 *              address specified by the corresponding element of the
 *              offsets array, and with the size of each element in the
 *              dataspace specified by the corresponding element of the
 *              element_sizes array.  The memory type provided by type is
 *              the same for all selections.  Data write is from
 *              the locations selected in the dataspaces in the
 *              mem_spaces array, within the buffers provided in the
 *              corresponding elements of the bufs array.
 *
 *              If i > 0 and element_sizes[i] == 0, presume
 *              element_sizes[n] = element_sizes[i-1] for all n >= i and
 *              < count.
 *
 *              If the underlying VFD supports selection writes, pass the
 *              call through directly.
 *
 *              If it doesn't, convert the selection write into a sequence
 *              of vector or scalar writes.
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully.
 *
 *              Failure:    FAIL
 *                          One or more writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_write_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count, H5S_t **mem_spaces, H5S_t **file_spaces,
                     haddr_t offsets[], size_t element_sizes[], const void *bufs[])
{
    H5FD_t  *file;
    hid_t    mem_space_ids_local[H5FD_LOCAL_SEL_ARR_LEN];
    hid_t   *mem_space_ids = mem_space_ids_local;
    hid_t    file_space_ids_local[H5FD_LOCAL_SEL_ARR_LEN];
    hid_t   *file_space_ids = file_space_ids_local;
    haddr_t  eoa;
    uint32_t num_spaces     = 0;
    bool     offsets_cooked = false;
    uint32_t i;
    herr_t   ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);
    assert(mem_spaces || count == 0);
    assert(file_spaces || count == 0);
    assert(offsets || count == 0);
    assert(element_sizes || count == 0);
    assert(bufs || count == 0);

    /* Verify that the first elements of the element_sizes and bufs arrays are
     * valid. */
    assert(count == 0 || element_sizes[0] != 0);
    assert(count == 0 || bufs[0] != NULL);

#ifndef H5_HAVE_PARALLEL
    /* The no-op case
     *
     * Do not return early for Parallel mode since the I/O could be a
     * collective transfer.
     */
    if (0 == count)
        HGOTO_DONE(SUCCEED);
#endif /* H5_HAVE_PARALLEL */

    /* Get file pointer */
    file = fh->file;

    if (file->base_addr > 0) {
        /* apply the base_addr offset to the offsets array.  Must undo before
         * we return.
         */
        for (i = 0; i < count; i++)
            offsets[i] += file->base_addr;
        offsets_cooked = true;
    }

    /* For now at least, only check that the offset is not past the eoa, since
     * looking into the highest offset in the selection (different from the
     * bounds) is potentially expensive.
     */
    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            eoa = (fh->driver->cls->get_eoa)(file, type);
        }
    H5_AFTER_USER_CB(FAIL)
    if (!H5_addr_defined(eoa))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver get_eoa request failed");

    for (i = 0; i < count; i++)
        if (offsets[i] > eoa)
            HGOTO_ERROR(H5E_ARGS, H5E_OVERFLOW, FAIL, "addr overflow, offsets[%d] = %llu, eoa = %llu", (int)i,
                        (unsigned long long)(offsets[i]), (unsigned long long)eoa);

    /* If the underlying VFD supports selection write, make the call */
    if (file->cls->write_selection) {
        uint32_t actual_selection_io_mode;

        /* Allocate array of space IDs if necessary, otherwise use local
         * buffers */
        if (count > sizeof(mem_space_ids_local) / sizeof(mem_space_ids_local[0])) {
            if (NULL == (mem_space_ids = H5MM_malloc(count * sizeof(hid_t))))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "memory allocation failed for dataspace list");
            if (NULL == (file_space_ids = H5MM_malloc(count * sizeof(hid_t))))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL, "memory allocation failed for dataspace list");
        }

        /* Create IDs for all dataspaces */
        for (; num_spaces < count; num_spaces++) {
            if ((mem_space_ids[num_spaces] = H5I_register(H5I_DATASPACE, mem_spaces[num_spaces], true)) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register dataspace ID");

            if ((file_space_ids[num_spaces] = H5I_register(H5I_DATASPACE, file_spaces[num_spaces], true)) <
                0) {
                if (NULL == H5I_remove(mem_space_ids[num_spaces]))
                    HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
                HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "unable to register dataspace ID");
            }
        }

        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value =
                    (fh->driver->cls->write_selection)(file, type, H5CX_get_dxpl(), count, mem_space_ids,
                                                       file_space_ids, offsets, element_sizes, bufs);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "driver write selection request failed");

        /* Set actual selection I/O, if this is a raw data operation */
        if (type == H5FD_MEM_DRAW) {
            H5CX_get_actual_selection_io_mode(&actual_selection_io_mode);
            actual_selection_io_mode |= H5D_SELECTION_IO;
            H5CX_set_actual_selection_io_mode(actual_selection_io_mode);
        }
    }
    else
        /* Otherwise, implement the selection write as a sequence of regular
         * or vector write calls.
         */

        if (H5FD__write_selection_translate(false, fh, type, count, mem_spaces, file_spaces, offsets,
                                            element_sizes, bufs) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "translation to vector or scalar write failed");

done:
    /* undo the base addr offset to the offsets array if necessary */
    if (offsets_cooked) {
        assert(file->base_addr > 0);
        for (i = 0; i < count; i++)
            offsets[i] -= file->base_addr;
    }

    /* Cleanup dataspace arrays.  Use H5I_remove() so we only close the IDs and
     * not the underlying dataspaces, which were not created by this function.
     */
    for (i = 0; i < num_spaces; i++) {
        if (NULL == H5I_remove(mem_space_ids[i]))
            HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
        if (NULL == H5I_remove(file_space_ids[i]))
            HDONE_ERROR(H5E_VFL, H5E_CANTREMOVE, FAIL, "problem removing id");
    }
    if (mem_space_ids != mem_space_ids_local)
        mem_space_ids = H5MM_xfree(mem_space_ids);
    if (file_space_ids != file_space_ids_local)
        file_space_ids = H5MM_xfree(file_space_ids);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_write_selection() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_flush
 *
 * Purpose:     Private version of H5FDflush()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_flush(H5FD_int_t *fh, bool closing)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->flush) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->flush)(fh->file, H5CX_get_dxpl(), closing);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "driver flush request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_flush() */

/*-------------------------------------------------------------------------
 * Function:	H5FD_truncate
 *
 * Purpose:     Private version of H5FDtruncate()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_truncate(H5FD_int_t *fh, bool closing)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->truncate) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->truncate)(fh->file, H5CX_get_dxpl(), closing);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTUPDATE, FAIL, "driver truncate request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_truncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_lock
 *
 * Purpose:     Private version of H5FDlock()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_lock(H5FD_int_t *fh, bool rw)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->lock) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->lock)(fh->file, rw);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTLOCKFILE, FAIL, "driver lock request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_lock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_unlock
 *
 * Purpose:     Private version of H5FDunlock()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_unlock(H5FD_int_t *fh)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver */
    if (fh->driver->cls->unlock) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->unlock)(fh->file);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTUNLOCKFILE, FAIL, "driver unlock request failed");
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_unlock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_delete_wrap
 *
 * Purpose:     Wrapper around H5FD_delete that saves and restores the current
 *              API context state.  Must be used by a routine that passes a
 *              different FAPL to H5FD_delete than the routine was called with.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_delete_wrap(const char *filename, H5P_genplist_t *fapl)
{
    hid_t  old_fapl_id = H5I_INVALID_HID; /* ID for old FAPL in API context */
    herr_t ret_value = SUCCEED;           /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Retrieve the current FAPL in the API context */
    if ((old_fapl_id = H5CX_get_fapl()) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file access property list");

    /* Verify access property list and set up collective metadata if appropriate */
    if (H5CX_set_apl(H5P_PLIST_ID(fapl), H5I_INVALID_HID, false) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTSET, FAIL, "can't set access property list info");

    /* Call actual H5FD_delete routine */
    if (H5FD_delete(filename, fapl) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTDELETEFILE, FAIL, "can't delete file");

done:
    /* Restore previous FAPL in the API context */
    if (old_fapl_id > 0)
        H5CX_set_fapl(old_fapl_id);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_delete_wrap() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_delete
 *
 * Purpose:     Private version of H5FDdelete()
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_delete(const char *filename, H5P_genplist_t *fapl)
{
    H5FD_driver_t *driver;              /* VFD for file */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */

    assert(filename);

    /* Get the VFD to delete the file with */
    if (NULL == (driver = H5CX_peek_driver()))
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to retrieve VFL driver");
    if (NULL == driver->cls->del)
        HGOTO_ERROR(H5E_VFL, H5E_UNSUPPORTED, FAIL, "file driver has no 'del' method");

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to file driver */
            ret_value = (driver->cls->del)(filename, H5P_PLIST_ID(fapl));
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDELETEFILE, FAIL, "delete failed");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_delete() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_ctl
 *
 * Purpose:     Private version of H5FDctl()
 *
 *              The desired operation is specified by the op_code
 *              parameter.
 *
 *              The flags parameter controls management of op_codes that
 *              are unknown to the callback
 *
 *              The input and output parameters allow op_code specific
 *              input and output
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_ctl(H5FD_int_t *fh, uint64_t op_code, uint64_t flags, const void *input, void **output)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    assert(fh->file);

    /* Dispatch to driver if the ctl function exists.
     *
     * If it doesn't, fail if the H5FD_CTL_FAIL_IF_UNKNOWN_FLAG is set.
     *
     * Otherwise, report success.
     */
    if (fh->driver->cls->ctl) {
        /* Prepare & restore library for user callback */
        H5_BEFORE_USER_CB(FAIL)
            {
                ret_value = (fh->driver->cls->ctl)(fh->file, op_code, flags, input, output);
            }
        H5_AFTER_USER_CB(FAIL)
        if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_FCNTL, FAIL, "VFD ctl request failed");
    }
    else if (flags & H5FD_CTL_FAIL_IF_UNKNOWN_FLAG)
        HGOTO_ERROR(H5E_VFL, H5E_FCNTL, FAIL,
                    "VFD ctl request failed (no ctl callback and fail if unknown flag is set)");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_ctl() */
