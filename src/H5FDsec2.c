/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 * Purpose: The POSIX unbuffered file I/O driver.
 *
 */

#include "H5FDdrvr_module.h" /* This source code file is part of the H5FD driver module */

#include "H5private.h"   /* Generic Functions        */
#include "H5Eprivate.h"  /* Error handling           */
#include "H5Fprivate.h"  /* File access              */
#include "H5FDprivate.h" /* File drivers             */
#include "H5FDposix_common.h" /* Common POSIX file drivers */
#include "H5FDsec2.h"    /* Sec2 file driver         */
#include "H5FLprivate.h" /* Free Lists               */
#include "H5Iprivate.h"  /* IDs                      */
#include "H5MMprivate.h" /* Memory management        */
#include "H5Pprivate.h"  /* Property lists           */

/* The driver identification number, initialized at runtime */
static hid_t H5FD_SEC2_g = 0;

/* The description of a file belonging to this driver.
 * (The actual information for this file is in the 'posix_common'
 *      struct, which handles all the common info for POSIX-based files)
 */
typedef struct H5FD_sec2_t {
    H5FD_t         pub; /* public stuff, must be first      */
    H5FD_posix_common_t pos_com;    /* Common POSIX info        */

    /* Information from properties set by 'h5repart' tool
     *
     * Whether to eliminate the family driver info and convert this file to
     * a single file.
     */
    hbool_t fam_to_single;
} H5FD_sec2_t;

/* Prototypes */
static herr_t  H5FD__sec2_term(void);
static H5FD_t *H5FD__sec2_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr);
static herr_t  H5FD__sec2_close(H5FD_t *_file);
static int     H5FD__sec2_cmp(const H5FD_t *_f1, const H5FD_t *_f2);
static herr_t  H5FD__sec2_query(const H5FD_t *_f1, unsigned long *flags);
static haddr_t H5FD__sec2_get_eoa(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__sec2_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr);
static haddr_t H5FD__sec2_get_eof(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__sec2_get_handle(H5FD_t *_file, hid_t fapl, void **file_handle);
static herr_t  H5FD__sec2_read(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                               void *buf);
static herr_t  H5FD__sec2_write(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                                const void *buf);
static herr_t  H5FD__sec2_truncate(H5FD_t *_file, hid_t dxpl_id, hbool_t closing);
static herr_t  H5FD__sec2_lock(H5FD_t *_file, hbool_t rw);
static herr_t  H5FD__sec2_unlock(H5FD_t *_file);

static const H5FD_class_t H5FD_sec2_g = {
    "sec2",                /* name                 */
    H5_POSIX_MAXADDR,      /* maxaddr              */
    H5F_CLOSE_WEAK,        /* fc_degree            */
    H5FD__sec2_term,       /* terminate            */
    NULL,                  /* sb_size              */
    NULL,                  /* sb_encode            */
    NULL,                  /* sb_decode            */
    0,                     /* fapl_size            */
    NULL,                  /* fapl_get             */
    NULL,                  /* fapl_copy            */
    NULL,                  /* fapl_free            */
    0,                     /* dxpl_size            */
    NULL,                  /* dxpl_copy            */
    NULL,                  /* dxpl_free            */
    H5FD__sec2_open,       /* open                 */
    H5FD__sec2_close,      /* close                */
    H5FD__sec2_cmp,        /* cmp                  */
    H5FD__sec2_query,      /* query                */
    NULL,                  /* get_type_map         */
    NULL,                  /* alloc                */
    NULL,                  /* free                 */
    H5FD__sec2_get_eoa,    /* get_eoa              */
    H5FD__sec2_set_eoa,    /* set_eoa              */
    H5FD__sec2_get_eof,    /* get_eof              */
    H5FD__sec2_get_handle, /* get_handle           */
    H5FD__sec2_read,       /* read                 */
    H5FD__sec2_write,      /* write                */
    NULL,                  /* flush                */
    H5FD__sec2_truncate,   /* truncate             */
    H5FD__sec2_lock,       /* lock                 */
    H5FD__sec2_unlock,     /* unlock               */
    H5FD_FLMAP_DICHOTOMY   /* fl_map               */
};

/* Declare a free list to manage the H5FD_sec2_t struct */
H5FL_DEFINE_STATIC(H5FD_sec2_t);

/*-------------------------------------------------------------------------
 * Function:    H5FD__init_package
 *
 * Purpose:     Initializes any interface-specific data or routines.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__init_package(void)
{
    herr_t ret_value    = SUCCEED;

    FUNC_ENTER_STATIC

    if (H5FD_sec2_init() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to initialize sec2 VFD")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5FD__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_sec2_init
 *
 * Purpose:     Initialize this driver by registering the driver with the
 *              library.
 *
 * Return:      Success:    The driver ID for the sec2 driver
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5FD_sec2_init(void)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    if (H5I_VFL != H5I_get_type(H5FD_SEC2_g))
        H5FD_SEC2_g = H5FD_register(&H5FD_sec2_g, sizeof(H5FD_class_t), FALSE);

    /* Set return value */
    ret_value = H5FD_SEC2_g;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_sec2_init() */

/*---------------------------------------------------------------------------
 * Function:    H5FD__sec2_term
 *
 * Purpose:     Shut down the VFD
 *
 * Returns:     SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Friday, Jan 30, 2004
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_term(void)
{
    FUNC_ENTER_STATIC_NOERR

    /* Reset VFL ID */
    H5FD_SEC2_g = 0;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__sec2_term() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_sec2
 *
 * Purpose:     Modify the file access property list to use the H5FD_SEC2
 *              driver defined in this source file.  There are no driver
 *              specific properties.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, February 19, 1998
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_sec2(hid_t fapl_id)
{
    H5P_genplist_t *plist; /* Property list pointer */
    herr_t          ret_value;

    FUNC_ENTER_API(FAIL)
    H5TRACE1("e", "i", fapl_id);

    if (NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")

    ret_value = H5P_set_driver(plist, H5FD_SEC2, NULL);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_sec2() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_open
 *
 * Purpose:     Create and/or opens a file as an HDF5 file.
 *
 * Return:      Success:    A pointer to a new file data structure. The
 *                          public fields will be initialized by the
 *                          caller, which is always H5FD_open().
 *              Failure:    NULL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static H5FD_t *
H5FD__sec2_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    H5FD_sec2_t *file        = NULL;  /* sec2 VFD info */
    hbool_t      file_opened = FALSE; /* Whether the file was opened */
    H5FD_t *     ret_value   = NULL;  /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check on file offsets */
    HDcompile_assert(sizeof(HDoff_t) >= sizeof(size_t));

    /* Create the new file struct */
    if (NULL == (file = H5FL_CALLOC(H5FD_sec2_t)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "unable to allocate file struct")

    /* Open the file */
    if (H5FD__posix_common_open(name, flags, maxaddr, fapl_id, &file->pos_com, NULL, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTOPENFILE, NULL, "can't open file")
    file_opened = TRUE;

    /* Check for non-default FAPL */
    if (H5P_FILE_ACCESS_DEFAULT != fapl_id) {
        H5P_genplist_t  *plist;             /* Property list pointer */

        /* Get the FAPL */
        if (NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
            HGOTO_ERROR(H5E_VFL, H5E_BADTYPE, NULL, "not a file access property list")

        /* This step is for h5repart tool only. If user wants to change file driver from
         * family to one that uses single files (sec2, etc.) while using h5repart, this
         * private property should be set so that in the later step, the library can ignore
         * the family driver information saved in the superblock.
         */
        if (H5P_exist_plist(plist, H5F_ACS_FAMILY_TO_SINGLE_NAME) > 0)
            if (H5P_get(plist, H5F_ACS_FAMILY_TO_SINGLE_NAME, &file->fam_to_single) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, NULL, "can't get property of changing family to single")
    } /* end if */

    /* Set return value */
    ret_value = (H5FD_t *)file;

done:
    if (NULL == ret_value)
        if (file) {
            if (file_opened)
                H5FD__posix_common_close(&file->pos_com, NULL);
            file = H5FL_FREE(H5FD_sec2_t, file);
        } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_open() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_close
 *
 * Purpose:     Closes an HDF5 file.
 *
 * Return:      Success:    SUCCEED
 *              Failure:    FAIL, file not closed.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_close(H5FD_t *_file)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Close the underlying file */
    if (H5FD__posix_common_close(&file->pos_com, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEFILE, FAIL, "unable to close file")

    /* Release the file info */
    file = H5FL_FREE(H5FD_sec2_t, file);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_close() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_cmp
 *
 * Purpose:     Compares two files belonging to this driver using an
 *              arbitrary (but consistent) ordering.
 *
 * Return:      Success:    A value like strcmp()
 *              Failure:    never fails (arguments were checked by the
 *                          caller).
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static int
H5FD__sec2_cmp(const H5FD_t *_f1, const H5FD_t *_f2)
{
    const H5FD_sec2_t *f1        = (const H5FD_sec2_t *)_f1;
    const H5FD_sec2_t *f2        = (const H5FD_sec2_t *)_f2;

    FUNC_ENTER_STATIC_NOERR

    /* Sanity checks */
    HDassert(f1);
    HDassert(f2);

    FUNC_LEAVE_NOAPI(H5FD__posix_common_cmp(&f1->pos_com, &f2->pos_com))
} /* end H5FD__sec2_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_query
 *
 * Purpose:     Set the flags that this VFL driver is capable of supporting.
 *              (listed in H5FDpublic.h)
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Friday, August 25, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_query(const H5FD_t *_file, unsigned long *flags)
{
    const H5FD_sec2_t *file = (const H5FD_sec2_t *)_file; /* sec2 VFD info */

    FUNC_ENTER_STATIC_NOERR

    /* Sanity check */
    HDassert(file);

    /* clang-format off */
    /* Set the VFL feature flags that this driver supports */
    /* Notice: the Mirror VFD Writer currently uses only the Sec2 driver as
     * the underlying driver -- as such, the Mirror VFD implementation copies
     * these feature flags as its own. Any modifications made here must be
     * reflected in H5FDmirror.c
     * -- JOS 2020-01-13
     */
    if (flags) {
        *flags = 0;
        *flags |= H5FD_FEAT_AGGREGATE_METADATA;  /* OK to aggregate metadata allocations  */
        *flags |= H5FD_FEAT_ACCUMULATE_METADATA; /* OK to accumulate metadata for faster writes */
        *flags |= H5FD_FEAT_DATA_SIEVE;          /* OK to perform data sieving for faster raw data reads & writes */
        *flags |= H5FD_FEAT_AGGREGATE_SMALLDATA; /* OK to aggregate "small" raw data allocations */
        *flags |= H5FD_FEAT_POSIX_COMPAT_HANDLE; /* get_handle callback returns a POSIX file descriptor */
        *flags |= H5FD_FEAT_SUPPORTS_SWMR_IO;    /* VFD supports the single-writer/multiple-readers (SWMR) pattern */
        *flags |= H5FD_FEAT_DEFAULT_VFD_COMPATIBLE; /* VFD creates a file which can be opened with the default VFD */

        /* Check for flags that are set by h5repart */
        if (file->fam_to_single)
            *flags |= H5FD_FEAT_IGNORE_DRVRINFO; /* Ignore the driver info when file is opened (which eliminates it) */
    } /* end if */
    /* clang-format on */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__sec2_query() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_get_eoa
 *
 * Purpose:     Gets the end-of-address marker for the file. The EOA marker
 *              is the first address past the last byte allocated in the
 *              format address space.
 *
 * Return:      The end-of-address marker.
 *
 * Programmer:  Robb Matzke
 *              Monday, August  2, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__sec2_get_eoa(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_sec2_t *file = (const H5FD_sec2_t *)_file;
    haddr_t eoa = HADDR_UNDEF;          /* EOA for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Get the file's EOA */
    if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOA for file")

    /* Set the return value */
    ret_value = eoa;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_get_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_set_eoa
 *
 * Purpose:     Set the end-of-address marker for the file. This function is
 *              called shortly after an existing HDF5 file is opened in order
 *              to tell the driver where the end of the HDF5 data is located.
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_set_eoa(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, haddr_t addr)
{
    H5FD_sec2_t *file = (H5FD_sec2_t *)_file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Set the file's EOA */
    if (H5FD__posix_common_set_eoa(&file->pos_com, addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set EOA for file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_set_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_get_eof
 *
 * Purpose:     Returns the end-of-file marker, which is the greater of
 *              either the filesystem end-of-file or the HDF5 end-of-address
 *              markers.
 *
 * Return:      End of file address, the first address past the end of the
 *              "file", either the filesystem file or the HDF5 file.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__sec2_get_eof(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_sec2_t *file = (const H5FD_sec2_t *)_file;
    haddr_t eof = HADDR_UNDEF;          /* EOF for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Get the file's EOF */
    if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOF for file")

    /* Set the return value */
    ret_value = eof;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_get_eof() */

/*-------------------------------------------------------------------------
 * Function:       H5FD__sec2_get_handle
 *
 * Purpose:        Returns the file handle of sec2 file driver.
 *
 * Returns:        SUCCEED/FAIL
 *
 * Programmer:     Raymond Lu
 *                 Sept. 16, 2002
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_get_handle(H5FD_t *_file, hid_t H5_ATTR_UNUSED fapl, void **file_handle)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file;
    herr_t       ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Check args */
    if (!file_handle)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file handle not valid")

    /* Get the file's handle */
    if (H5FD__posix_common_get_handle(&file->pos_com, file_handle) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get handle for file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_get_handle() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_read
 *
 * Purpose:     Reads SIZE bytes of data from FILE beginning at address ADDR
 *              into buffer BUF according to data transfer properties in
 *              DXPL_ID.
 *
 * Return:      Success:    SUCCEED. Result is stored in caller-supplied
 *                          buffer BUF.
 *              Failure:    FAIL, Contents of buffer BUF are undefined.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_read(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr,
                size_t size, void *buf /*out*/)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(buf);

    /* Perform the read */
    if (H5FD__posix_common_read(&file->pos_com, addr, size, buf, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "can't read from file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_read() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_write
 *
 * Purpose:     Writes SIZE bytes of data to FILE beginning at address ADDR
 *              from buffer BUF according to data transfer properties in
 *              DXPL_ID.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_write(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr,
                 size_t size, const void *buf)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(buf);

    /* Perform the write */
    if (H5FD__posix_common_write(&file->pos_com, addr, size, buf, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "can't write to file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_write() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_truncate
 *
 * Purpose:     Makes sure that the true file size is the same (or larger)
 *              than the end-of-address.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Wednesday, August  4, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_truncate(H5FD_t *_file, hid_t H5_ATTR_UNUSED dxpl_id, hbool_t H5_ATTR_UNUSED closing)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file; /* VFD file struct */
    herr_t       ret_value = SUCCEED;              /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Truncate the file */
    if (H5FD__posix_common_truncate(&file->pos_com, HADDR_UNDEF, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTTRUNCATE, FAIL, "can't truncate file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_truncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_lock
 *
 * Purpose:     To place an advisory lock on a file.
 *		The lock type to apply depends on the parameter "rw":
 *			TRUE--opens for write: an exclusive lock
 *			FALSE--opens for read: a shared lock
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_lock(H5FD_t *_file, hbool_t rw)
{
    H5FD_sec2_t *file = (H5FD_sec2_t *)_file; /* VFD file struct          */
    herr_t       ret_value = SUCCEED;         /* Return value             */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Lock the file */
    if (H5FD__posix_common_lock(&file->pos_com, rw, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTLOCK, FAIL, "can't lock file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_lock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__sec2_unlock
 *
 * Purpose:     To remove the existing lock on the file
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__sec2_unlock(H5FD_t *_file)
{
    H5FD_sec2_t *file      = (H5FD_sec2_t *)_file; /* VFD file struct          */
    herr_t       ret_value = SUCCEED;              /* Return value             */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Unlock the file */
    if (H5FD__posix_common_unlock(&file->pos_com, NULL) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTUNLOCK, FAIL, "can't unlock file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__sec2_unlock() */
