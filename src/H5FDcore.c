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
 *              Tuesday, August 10, 1999
 *
 * Purpose:     A driver which stores the HDF5 data in main memory  using
 *              only the HDF5 public API. This driver is useful for fast
 *              access to small, temporary hdf5 files.
 */

#include "H5FDdrvr_module.h" /* This source code file is part of the H5FD driver module */

#include "H5private.h"   /* Generic Functions            */
#include "H5Eprivate.h"  /* Error handling               */
#include "H5Fprivate.h"  /* File access                  */
#include "H5FDprivate.h" /* File drivers                 */
#include "H5FDcore.h"    /* Core file driver             */
#include "H5FDposix_common.h" /* Common POSIX file drivers  */
#include "H5FLprivate.h" /* Free lists                   */
#include "H5Iprivate.h"  /* IDs                          */
#include "H5MMprivate.h" /* Memory management            */
#include "H5Pprivate.h"  /* Property lists               */
#include "H5SLprivate.h" /* Skip lists                   */

/* The driver identification number, initialized at runtime */
static hid_t H5FD_CORE_g = 0;

/* The skip list node type.  Represents a region in the file. */
typedef struct H5FD_core_region_t {
    haddr_t start; /* Start address of the region          */
    haddr_t end;   /* End address of the region            */
} H5FD_core_region_t;

/* The description of a file belonging to this driver */
typedef struct H5FD_core_t {
    H5FD_t         pub;              /* public stuff, must be first          */
    H5FD_posix_common_t pos_com;     /* Common POSIX info                    */

    char *         name;             /* for equivalence testing              */
    unsigned char *mem;              /* the underlying memory                */
    size_t         increment;        /* multiples for mem allocation         */
    hbool_t        backing_store;    /* write to file name on flush          */
    hbool_t        file_opened;      /* whether a backing file is open       */
    hbool_t        write_tracking;   /* Whether to track writes              */
    size_t         bstore_page_size; /* backing store page size              */

    hbool_t                     dirty;        /* changes not saved?       */
    H5SL_t *                    dirty_list;   /* dirty parts of the file  */
    H5FD_file_image_callbacks_t fi_callbacks; /* file image callbacks     */
} H5FD_core_t;

/* Driver-specific file access properties */
typedef struct H5FD_core_fapl_t {
    size_t  increment;      /* how much to grow memory */
    hbool_t backing_store;  /* write to file name on flush */
    hbool_t write_tracking; /* Whether to track writes */
    size_t  page_size;      /* Page size for tracked writes */
} H5FD_core_fapl_t;

/* Allocate memory in multiples of this size by default */
#define H5FD_CORE_INCREMENT                8192
#define H5FD_CORE_WRITE_TRACKING_FLAG      FALSE
#define H5FD_CORE_WRITE_TRACKING_PAGE_SIZE 524288

/* Prototypes */
static herr_t  H5FD__core_add_dirty_region(H5FD_core_t *file, haddr_t start, haddr_t end);
static herr_t  H5FD__core_destroy_dirty_list(H5FD_core_t *file);
static herr_t  H5FD__core_term(void);
static void *  H5FD__core_fapl_get(H5FD_t *_file);
static H5FD_t *H5FD__core_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr);
static herr_t  H5FD__core_close(H5FD_t *_file);
static int     H5FD__core_cmp(const H5FD_t *_f1, const H5FD_t *_f2);
static herr_t  H5FD__core_query(const H5FD_t *_f1, unsigned long *flags);
static haddr_t H5FD__core_get_eoa(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__core_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr);
static haddr_t H5FD__core_get_eof(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__core_get_handle(H5FD_t *_file, hid_t fapl, void **file_handle);
static herr_t  H5FD__core_read(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                               void *buf);
static herr_t  H5FD__core_write(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                                const void *buf);
static herr_t  H5FD__core_flush(H5FD_t *_file, hid_t dxpl_id, hbool_t closing);
static herr_t  H5FD__core_truncate(H5FD_t *_file, hid_t dxpl_id, hbool_t closing);
static herr_t  H5FD__core_lock(H5FD_t *_file, hbool_t rw);
static herr_t  H5FD__core_unlock(H5FD_t *_file);

static const H5FD_class_t H5FD_core_g = {
    "core",                   /* name                 */
    H5_POSIX_MAXADDR,         /* maxaddr              */
    H5F_CLOSE_WEAK,           /* fc_degree            */
    H5FD__core_term,          /* terminate            */
    NULL,                     /* sb_size              */
    NULL,                     /* sb_encode            */
    NULL,                     /* sb_decode            */
    sizeof(H5FD_core_fapl_t), /* fapl_size            */
    H5FD__core_fapl_get,      /* fapl_get             */
    NULL,                     /* fapl_copy            */
    NULL,                     /* fapl_free            */
    0,                        /* dxpl_size            */
    NULL,                     /* dxpl_copy            */
    NULL,                     /* dxpl_free            */
    H5FD__core_open,          /* open                 */
    H5FD__core_close,         /* close                */
    H5FD__core_cmp,           /* cmp                  */
    H5FD__core_query,         /* query                */
    NULL,                     /* get_type_map         */
    NULL,                     /* alloc                */
    NULL,                     /* free                 */
    H5FD__core_get_eoa,       /* get_eoa              */
    H5FD__core_set_eoa,       /* set_eoa              */
    H5FD__core_get_eof,       /* get_eof              */
    H5FD__core_get_handle,    /* get_handle           */
    H5FD__core_read,          /* read                 */
    H5FD__core_write,         /* write                */
    H5FD__core_flush,         /* flush                */
    H5FD__core_truncate,      /* truncate             */
    H5FD__core_lock,          /* lock                 */
    H5FD__core_unlock,        /* unlock               */
    H5FD_FLMAP_DICHOTOMY      /* fl_map               */
};

/* Define a free list to manage the region type */
H5FL_DEFINE(H5FD_core_region_t);

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_add_dirty_region
 *
 * Purpose:     Add a new dirty region to the list for later flushing
 *              to the backing store.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_add_dirty_region(H5FD_core_t *file, haddr_t start, haddr_t end)
{
    H5FD_core_region_t *b_item          = NULL;
    H5FD_core_region_t *a_item          = NULL;
    H5FD_core_region_t *item            = NULL;
    haddr_t             b_addr          = 0;
    haddr_t             a_addr          = 0;
    hbool_t             create_new_node = TRUE;
    herr_t              ret_value       = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(file);
    HDassert(file->dirty_list);
    HDassert(start <= end);

    /* Adjust the dirty region to the nearest block boundaries */
    if (start % file->bstore_page_size != 0)
        start = (start / file->bstore_page_size) * file->bstore_page_size;

    if (end % file->bstore_page_size != (file->bstore_page_size - 1)) {
        haddr_t eof = HADDR_UNDEF;          /* EOF for the file */

        /* Get the file's EOF */
        if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOF for file")

        end = (((end / file->bstore_page_size) + 1) * file->bstore_page_size) - 1;
        if (end > eof)
            end = eof - 1;
    } /* end if */

    /* Get the regions before and after the intended insertion point */
    b_addr = start + 1;
    a_addr = end + 2;
    b_item = (H5FD_core_region_t *)H5SL_less(file->dirty_list, &b_addr);
    a_item = (H5FD_core_region_t *)H5SL_less(file->dirty_list, &a_addr);

    /* Check to see if we need to extend the upper end of the NEW region */
    if (a_item)
        if (start < a_item->start && end < a_item->end) {
            /* Extend the end of the NEW region to match the existing AFTER region */
            end = a_item->end;
        } /* end if */
    /* Attempt to extend the PREV region */
    if (b_item)
        if (start <= b_item->end + 1) {

            /* Need to set this for the delete algorithm */
            start = b_item->start;

            /* We won't need to insert a new node since we can
             * just update an existing one instead.
             */
            create_new_node = FALSE;
        } /* end if */

    /* Remove any old nodes that are no longer needed */
    while (a_item && a_item->start > start) {

        H5FD_core_region_t *less;
        haddr_t             key = a_item->start - 1;

        /* Save the previous node before we trash this one */
        less = (H5FD_core_region_t *)H5SL_less(file->dirty_list, &key);

        /* Delete this node */
        a_item = (H5FD_core_region_t *)H5SL_remove(file->dirty_list, &a_item->start);
        a_item = H5FL_FREE(H5FD_core_region_t, a_item);

        /* Set up to check the next node */
        if (less)
            a_item = less;
    } /* end while */

    /* Insert the new node */
    if (create_new_node) {
        if (NULL == (item = (H5FD_core_region_t *)H5SL_search(file->dirty_list, &start))) {
            /* Ok to insert.  No pre-existing node with that key. */
            item        = (H5FD_core_region_t *)H5FL_CALLOC(H5FD_core_region_t);
            item->start = start;
            item->end   = end;
            if (H5SL_insert(file->dirty_list, item, &item->start) < 0)
                HGOTO_ERROR(H5E_SLIST, H5E_CANTINSERT, FAIL, "can't insert new dirty region: (%llu, %llu)\n",
                            (unsigned long long)start, (unsigned long long)end)
        } /* end if */
        else {
            /* Store the new item endpoint if it's bigger */
            item->end = (item->end < end) ? end : item->end;
        }
    }
    else {
        /* Update the size of the before region */
        if (b_item->end < end)
            b_item->end = end;
    }

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_add_dirty_region() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_destroy_dirty_list
 *
 * Purpose:     Completely destroy the dirty list.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_destroy_dirty_list(H5FD_core_t *file)
{
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    HDassert(file);

    /* Destroy the list, including any remaining list elements */
    if (file->dirty_list) {
        H5FD_core_region_t *region = NULL;

        while (NULL != (region = (H5FD_core_region_t *)H5SL_remove_first(file->dirty_list)))
            region = H5FL_FREE(H5FD_core_region_t, region);

        if (H5SL_close(file->dirty_list) < 0)
            HGOTO_ERROR(H5E_SLIST, H5E_CLOSEERROR, FAIL, "can't close core vfd dirty list")
        file->dirty_list = NULL;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_destroy_dirty_list() */

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

    if (H5FD_core_init() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to initialize core VFD")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5FD__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_core_init
 *
 * Purpose:     Initialize this driver by registering the driver with the
 *              library.
 *
 * Return:      Success:    The driver ID for the core driver
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5FD_core_init(void)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    if (H5I_VFL != H5I_get_type(H5FD_CORE_g))
        H5FD_CORE_g = H5FD_register(&H5FD_core_g, sizeof(H5FD_class_t), FALSE);

    /* Set return value */
    ret_value = H5FD_CORE_g;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_core_init() */

/*---------------------------------------------------------------------------
 * Function:    H5FD__core_term
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
H5FD__core_term(void)
{
    FUNC_ENTER_STATIC_NOERR

    /* Reset VFL ID */
    H5FD_CORE_g = 0;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__core_term() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_core_write_tracking
 *
 * Purpose:    Enables/disables core VFD write tracking and page
 *              aggregation size.
 *
 * Return:    Non-negative on success/Negative on failure
 *
 * Programmer:  Dana Robinson
 *              Tuesday, April 8, 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_core_write_tracking(hid_t plist_id, hbool_t is_enabled, size_t page_size)
{
    H5P_genplist_t *        plist;               /* Property list pointer */
    H5FD_core_fapl_t        fa;                  /* Core VFD info */
    const H5FD_core_fapl_t *old_fa;              /* Old core VFD info */
    herr_t                  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "ibz", plist_id, is_enabled, page_size);

    /* The page size cannot be zero */
    if (page_size == 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "page_size cannot be zero")

    /* Get the plist structure */
    if (NULL == (plist = H5P_object_verify(plist_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADID, FAIL, "can't find object for ID")
    if (H5FD_CORE != H5P_peek_driver(plist))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "incorrect VFL driver")
    if (NULL == (old_fa = (const H5FD_core_fapl_t *)H5P_peek_driver_info(plist)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "bad VFL driver info")

    /* Set VFD info values */
    HDmemset(&fa, 0, sizeof(H5FD_core_fapl_t));
    fa.increment      = old_fa->increment;
    fa.backing_store  = old_fa->backing_store;
    fa.write_tracking = is_enabled;
    fa.page_size      = page_size;

    /* Set the property values & the driver for the FAPL */
    if (H5P_set_driver(plist, H5FD_CORE, &fa) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set core VFD as driver")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_core_write_tracking() */

/*-------------------------------------------------------------------------
 * Function:    H5Pget_core_write_tracking
 *
 * Purpose:    Gets information about core VFD write tracking and page
 *              aggregation size.
 *
 * Return:    Non-negative on success/Negative on failure
 *
 * Programmer:  Dana Robinson
 *              Tuesday, April 8, 2014
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_core_write_tracking(hid_t plist_id, hbool_t *is_enabled /*out*/, size_t *page_size /*out*/)
{
    H5P_genplist_t *        plist;               /* Property list pointer */
    const H5FD_core_fapl_t *fa;                  /* Core VFD info */
    herr_t                  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "ixx", plist_id, is_enabled, page_size);

    /* Get the plist structure */
    if (NULL == (plist = H5P_object_verify(plist_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADID, FAIL, "can't find object for ID")
    if (H5FD_CORE != H5P_peek_driver(plist))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "incorrect VFL driver")
    if (NULL == (fa = (const H5FD_core_fapl_t *)H5P_peek_driver_info(plist)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "bad VFL driver info")

    /* Get values */
    if (is_enabled)
        *is_enabled = fa->write_tracking;
    if (page_size)
        *page_size = fa->page_size;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pget_core_write_tracking() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_core
 *
 * Purpose:     Modify the file access property list to use the H5FD_CORE
 *              driver defined in this source file.  The INCREMENT specifies
 *              how much to grow the memory each time we need more.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, February 19, 1998
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_core(hid_t fapl_id, size_t increment, hbool_t backing_store)
{
    H5P_genplist_t * plist;               /* Property list pointer */
    H5FD_core_fapl_t fa;                  /* Core VFD info */
    herr_t           ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "izb", fapl_id, increment, backing_store);

    /* Check argument */
    if (NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")

    /* Set VFD info values */
    HDmemset(&fa, 0, sizeof(H5FD_core_fapl_t));
    fa.increment      = increment;
    fa.backing_store  = backing_store;
    fa.write_tracking = H5FD_CORE_WRITE_TRACKING_FLAG;
    fa.page_size      = H5FD_CORE_WRITE_TRACKING_PAGE_SIZE;

    /* Set the property values & the driver for the FAPL */
    if (H5P_set_driver(plist, H5FD_CORE, &fa) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTSET, FAIL, "can't set core VFD as driver")

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_core() */

/*-------------------------------------------------------------------------
 * Function:    H5Pget_fapl_core
 *
 * Purpose:     Queries properties set by the H5Pset_fapl_core() function.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Tuesday, August 10, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pget_fapl_core(hid_t fapl_id, size_t *increment /*out*/, hbool_t *backing_store /*out*/)
{
    H5P_genplist_t *        plist;               /* Property list pointer */
    const H5FD_core_fapl_t *fa;                  /* Core VFD info */
    herr_t                  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE3("e", "ixx", fapl_id, increment, backing_store);

    if (NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")
    if (H5FD_CORE != H5P_peek_driver(plist))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "incorrect VFL driver")
    if (NULL == (fa = (const H5FD_core_fapl_t *)H5P_peek_driver_info(plist)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, FAIL, "bad VFL driver info")

    if (increment)
        *increment = fa->increment;
    if (backing_store)
        *backing_store = fa->backing_store;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5Pget_fapl_core() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_fapl_get
 *
 * Purpose:     Returns a copy of the file access properties.
 *
 * Return:      Success:    Ptr to new file access properties.
 *              Failure:    NULL
 *
 * Programmer:  Robb Matzke
 *              Friday, August 13, 1999
 *
 *-------------------------------------------------------------------------
 */
static void *
H5FD__core_fapl_get(H5FD_t *_file)
{
    H5FD_core_t *     file = (H5FD_core_t *)_file;
    H5FD_core_fapl_t *fa;               /* Core VFD info */
    void *            ret_value = NULL; /* Return value */

    FUNC_ENTER_STATIC

    if (NULL == (fa = (H5FD_core_fapl_t *)H5MM_calloc(sizeof(H5FD_core_fapl_t))))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "memory allocation failed")

    fa->increment      = file->increment;
    fa->backing_store  = file->file_opened;
    fa->write_tracking = file->write_tracking;
    fa->page_size      = file->bstore_page_size;

    /* Set return value */
    ret_value = fa;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_fapl_get() */

/*-------------------------------------------------------------------------
 * Function:    H5FD___core_open
 *
 * Purpose:     Create memory as an HDF5 file.
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
H5FD__core_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    H5FD_core_t *           file = NULL;
    const H5FD_core_fapl_t *fa   = NULL;
    H5P_genplist_t *        plist; /* Property list pointer */
    H5FD_file_image_info_t file_image_info;
    H5FD_t *               ret_value = NULL; /* Return value */

    FUNC_ENTER_STATIC

    /* Create the new file struct */
    if (NULL == (file = (H5FD_core_t *)H5MM_calloc(sizeof(H5FD_core_t))))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "unable to allocate file struct")

    /* Track the "file" name, mainly for 'cmp' callback when no backing file opened */
    if (name && *name)
        file->name = H5MM_xstrdup(name);

    /* Get the core VFD's properties */
    HDassert(H5P_DEFAULT != fapl_id);
    if (NULL == (plist = (H5P_genplist_t *)H5I_object(fapl_id)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list")
    if (NULL == (fa = (const H5FD_core_fapl_t *)H5P_peek_driver_info(plist)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, NULL, "bad VFL driver info")

    /* Retrieve initial file image info */
    if (H5P_peek(plist, H5F_ACS_FILE_IMAGE_INFO_NAME, &file_image_info) < 0)
        HGOTO_ERROR(H5E_PLIST, H5E_CANTGET, NULL, "can't get initial file image info")

    /* If the file image exists and this is an open, make sure the file doesn't exist */
    HDassert(((file_image_info.buffer != NULL) && (file_image_info.size > 0)) ||
             ((file_image_info.buffer == NULL) && (file_image_info.size == 0)));
    if ((file_image_info.buffer != NULL) && !(H5F_ACC_CREAT & flags)) {
        if (0 == HDaccess(name, F_OK))
            HGOTO_ERROR(H5E_FILE, H5E_FILEEXISTS, NULL, "file already exists")

        /* If backing store is requested, create and stat the file
         * Note: We are forcing the O_CREAT flag here, even though this is
         * technically an open.
         */
        if (fa->backing_store) {
            if (H5FD__posix_common_open(name, flags | H5F_ACC_CREAT, maxaddr, fapl_id, &file->pos_com, NULL, NULL) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTOPENFILE, NULL, "can't open file")
            file->file_opened = TRUE;
        } /* end if */
    }     /* end if */
    /* Open backing store, and get stat() from file.  The only case that backing
     * store is off is when  the backing_store flag is off and H5F_ACC_CREAT is
     * on. */
    else if (fa->backing_store || !(H5F_ACC_CREAT & flags)) {
        if (H5FD__posix_common_open(name, flags, maxaddr, fapl_id, &file->pos_com, NULL, NULL) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTOPENFILE, NULL, "can't open file")
        file->file_opened = TRUE;
    } /* end if */

    /* Keep a copy of the name, for comparisons */
    if (name && *name)
        file->name = H5MM_xstrdup(name);

    /* The increment comes from either the file access property list or the
     * default value. But if the file access property list was zero then use
     * the default value instead.
     */
    file->increment = (fa->increment > 0) ? fa->increment : H5FD_CORE_INCREMENT;

    /* If save data in backing store. */
    file->backing_store = fa->backing_store;

    /* Save file image callbacks */
    file->fi_callbacks = file_image_info.callbacks;

    /* If an existing file is opened, load the whole file into memory. */
    if (!(H5F_ACC_CREAT & flags)) {
        size_t size;

        /* Retrieve file size */
        if (file_image_info.buffer && file_image_info.size > 0)
            size = file_image_info.size;
        else {
            haddr_t eof = HADDR_UNDEF;          /* EOF for the file */

            /* Get the file's EOF */
            if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, NULL, "unable to get EOF for file")

            H5_CHECKED_ASSIGN(size, size_t, eof, hsize_t);
        } /* end else */

        /* Check if we should allocate the memory buffer and read in existing data */
        if (size) {
            /* Allocate memory for the file's data, using the file image callback if available. */
            if (file->fi_callbacks.image_malloc) {
                if (NULL == (file->mem = (unsigned char *)file->fi_callbacks.image_malloc(
                                 size, H5FD_FILE_IMAGE_OP_FILE_OPEN, file->fi_callbacks.udata)))
                    HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "image malloc callback failed")
            } /* end if */
            else {
                if (NULL == (file->mem = (unsigned char *)H5MM_malloc(size)))
                    HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, NULL, "unable to allocate memory block")
            } /* end else */

            /* If there is an initial file image, copy it, using the callback if possible */
            if (file_image_info.buffer && file_image_info.size > 0) {
                if (file->fi_callbacks.image_memcpy) {
                    if (file->mem != file->fi_callbacks.image_memcpy(file->mem, file_image_info.buffer, size,
                                                                     H5FD_FILE_IMAGE_OP_FILE_OPEN,
                                                                     file->fi_callbacks.udata))
                        HGOTO_ERROR(H5E_FILE, H5E_CANTCOPY, NULL, "image_memcpy callback failed")
                } /* end if */
                else
                    H5MM_memcpy(file->mem, file_image_info.buffer, size);
            } /* end if */
            /* Read in existing data from the file if there is no image */
            else {
                if (H5FD__posix_common_read(&file->pos_com, 0, size, file->mem, NULL) < 0)
                    HGOTO_ERROR(H5E_VFL, H5E_READERROR, NULL, "can't read image from file")
            } /* end else */
        } /* end if */
    } /* end if */

    /* Get the write tracking & page size */
    file->write_tracking   = fa->write_tracking;
    file->bstore_page_size = fa->page_size;

    /* Set up write tracking if the backing store is on */
    file->dirty_list = NULL;
    if (fa->backing_store) {
        hbool_t use_write_tracking = FALSE; /* what we're actually doing */

        /* default is to have write tracking OFF for create (hence the check to see
         * if the user explicitly set a page size) and ON with the default page size
         * on open (when not read-only).
         */
        /* Only use write tracking if the file is open for writing */
        use_write_tracking = (TRUE == fa->write_tracking) /* user asked for write tracking */
                             && (flags & H5F_ACC_RDWR)    /* file is open for writing (i.e. not read-only) */
                             && (file->bstore_page_size != 0); /* page size is not zero */

        /* initialize the dirty list */
        if (use_write_tracking)
            if (NULL == (file->dirty_list = H5SL_create(H5SL_TYPE_HADDR, NULL)))
                HGOTO_ERROR(H5E_SLIST, H5E_CANTCREATE, NULL, "can't create core vfd dirty region list");
    } /* end if */

    /* Set return value */
    ret_value = (H5FD_t *)file;

done:
    if (!ret_value && file) {
        if (file->file_opened)
            H5FD__posix_common_close(&file->pos_com, NULL);
        H5MM_xfree(file->name);
        H5MM_xfree(file->mem);
        H5MM_xfree(file);
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_open() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_close
 *
 * Purpose:     Closes the file.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_close(H5FD_t *_file)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Flush any changed buffers */
    if (H5FD__core_flush(_file, (hid_t)-1, TRUE) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTFLUSH, FAIL, "unable to flush core vfd backing store")

    /* Destroy the dirty region list */
    if (file->dirty_list)
        if (H5FD__core_destroy_dirty_list(file) != SUCCEED)
            HGOTO_ERROR(H5E_VFL, H5E_CANTFREE, FAIL, "unable to free core vfd dirty region list")

    /* Close the underlying file */
    if (file->file_opened)
        if (H5FD__posix_common_close(&file->pos_com, NULL) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEFILE, FAIL, "unable to close file")

    /* Release resources */
    if (file->name)
        H5MM_xfree(file->name);
    if (file->mem) {
        /* Use image callback if available */
        if (file->fi_callbacks.image_free) {
            if (file->fi_callbacks.image_free(file->mem, H5FD_FILE_IMAGE_OP_FILE_CLOSE,
                                              file->fi_callbacks.udata) < 0)
                HGOTO_ERROR(H5E_FILE, H5E_CANTFREE, FAIL, "image_free callback failed")
        } /* end if */
        else
            H5MM_xfree(file->mem);
    } /* end if */
    HDmemset(file, 0, sizeof(H5FD_core_t));
    H5MM_xfree(file);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_close() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_cmp
 *
 * Purpose:     Compares two files belonging to this driver by name. If one
 *              file doesn't have a name then it is less than the other file.
 *              If neither file has a name then the comparison is by file
 *              address.
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
H5FD__core_cmp(const H5FD_t *_f1, const H5FD_t *_f2)
{
    const H5FD_core_t *f1        = (const H5FD_core_t *)_f1;
    const H5FD_core_t *f2        = (const H5FD_core_t *)_f2;
    int                ret_value = 0;

    FUNC_ENTER_STATIC_NOERR

    if (f1->file_opened && f2->file_opened)
        ret_value = H5FD__posix_common_cmp(&f1->pos_com, &f2->pos_com);
    else {
        if (NULL == f1->name && NULL == f2->name) {
            if (f1 < f2)
                HGOTO_DONE(-1)
            else if (f1 > f2)
                HGOTO_DONE(1)
            HGOTO_DONE(0)
        } /* end if */

        if (NULL == f1->name)
            HGOTO_DONE(-1)
        else if (NULL == f2->name)
            HGOTO_DONE(1)

        ret_value = HDstrcmp(f1->name, f2->name);
    } /* end else */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_query
 *
 * Purpose:     Set the flags that this VFL driver is capable of supporting.
 *              (listed in H5FDpublic.h)
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Tuesday, October  7, 2008
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_query(const H5FD_t *_file, unsigned long *flags /* out */)
{
    const H5FD_core_t *file = (const H5FD_core_t *)_file;

    FUNC_ENTER_STATIC_NOERR

    /* clang-format off */
    /* Set the VFL feature flags that this driver supports */
    if (flags) {
        *flags = 0;
        *flags |= H5FD_FEAT_AGGREGATE_METADATA;             /* OK to aggregate metadata allocations                             */
        *flags |= H5FD_FEAT_ACCUMULATE_METADATA;            /* OK to accumulate metadata for faster writes                      */
        *flags |= H5FD_FEAT_DATA_SIEVE;                     /* OK to perform data sieving for faster raw data reads & writes    */
        *flags |= H5FD_FEAT_AGGREGATE_SMALLDATA;            /* OK to aggregate "small" raw data allocations                     */
        *flags |= H5FD_FEAT_ALLOW_FILE_IMAGE;               /* OK to use file image feature with this VFD                       */
        *flags |= H5FD_FEAT_CAN_USE_FILE_IMAGE_CALLBACKS;   /* OK to use file image callbacks with this VFD                     */

        /* These feature flags are only applicable if the backing store is enabled */
        if (file && file->file_opened && file->backing_store) {
            *flags |= H5FD_FEAT_POSIX_COMPAT_HANDLE;        /* get_handle callback returns a POSIX file descriptor              */
            *flags |= H5FD_FEAT_DEFAULT_VFD_COMPATIBLE;     /* VFD creates a file which can be opened with the default VFD      */
        } /* end if */
    } /* end if */
    /* clang-format on */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__core_query() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_get_eoa
 *
 * Purpose:     Gets the end-of-address marker for the file. The EOA marker
 *              is the first address past the last byte allocated in the
 *              format address space.
 *
 * Return:      The end-of-address marker. (Can't fail)
 *
 * Programmer:  Robb Matzke
 *              Monday, August  2, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__core_get_eoa(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_core_t *file = (const H5FD_core_t *)_file;
    haddr_t eoa = HADDR_UNDEF;          /* EOA for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Get the file's EOA */
    if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOA for file")

    /* Set the return value */
    ret_value = eoa;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_get_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_set_eoa
 *
 * Purpose:     Set the end-of-address marker for the file. This function is
 *              called shortly after an existing HDF5 file is opened in order
 *              to tell the driver where the end of the HDF5 data is located.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_set_eoa(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, haddr_t addr)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Set the file's EOA */
    if (H5FD__posix_common_set_eoa(&file->pos_com, addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set EOA for file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_set_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_get_eof
 *
 * Purpose:     Returns the end-of-file marker, which is the greater of
 *              either the size of the underlying memory or the HDF5
 *              end-of-address markers.
 *
 * Return:      End of file address, the first address past
 *              the end of the "file", either the memory
 *              or the HDF5 file. (Can't fail)
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__core_get_eof(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_core_t *file = (const H5FD_core_t *)_file;
    haddr_t eof = HADDR_UNDEF;          /* EOF for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Get the file's EOF */
    if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOF for file")

    /* Set the return value */
    ret_value = eof;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_get_eof() */

/*-------------------------------------------------------------------------
 * Function:       H5FD__core_get_handle
 *
 * Purpose:        Gets the file handle of CORE file driver.
 *
 * Returns:        SUCCEED/FAIL
 *
 * Programmer:     Raymond Lu
 *                 Sept. 16, 2002
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_get_handle(H5FD_t *_file, hid_t fapl, void **file_handle)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file; /* core VFD info */
    herr_t       ret_value = SUCCEED;              /* Return value */

    FUNC_ENTER_STATIC

    /* Check args */
    if (!file_handle)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file handle not valid")

    /* Check for non-default FAPL */
    if (H5P_FILE_ACCESS_DEFAULT != fapl && H5P_DEFAULT != fapl) {
        H5P_genplist_t *plist; /* Property list pointer */

        /* Get the FAPL */
        if (NULL == (plist = (H5P_genplist_t *)H5I_object(fapl)))
            HGOTO_ERROR(H5E_VFL, H5E_BADTYPE, FAIL, "not a file access property list")

        /* Check if private property for retrieving the backing store POSIX
         * file descriptor is set.  (This should not be set except within the
         * library)  QAK - 2009/12/04
         */
        if (H5P_exist_plist(plist, H5F_ACS_WANT_POSIX_FD_NAME) > 0) {
            hbool_t want_posix_fd; /* Setting for retrieving file descriptor from core VFD */

            /* Get property */
            if (H5P_get(plist, H5F_ACS_WANT_POSIX_FD_NAME, &want_posix_fd) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't get property of retrieving file descriptor")

            /* If property is set, pass back the file descriptor instead of the memory address */
            if (want_posix_fd) {
                /* Get the file's handle */
                if (H5FD__posix_common_get_handle(&file->pos_com, file_handle) < 0)
                    HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get handle for file")
            } /* end if */
            else
                *file_handle = &(file->mem);
        } /* end if */
        else
            *file_handle = &(file->mem);
    } /* end if */
    else
        *file_handle = &(file->mem);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_get_handle() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_read
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
H5FD__core_read(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr,
                size_t size, void *buf /*out*/)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file;
    haddr_t      eof       = HADDR_UNDEF; /* EOF for the file */
    herr_t       ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(buf);

    /* Check for overflow conditions */
    if (!H5F_addr_defined(addr))
        HGOTO_ERROR(H5E_IO, H5E_OVERFLOW, FAIL, "file address overflowed")
    if (H5FD_POSIX_REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_IO, H5E_OVERFLOW, FAIL, "file address overflowed")

    /* Get the file's EOF */
    if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOF for file")

    /* Read the part which is before the EOF marker */
    if (addr < eof) {
        size_t nbytes;
#ifndef NDEBUG
        hsize_t temp_nbytes;

        temp_nbytes = eof - addr;
        H5_CHECK_OVERFLOW(temp_nbytes, hsize_t, size_t);
        nbytes = MIN(size, (size_t)temp_nbytes);
#else /* NDEBUG */
        nbytes = MIN(size, (size_t)(eof - addr));
#endif /* NDEBUG */

        H5MM_memcpy(buf, file->mem + addr, nbytes);
        size -= nbytes;
        addr += nbytes;
        buf = (char *)buf + nbytes;
    }

    /* Read zeros for the part which is after the EOF markers */
    if (size > 0)
        HDmemset(buf, 0, size);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_read() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_write
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
H5FD__core_write(H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr,
                 size_t size, const void *buf)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file;
    haddr_t      eof       = HADDR_UNDEF; /* EOF for the file */
    herr_t       ret_value = SUCCEED;     /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(buf);

    /* Check for overflow conditions */
    if (H5FD_POSIX_REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_IO, H5E_OVERFLOW, FAIL, "file address overflowed")

    /* Get the file's EOF */
    if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOF for file")

    /*
     * Allocate more memory if necessary, careful of overflow. Also, if the
     * allocation fails then the file should remain in a usable state.  Be
     * careful of non-Posix realloc() that doesn't understand what to do when
     * the first argument is null.
     */
    if (addr + size > eof) {
        unsigned char *x;
        size_t         new_eof;

        /* Determine new size of memory buffer */
        H5_CHECKED_ASSIGN(new_eof, size_t, file->increment * ((addr + size) / file->increment), hsize_t);
        if ((addr + size) % file->increment)
            new_eof += file->increment;

        /* (Re)allocate memory for the file buffer, using callbacks if available */
        if (file->fi_callbacks.image_realloc) {
            if (NULL == (x = (unsigned char *)file->fi_callbacks.image_realloc(
                             file->mem, new_eof, H5FD_FILE_IMAGE_OP_FILE_RESIZE, file->fi_callbacks.udata)))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL,
                            "unable to allocate memory block of %llu bytes with callback",
                            (unsigned long long)new_eof)
        } /* end if */
        else {
            if (NULL == (x = (unsigned char *)H5MM_realloc(file->mem, new_eof)))
                HGOTO_ERROR(H5E_RESOURCE, H5E_CANTALLOC, FAIL,
                            "unable to allocate memory block of %llu bytes", (unsigned long long)new_eof)
        } /* end else */

        HDmemset(x + eof, 0, (size_t)(new_eof - eof));
        file->mem = x;

        /* Set the file's EOF */
        if (H5FD__posix_common_set_eof(&file->pos_com, new_eof) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set EOF for file")
    } /* end if */

    /* Add the buffer region to the dirty list if using that optimization */
    if (file->dirty_list) {
        haddr_t start = addr;
        haddr_t end   = addr + (haddr_t)size - 1;

        if (H5FD__core_add_dirty_region(file, start, end) != SUCCEED)
            HGOTO_ERROR(
                H5E_VFL, H5E_CANTINSERT, FAIL,
                "unable to add core VFD dirty region during write call - addresses: start=%llu end=%llu",
                (unsigned long long)start, (unsigned long long)end)
    }

    /* Write from BUF to memory */
    H5MM_memcpy(file->mem + addr, buf, size);

    /* Mark memory buffer as modified */
    file->dirty = TRUE;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_write() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_flush
 *
 * Purpose:     Flushes the file to backing store if there is any and if the
 *              dirty flag is set.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Friday, October 15, 1999
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_flush(H5FD_t *_file, hid_t H5_ATTR_UNUSED dxpl_id, hbool_t H5_ATTR_UNUSED closing)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Write to backing store */
    if (file->dirty && file->file_opened && file->backing_store) {
        haddr_t eof = HADDR_UNDEF;          /* EOF for the file */

        /* Get the file's EOF */
        if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOF for file")

        /* Use the dirty list, if available */
        if (file->dirty_list) {
            H5FD_core_region_t *item = NULL;
            size_t              size;

            while (NULL != (item = (H5FD_core_region_t *)H5SL_remove_first(file->dirty_list))) {

                /* The file may have been truncated, so check for that
                 * and skip or adjust as necessary.
                 */
                if (item->start < eof) {
                    if (item->end >= eof)
                        item->end = eof - 1;

                    size = (size_t)((item->end - item->start) + 1);

                    if (H5FD__posix_common_write(&file->pos_com, item->start, size, file->mem + item->start, NULL) < 0)
                        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "unable to write to backing store")
                } /* end if */

                item = H5FL_FREE(H5FD_core_region_t, item);
            } /* end while */
        } /* end if */
        /* Otherwise, write the entire file out at once */
        else {
            if (H5FD__posix_common_write(&file->pos_com, (haddr_t)0, (size_t)eof, file->mem, NULL) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "unable to write to backing store")
        } /* end else */

        file->dirty = FALSE;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_flush() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_truncate
 *
 * Purpose:     Makes sure that the true file size is the same (or larger)
 *              than the end-of-address.
 *
 * Note:        For file images opened with the core file driver, it is
 *              necessary that we avoid reallocating the core file driver's
 *              buffer uneccessarily.
 *
 *              If we are closing, and there is no backing store, this
 *              function is a no-op.
 *
 *              If we are closing, and there is backing store, we set the
 *              EOF to equal the EOA, and truncate the backing store to
 *              the new EOF.
 *
 *              If we are not closing, we realloc the buffer to size equal
 *              to the smallest multiple of the allocation increment that
 *              equals or exceeds the EOA and set the EOF accordingly.
 *              The backing store is _not_ truncated to the new EOF.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Quincey Koziol
 *              Tuesday, October  7, 2008
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_truncate(H5FD_t *_file, hid_t H5_ATTR_UNUSED dxpl_id, hbool_t closing)
{
    H5FD_core_t *file = (H5FD_core_t *)_file;
    herr_t       ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* if we are closing and not using backing store, do nothing */
    if (!closing || file->backing_store) {
        haddr_t eoa = HADDR_UNDEF;      /* EOA for the file */
        haddr_t eof = HADDR_UNDEF;      /* EOF for the file */
        size_t new_eof;                 /* New size of memory buffer */

        /* Get the file's EOA */
        if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOA for file")

        if (closing) /* set EOF to EOA */
            new_eof = eoa;
        else { /* set EOF to smallest multiple of increment that exceeds EOA */
            /* Determine new size of memory buffer */
            H5_CHECKED_ASSIGN(new_eof, size_t, file->increment * (eoa / file->increment), hsize_t);
            if (eoa % file->increment)
                new_eof += file->increment;
        } /* end else */

        /* Get the file's current EOF */
        if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOF for file")

        /* Extend the file to make sure it's large enough */
        if (!H5F_addr_eq(eof, (haddr_t)new_eof)) {
            unsigned char *x; /* Pointer to new buffer for file data */

            /* (Re)allocate memory for the file buffer, using callback if available */
            if (file->fi_callbacks.image_realloc) {
                if (NULL == (x = (unsigned char *)file->fi_callbacks.image_realloc(file->mem,
                        new_eof, H5FD_FILE_IMAGE_OP_FILE_RESIZE, file->fi_callbacks.udata)))
                  HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, FAIL, "unable to allocate memory block with callback")
            } /* end if */
            else {
                if (NULL == (x = (unsigned char *)H5MM_realloc(file->mem, new_eof)))
                    HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, FAIL, "unable to allocate memory block")
            } /* end else */

            if (eof < new_eof)
                HDmemset(x + eof, 0, (size_t)(new_eof - eof));
            file->mem = x;

            /* Update backing store file, if using it and if closing */
            if (closing && file->file_opened && file->backing_store)
                /* Truncate the file */
                if (H5FD__posix_common_truncate(&file->pos_com, new_eof, NULL) < 0)
                    HGOTO_ERROR(H5E_VFL, H5E_CANTTRUNCATE, FAIL, "can't truncate file")

            /* Set the file's EOF */
            if (H5FD__posix_common_set_eof(&file->pos_com, new_eof) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set EOF for file")
        } /* end if */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_truncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_lock
 *
 * Purpose:     To place an advisory lock on a file.
 *        The lock type to apply depends on the parameter "rw":
 *            TRUE--opens for write: an exclusive lock
 *            FALSE--opens for read: a shared lock
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__core_lock(H5FD_t *_file, hbool_t rw)
{
    H5FD_core_t *file = (H5FD_core_t *)_file; /* VFD file struct          */
    herr_t       ret_value = SUCCEED;         /* Return value             */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Only lock if there is an opened backing file */
    if (file->file_opened)
        /* Lock the file */
        if (H5FD__posix_common_lock(&file->pos_com, rw, NULL) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTLOCK, FAIL, "can't lock file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_lock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__core_unlock
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
H5FD__core_unlock(H5FD_t *_file)
{
    H5FD_core_t *file      = (H5FD_core_t *)_file; /* VFD file struct */
    herr_t       ret_value = SUCCEED;              /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Only unlock if there is an opened backing file */
    if (file->file_opened)
        /* Unlock the file */
        if (H5FD__posix_common_unlock(&file->pos_com, NULL) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTUNLOCK, FAIL, "can't unlock file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__core_unlock() */
