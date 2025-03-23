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
 * Purpose: The Virtual File Layer as described in documentation.
 *          This is the greatest common denominator for all types of
 *          storage access whether a file, memory, network, etc. This
 *          layer usually just dispatches the request to an actual
 *          file driver layer.
 */

/****************/
/* Module Setup */
/****************/

#define H5F_FRIEND      /* Suppress error about including H5Fpkg */
#include "H5FDmodule.h" /* This source code file is part of the H5FD module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions                        */
#include "H5CXprivate.h" /* API Contexts                             */
#include "H5Eprivate.h"  /* Error handling                           */
#include "H5Fpkg.h"      /* File access                              */
#include "H5FDpkg.h"     /* File Drivers                             */
#include "H5FLprivate.h" /* Free Lists                               */
#include "H5Iprivate.h"  /* IDs                                      */
#include "H5MMprivate.h" /* Memory management                        */
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

/* Package initialization variable */
bool H5_PKG_INIT_VAR = false;

/* Whether to ignore file locks when disabled (env var value) */
htri_t H5FD_ignore_disabled_file_locks_p = FAIL;

/* Declare extern the free list to manage the H5FD_int_t struct */
H5FL_EXTERN(H5FD_int_t);

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5FDregister
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
hid_t
H5FDregister(const H5FD_class_t *cls)
{
    H5FD_driver_t *driver = NULL;
    H5FD_mem_t     type;
    hid_t          ret_value = H5I_INVALID_HID;

    FUNC_ENTER_API(H5I_INVALID_HID)

    /* Check arguments */
    if (!cls)
        HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, H5I_INVALID_HID, "null class pointer is disallowed");
    if (cls->version != H5FD_CLASS_VERSION)
        HGOTO_ERROR(H5E_ARGS, H5E_VERSION, H5I_INVALID_HID, "wrong file driver version #");
    if (!cls->open || !cls->close)
        HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, H5I_INVALID_HID,
                    "'open' and/or 'close' methods are not defined");
    if (!cls->get_eoa || !cls->set_eoa)
        HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, H5I_INVALID_HID,
                    "'get_eoa' and/or 'set_eoa' methods are not defined");
    if (!cls->get_eof)
        HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, H5I_INVALID_HID, "'get_eof' method is not defined");
    if (!cls->read || !cls->write)
        HGOTO_ERROR(H5E_ARGS, H5E_UNINITIALIZED, H5I_INVALID_HID,
                    "'read' and/or 'write' method is not defined");
    for (type = H5FD_MEM_DEFAULT; type < H5FD_MEM_NTYPES; type++)
        if (cls->fl_map[type] < H5FD_MEM_NOLIST || cls->fl_map[type] >= H5FD_MEM_NTYPES)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, H5I_INVALID_HID, "invalid free-list mapping");

    /* Create the new class ID */
    if (NULL == (driver = H5FD__driver_register(cls)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, H5I_INVALID_HID, "unable to register file driver");

    if ((ret_value = H5I_register(H5I_VFL, driver, true)) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTREGISTER, FAIL, "can't create ID for driver");

    /* ID is holding a reference to the driver */
    H5FD__driver_inc_rc(driver);

done:
    /* Release the driver on error */
    if (ret_value < 0)
        if (driver && H5FD__driver_dec_rc(driver) < 0)
            HDONE_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "unable to release driver");

    FUNC_LEAVE_API(ret_value)
} /* end H5FDregister() */

/*-------------------------------------------------------------------------
 * Function:    H5FDis_driver_registered_by_name
 *
 * Purpose:     Tests whether a VFD class has been registered or not
 *              according to a supplied driver name.
 *
 * Return:      >0 if a VFD with that name has been registered
 *              0 if a VFD with that name has NOT been registered
 *              <0 on errors
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5FDis_driver_registered_by_name(const char *driver_name)
{
    htri_t ret_value = false; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check if driver with this name is registered */
    if ((ret_value = H5FD__is_driver_registered_by_name(driver_name, NULL)) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't check if VFD is registered");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDis_driver_registered_by_name() */

/*-------------------------------------------------------------------------
 * Function:    H5FDis_driver_registered_by_value
 *
 * Purpose:     Tests whether a VFD class has been registered or not
 *              according to a supplied driver value (ID).
 *
 * Return:      >0 if a VFD with that value has been registered
 *              0 if a VFD with that value hasn't been registered
 *              <0 on errors
 *
 *-------------------------------------------------------------------------
 */
htri_t
H5FDis_driver_registered_by_value(H5FD_class_value_t driver_value)
{
    htri_t ret_value = false;

    FUNC_ENTER_API(FAIL)

    /* Check if driver with this value is registered */
    if ((ret_value = H5FD__is_driver_registered_by_value(driver_value, NULL)) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't check if VFD is registered");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDis_driver_registered_by_value() */

/*-------------------------------------------------------------------------
 * Function:    H5FDunregister
 *
 * Purpose:     Removes a driver ID from the library. This in no way affects
 *              file access property lists which have been defined to use
 *              this driver or files which are already opened under this
 *              driver.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDunregister(hid_t driver_id)
{
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (NULL == H5I_object_verify(driver_id, H5I_VFL))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file driver");

    /* The H5FD_class_t struct will be freed by this function */
    if (H5I_dec_app_ref(driver_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "unable to unregister file driver");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDunregister() */

/*---------------------------------------------------------------------------
 * Function:    H5FDcmp_driver_cls
 *
 * Purpose:     Compares two driver classes (based on their value field)
 *
 * Note:        This routine is _only_ for HDF5 VFD driver authors!  It is
 *              _not_ part of the public API for HDF5 application developers.
 *
 * Return:      Success:    Non-negative, *cmp set to a value like strcmp
 *              Failure:    Negative, *cmp unset
 *
 *---------------------------------------------------------------------------
 */
herr_t
H5FDcmp_driver_cls(int *cmp, hid_t driver_id1, hid_t driver_id2)
{
    H5FD_driver_t *drvr1, *drvr2;       /* Drivers for IDs */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check args and get driver pointers */
    if (NULL == (drvr1 = H5I_object_verify(driver_id1, H5I_VFL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VFD driver ID");
    if (NULL == (drvr2 = H5I_object_verify(driver_id2, H5I_VFL)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a VFD driver ID");

    /* Compare the two VFD driver classes */
    if (H5FD_cmp_driver_cls(cmp, drvr1->cls, drvr2->cls) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTCOMPARE, FAIL, "can't compare driver classes");

done:
    FUNC_LEAVE_API(ret_value)
} /* H5FDcmp_driver_cls() */

/*-------------------------------------------------------------------------
 * Function:    H5FDopen
 *
 * Purpose:     Opens a file named NAME for the type(s) of access described
 *              by the bit vector FLAGS according to a file access property
 *              list FAPL_ID (which may be the constant H5P_DEFAULT). The
 *              file should expect to handle format addresses in the range [0,
 *              MAXADDR] (if MAXADDR is the undefined address then the caller
 *              doesn't care about the address range).
 *
 *              Possible values for the FLAGS bits are:
 *
 *              H5F_ACC_RDWR:   Open the file for read and write access. If
 *                              this bit is not set then open the file for
 *                              read only access. It is permissible to open a
 *                              file for read and write access when only read
 *                              access is requested by the library (the
 *                              library will never attempt to write to a file
 *                              which it opened with only read access).
 *
 *              H5F_ACC_CREATE: Create the file if it doesn't already exist.
 *                              However, see H5F_ACC_EXCL below.
 *
 *              H5F_ACC_TRUNC:  Truncate the file if it already exists. This
 *                              is equivalent to deleting the file and then
 *                              creating a new empty file.
 *
 *              H5F_ACC_EXCL:   When used with H5F_ACC_CREATE, if the file
 *                              already exists then the open should fail.
 *                              Note that this is unsupported/broken with
 *                              some file drivers (e.g., sec2 across nfs) and
 *                              will contain a race condition when used to
 *                              perform file locking.
 *
 *              The MAXADDR is the maximum address which will be requested by
 *              the library during an allocation operation. Usually this is
 *              the same value as the MAXADDR field of the class structure,
 *              but it can be smaller if the driver is being used under some
 *              other driver.
 *
 *              Note that when the driver 'open' callback gets control that
 *              the public part of the file struct (the H5FD_t part) will be
 *              incomplete and will be filled in after that callback returns.
 *
 * Return:      Success:    Pointer to a new file driver struct.
 *
 *              Failure:    NULL
 *
 *-------------------------------------------------------------------------
 */
H5FD_t *
H5FDopen(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    H5P_genplist_t *fapl; /* File access property list */
    H5FD_int_t     *fh        = NULL;
    H5FD_t         *ret_value = NULL;

    FUNC_ENTER_API(NULL)

    /* Check arguments */
    if (NULL == (fapl = H5P_object_verify(fapl_id, H5P_TYPE_FILE_ACCESS, true)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list");

    /* Call private function */
    if (H5FD_open(false, &fh, name, flags, fapl, maxaddr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, NULL, "unable to open file");

    /* Set return value */
    ret_value = fh->file;

done:
    /* Clean up */
    if (fh) {
        if (fh->driver && H5FD__driver_dec_rc(fh->driver) < 0)
            HDONE_ERROR(H5E_VFL, H5E_CANTDEC, NULL, "unable to release driver");
        H5FL_FREE(H5FD_int_t, fh);
    }

    FUNC_LEAVE_API(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:    H5FDclose
 *
 * Purpose:     Closes the file by calling the driver 'close' callback, which
 *              should free all driver-private data and free the file struct.
 *              Note that the public part of the file struct (the H5FD_t part)
 *              will be all zero during the driver close callback like during
 *              the 'open' callback.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDclose(H5FD_t *file)
{
    H5FD_int_t    *fh        = NULL;    /* Temporary internal file handle */
    H5FD_driver_t *driver    = NULL;    /* Temporary VFD driver */
    herr_t         ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Set up internal file handle */
    if (NULL == (fh = H5FL_CALLOC(H5FD_int_t)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTALLOC, FAIL, "unable to allocate internal file handle");

    /* Find correct driver */
    if (true != H5FD__is_driver_registered_by_value(file->cls->value, &driver))
        HGOTO_ERROR(H5E_VFL, H5E_NOTFOUND, FAIL, "can't find matching VFD driver");
    assert(driver);

    /* Construct temporary internal file handle */
    if (H5FD__driver_inc_rc(driver) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINC, FAIL, "unable to increment ref count on VFL driver");
    fh->driver = driver;
    fh->file   = file;

    /* Call private function */
    /* (releases the temporary file handle) */
    if (H5FD_close(fh) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEFILE, FAIL, "unable to close file");

done:
    /* Clean up on error */
    if (ret_value < 0)
        if (fh) {
            if (fh->driver && H5FD__driver_dec_rc(fh->driver) < 0)
                HDONE_ERROR(H5E_VFL, H5E_CANTDEC, FAIL, "unable to release driver");
            H5FL_FREE(H5FD_int_t, fh);
        }

    FUNC_LEAVE_API(ret_value)
} /* end H5FDclose() */

/*-------------------------------------------------------------------------
 * Function:    H5FDcmp
 *
 * Purpose:     Compare the keys of two files using the file driver callback
 *              if the files belong to the same driver, otherwise sort the
 *              files by driver class pointer value.
 *
 * Return:      Success:    A value like strcmp()
 *
 *              Failure:    Must never fail. If both file handles are
 *                          invalid then they compare equal. If one file
 *                          handle is invalid then it compares less than
 *                          the other.  If both files belong to the same
 *                          driver and the driver doesn't provide a
 *                          comparison callback then the file pointers
 *                          themselves are compared.
 *
 *-------------------------------------------------------------------------
 */
int
H5FDcmp(const H5FD_t *f1, const H5FD_t *f2)
{
    H5FD_int_t    fh1, fh2;         /* Temporary internal file handles */
    H5FD_driver_t driver1, driver2; /* Temporary VFD drivers */
    int           ret_value = -1;

    FUNC_ENTER_API(-1) /* return value is arbitrary */

    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    /* Construct temporary internal file handles */
    H5FD__construct_tmp_fh((H5FD_t *)f1, &fh1, &driver1);
    H5FD__construct_tmp_fh((H5FD_t *)f2, &fh2, &driver2);
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    /* Call private function */
    ret_value = H5FD_cmp(&fh1, &fh2);

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDcmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FDquery
 *
 * Purpose:     Query a VFL driver for its feature flags. (listed in H5FDpublic.h)
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDquery(const H5FD_t *file, unsigned long *flags /*out*/)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (!flags)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "flags parameter cannot be NULL");

    H5_GCC_CLANG_DIAG_OFF("cast-qual")
    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh((H5FD_t *)file, &fh, &driver);
    H5_GCC_CLANG_DIAG_ON("cast-qual")

    /* Call private function */
    if (H5FD__query(&fh, flags) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to query feature flags");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDquery() */

/*-------------------------------------------------------------------------
 * Function:    H5FDalloc
 *
 * Purpose:     Allocates SIZE bytes of memory from the FILE. The memory will
 *              be used according to the allocation class TYPE. First we try
 *              to satisfy the request from one of the free lists, according
 *              to the free list map provided by the driver. The free list
 *              array has one entry for each request type and the value of
 *              that array element can be one of four possibilities:
 *
 *              It can be the constant H5FD_MEM_DEFAULT (or zero) which
 *              indicates that the identity mapping is used. In other
 *              words, the request type maps to its own free list.
 *
 *              It can be the request type itself, which has the same
 *              effect as the H5FD_MEM_DEFAULT value above.
 *
 *              It can be the ID for another request type, which
 *              indicates that the free list for the specified type
 *              should be used instead.
 *
 *              It can be the constant H5FD_MEM_NOLIST which means that
 *              no free list should be used for this type of request.
 *
 *              If the request cannot be satisfied from a free list then
 *              either the driver's 'alloc' callback is invoked (if one was
 *              supplied) or the end-of-address marker is extended. The
 *              'alloc' callback is always called with the same arguments as
 *              the H5FDalloc().
 *
 * Return:      Success:    The format address of the new file memory.
 *              Failure:    The undefined address HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FDalloc(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, hsize_t size)
{
    H5FD_int_t    fh;     /* Temporary internal file handle */
    H5FD_driver_t driver; /* Temporary VFD driver */
    haddr_t       ret_value = HADDR_UNDEF;

    FUNC_ENTER_API(HADDR_UNDEF)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file class pointer cannot be NULL");
    if (type < H5FD_MEM_DEFAULT || type >= H5FD_MEM_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "invalid request type");
    if (size == 0)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "zero-size request");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, HADDR_UNDEF, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (HADDR_UNDEF == (ret_value = H5FD__alloc_real(&fh, type, size, NULL, NULL)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, HADDR_UNDEF, "unable to allocate file memory");

    /* (Note compensating for base address subtraction in internal routine) */
    ret_value += file->base_addr;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDalloc() */

/*-------------------------------------------------------------------------
 * Function:    H5FDfree
 *
 * Purpose:     Frees format addresses starting with ADDR and continuing for
 *              SIZE bytes in the file FILE. The type of space being freed is
 *              specified by TYPE, which is mapped to a free list as
 *              described for the H5FDalloc() function above.  If the request
 *              doesn't map to a free list then either the application 'free'
 *              callback is invoked (if defined) or the memory is leaked.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDfree(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, haddr_t addr, hsize_t size)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (type < H5FD_MEM_DEFAULT || type >= H5FD_MEM_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid request type");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__free_real(&fh, type, addr - file->base_addr, size) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTFREE, FAIL, "file deallocation request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDfree() */

/*-------------------------------------------------------------------------
 * Function:    H5FDget_eoa
 *
 * Purpose:     Returns the address of the first byte after the last
 *              allocated memory in the file.
 *
 * Return:      Success:    First byte after allocated memory.
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FDget_eoa(H5FD_t *file, H5FD_mem_t type)
{
    H5FD_int_t    fh;     /* Temporary internal file handle */
    H5FD_driver_t driver; /* Temporary VFD driver */
    haddr_t       ret_value;

    FUNC_ENTER_API(HADDR_UNDEF)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file class pointer cannot be NULL");
    if (type < H5FD_MEM_DEFAULT || type >= H5FD_MEM_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "invalid file type");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (HADDR_UNDEF == (ret_value = H5FD_get_eoa(&fh, type)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, HADDR_UNDEF, "file get eoa request failed");

    /* (Note compensating for base address subtraction in internal routine) */
    ret_value += file->base_addr;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDget_eoa() */

/*-------------------------------------------------------------------------
 * Function:	H5FDset_eoa
 *
 * Purpose:	Set the end-of-address marker for the file. The ADDR is the
 *		address of the first byte past the last allocated byte of the
 *		file. This function is called from two places:
 *
 *		    It is called after an existing file is opened in order to
 *		    "allocate" enough space to read the superblock and then
 *		    to "allocate" the entire hdf5 file based on the contents
 *		    of the superblock.
 *
 *		    It is called during file memory allocation if the
 *		    allocation request cannot be satisfied from the free list
 *		    and the driver didn't supply an allocation callback.
 *
 * Return:	Success:	Non-negative
 *		Failure:	Negative, no side effect
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDset_eoa(H5FD_t *file, H5FD_mem_t type, haddr_t addr)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (type < H5FD_MEM_DEFAULT || type >= H5FD_MEM_NTYPES)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid file type");
    if (!H5_addr_defined(addr) || addr > file->maxaddr)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "invalid end-of-address value");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD_set_eoa(&fh, type, addr - file->base_addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "file set eoa request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDset_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FDget_eof
 *
 * Purpose:     Returns the end-of-file address, which is the greater of the
 *              end-of-format address and the actual EOF marker. This
 *              function is called after an existing file is opened in order
 *              for the library to learn the true size of the underlying file
 *              and to determine whether the hdf5 data has been truncated.
 *
 *              It is also used when a file is first opened to learn whether
 *              the file is empty or not.
 *
 *              It is permissible for the driver to return the maximum address
 *              for the file size if the file is not empty.
 *
 * Return:      Success:    The EOF address.
 *
 *              Failure:    HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FDget_eof(H5FD_t *file, H5FD_mem_t type)
{
    H5FD_int_t    fh;     /* Temporary internal file handle */
    H5FD_driver_t driver; /* Temporary VFD driver */
    haddr_t       ret_value;

    FUNC_ENTER_API(HADDR_UNDEF)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, HADDR_UNDEF, "file class pointer cannot be NULL");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (HADDR_UNDEF == (ret_value = H5FD_get_eof(&fh, type)))
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, HADDR_UNDEF, "file get eof request failed");

    /* (Note compensating for base address subtraction in internal routine) */
    ret_value += file->base_addr;

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDget_eof() */

/*--------------------------------------------------------------------------
 * Function:    H5FDget_vfd_handle
 *
 * Purpose:     Returns a pointer to the file handle of low-level virtual
 *              file driver.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *--------------------------------------------------------------------------
 */
herr_t
H5FDget_vfd_handle(H5FD_t *file, hid_t fapl_id, void **file_handle /*out*/)
{
    H5FD_int_t      fh;     /* Temporary internal file handle */
    H5FD_driver_t   driver; /* Temporary VFD driver */
    H5P_genplist_t *fapl;   /* File access property list */
    herr_t          ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (NULL == (fapl = H5P_object_verify(fapl_id, H5P_TYPE_FILE_ACCESS, true)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "fapl_id parameter is not a file access property list");
    if (!file_handle)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file handle parameter cannot be NULL");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_get_vfd_handle(&fh, fapl, file_handle) < 0)
        HGOTO_ERROR(H5E_FILE, H5E_CANTGET, FAIL, "can't get file handle for file driver");

done:
    if (FAIL == ret_value)
        if (file_handle)
            *file_handle = NULL;

    FUNC_LEAVE_API(ret_value)
} /* end H5FDget_vfd_handle() */

/*-------------------------------------------------------------------------
 * Function:    H5FDread
 *
 * Purpose:     Reads SIZE bytes from FILE beginning at address ADDR
 *              according to the data transfer property list DXPL_ID (which may
 *              be the constant H5P_DEFAULT). The result is written into the
 *              buffer BUF.
 *
 * Return:      Success:    Non-negative
 *                          The read result is written into the BUF buffer
 *                          which should be allocated by the caller.
 *
 *              Failure:	Negative
 *                          The contents of BUF are undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDread(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, haddr_t addr, size_t size, void *buf /*out*/)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (!buf)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "result buffer parameter can't be NULL");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD_read(&fh, type, addr - file->base_addr, size, buf) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "file read request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDread() */

/*-------------------------------------------------------------------------
 * Function:    H5FDwrite
 *
 * Purpose:     Writes SIZE bytes to FILE beginning at address ADDR according
 *              to the data transfer property list DXPL_ID (which may be the
 *              constant H5P_DEFAULT). The bytes to be written come from the
 *              buffer BUF.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDwrite(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, haddr_t addr, size_t size, const void *buf)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (!buf)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "result buffer parameter can't be NULL");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD_write(&fh, type, addr - file->base_addr, size, buf) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "file write request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDwrite() */

/*-------------------------------------------------------------------------
 * Function:    H5FDread_vector
 *
 * Purpose:     Perform count reads from the specified file at the offsets
 *              provided in the addrs array, with the lengths and memory
 *              types provided in the sizes and types arrays.  Data read
 *              is returned in the buffers provided in the bufs array.
 *
 *              All reads are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
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
H5FDread_vector(H5FD_t *file, hid_t dxpl_id, uint32_t count, H5FD_mem_t types[], haddr_t addrs[],
                size_t sizes[], void *bufs[] /* out */)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!types)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "types parameter can't be NULL if count is positive");
        if (!addrs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "addrs parameter can't be NULL if count is positive");
        if (!sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (types[0] == H5FD_MEM_NOLIST)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "count[0] can't be H5FD_MEM_NOLIST");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base addresses addition in internal routine) */
    if (H5FD_read_vector(&fh, count, types, addrs, sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "file vector read request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDread_vector() */

/*-------------------------------------------------------------------------
 * Function:    H5FDwrite_vector
 *
 * Purpose:     Perform count writes to the specified file at the offsets
 *              provided in the addrs array, with the lengths and memory
 *              types provided in the sizes and types arrays.  Data to be
 *              written is in the buffers provided in the bufs array.
 *
 *              All writes are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully
 *
 *              Failure:    FAIL
 *                          One or more of the writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDwrite_vector(H5FD_t *file, hid_t dxpl_id, uint32_t count, H5FD_mem_t types[], haddr_t addrs[],
                 size_t sizes[], const void *bufs[] /* in */)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!types)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "types parameter can't be NULL if count is positive");
        if (!addrs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "addrs parameter can't be NULL if count is positive");
        if (!sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (types[0] == H5FD_MEM_NOLIST)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "count[0] can't be H5FD_MEM_NOLIST");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD_write_vector(&fh, count, types, addrs, sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "file vector write request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDwrite_vector() */

/*-------------------------------------------------------------------------
 * Function:    H5FDread_selection
 *
 * Purpose:     Perform count reads from the specified file at the
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
 *              of individual reads.
 *
 *              All reads are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
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
H5FDread_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count, hid_t mem_space_ids[],
                   hid_t file_space_ids[], haddr_t offsets[], size_t element_sizes[], void *bufs[] /* out */)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__read_selection_id(H5FD_IO_SKIP_NO_CB, &fh, type, count, mem_space_ids, file_space_ids, offsets,
                                element_sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "file selection read request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDread_selection() */

/*-------------------------------------------------------------------------
 * Function:    H5FDwrite_selection
 *
 * Purpose:     Perform count writes to the specified file at the
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
 *              of individual writes.
 *
 *              All writes are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully
 *
 *              Failure:    FAIL
 *                          One or more of the writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDwrite_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count, hid_t mem_space_ids[],
                    hid_t file_space_ids[], haddr_t offsets[], size_t element_sizes[], const void *bufs[])
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__write_selection_id(H5FD_IO_SKIP_NO_CB, &fh, type, count, mem_space_ids, file_space_ids, offsets,
                                 element_sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "file selection write request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDwrite_selection() */

/*-------------------------------------------------------------------------
 * Purpose:     This is similar to H5FDread_selection() with the
 *              exception noted below.
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
 *              Note:
 *              It will skip selection read call whether the underlying VFD
 *              supports selection reads or not.
 *
 *              It will translate the selection read to a vector read call
 *              if vector reads are supported, or a series of scalar read
 *              calls otherwise.
 *
 *              All reads are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All reads have completed successfully, and
 *                          the results have been written into the supplied
 *                          buffers.
 *
 *              Failure:    FAIL
 *                          The contents of supplied buffers are undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDread_vector_from_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count,
                               hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                               size_t element_sizes[], void *bufs[] /* out */)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__read_vector_from_selection(&fh, type, count, mem_space_ids, file_space_ids, offsets,
                                         element_sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "file selection read request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDread_vector_from_selection() */

/*-------------------------------------------------------------------------
 * Purpose:     This is similar to H5FDwrite_selection() with the
 *              exception noted below.
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
 *              Note:
 *              It will skip selection write call whether the underlying VFD
 *              supports selection writes or not.
 *
 *              It will translate the selection write to a vector write call
 *              if vector writes are supported, or a series of scalar write
 *              calls otherwise.
 *
 *              All writes are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully
 *
 *              Failure:    FAIL
 *                          One or more of the writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDwrite_vector_from_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count,
                                hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                                size_t element_sizes[], const void *bufs[])
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__write_vector_from_selection(&fh, type, count, mem_space_ids, file_space_ids, offsets,
                                          element_sizes, bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "file selection write request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDwrite_vector_from_selection() */

/*-------------------------------------------------------------------------
 * Purpose:     This is similar to H5FDread_selection() with the
 *              exception noted below.
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
 *              Note:
 *              It will skip selection and vector read calls whether the underlying
 *              VFD supports selection and vector reads or not.
 *
 *              It will translate the selection read to a series of
 *              scalar read calls.
 *
 *              All reads are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All reads have completed successfully, and
 *                          the results have been written into the supplied
 *                          buffers.
 *
 *              Failure:    FAIL
 *                          The contents of supplied buffers are undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDread_from_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count, hid_t mem_space_ids[],
                        hid_t file_space_ids[], haddr_t offsets[], size_t element_sizes[], void *bufs[])
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__read_from_selection(&fh, type, count, mem_space_ids, file_space_ids, offsets, element_sizes,
                                  bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "file selection read request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDread_from_selection() */

/*-------------------------------------------------------------------------
 * Purpose:     This is similar to H5FDwrite_selection() with the
 *              exception noted below.
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
 *              Note:
 *              It will skip selection and vector write calls whether the underlying
 *              VFD supports selection and vector writes or not.
 *
 *              It will translate the selection write to a series of
 *              scalar write calls.
 *
 *              All writes are done according to the data transfer property
 *              list dxpl_id (which may be the constant H5P_DEFAULT).
 *
 * Return:      Success:    SUCCEED
 *                          All writes have completed successfully
 *
 *              Failure:    FAIL
 *                          One or more of the writes failed.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDwrite_from_selection(H5FD_t *file, H5FD_mem_t type, hid_t dxpl_id, uint32_t count, hid_t mem_space_ids[],
                         hid_t file_space_ids[], haddr_t offsets[], size_t element_sizes[],
                         const void *bufs[])
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");
    if (count > 0) {
        if (!mem_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "mem_spaces parameter can't be NULL if count is positive");
        if (!file_space_ids)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "file_spaces parameter can't be NULL if count is positive");
        if (!offsets)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "offsets parameter can't be NULL if count is positive");
        if (!element_sizes)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL,
                        "element_sizes parameter can't be NULL if count is positive");
        if (!bufs)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs parameter can't be NULL if count is positive");
        if (element_sizes[0] == 0)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "sizes[0] can't be 0");
        if (bufs[0] == NULL)
            HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "bufs[0] can't be NULL");
    }

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    /* (Note compensating for base address addition in internal routine) */
    if (H5FD__write_from_selection(&fh, type, count, mem_space_ids, file_space_ids, offsets, element_sizes,
                                   bufs) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "file selection write request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDwrite_from_selection() */

/*-------------------------------------------------------------------------
 * Function:    H5FDflush
 *
 * Purpose:     Notify driver to flush all cached data.  If the driver has no
 *              flush method then nothing happens.
 *
 * Return:      Non-negative on success/Negative on failureL
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDflush(H5FD_t *file, hid_t dxpl_id, hbool_t closing)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_flush(&fh, closing) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTFLUSH, FAIL, "file flush request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDflush() */

/*-------------------------------------------------------------------------
 * Function:    H5FDtruncate
 *
 * Purpose:     Notify driver to truncate the file back to the allocated size.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDtruncate(H5FD_t *file, hid_t dxpl_id, hbool_t closing)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Set DXPL for operation */
    if (H5CX_set_dxpl(dxpl_id) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "can't set DXPL for operation");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_truncate(&fh, closing) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTUPDATE, FAIL, "file flush request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDtruncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FDlock
 *
 * Purpose:     Set a file lock
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDlock(H5FD_t *file, hbool_t rw)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_lock(&fh, rw) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTLOCKFILE, FAIL, "file lock request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDlock() */

/*-------------------------------------------------------------------------
 * Function:    H5FDunlock
 *
 * Purpose:     Remove a file lock
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDunlock(H5FD_t *file)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_unlock(&fh) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTUNLOCKFILE, FAIL, "file unlock request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDunlock() */

/*-------------------------------------------------------------------------
 * Function:    H5FDctl
 *
 * Purpose:     Perform a CTL operation.
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
H5FDctl(H5FD_t *file, uint64_t op_code, uint64_t flags, const void *input, void **output)
{
    H5FD_int_t    fh;                  /* Temporary internal file handle */
    H5FD_driver_t driver;              /* Temporary VFD driver */
    herr_t        ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!file)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file pointer cannot be NULL");
    if (!file->cls)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file class pointer cannot be NULL");

    /* Don't attempt to validate the op code.  If appropriate, that will
     * be done by the underlying VFD callback, along with the input and
     * output parameters.
     */

    /* Construct temporary internal file handle */
    H5FD__construct_tmp_fh(file, &fh, &driver);

    /* Call private function */
    if (H5FD_ctl(&fh, op_code, flags, input, output) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_FCNTL, FAIL, "VFD ctl request failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDctl() */

/*-------------------------------------------------------------------------
 * Function: H5FDdriver_query
 *
 * Purpose:  Similar to H5FD_query(), but intended for cases when we don't
 *           have a file available (e.g. before one is opened). Since we
 *           can't use the file to get the driver, the driver ID is passed
 *           in as a parameter.
 *
 * Return:   Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDdriver_query(hid_t driver_id, unsigned long *flags /*out*/)
{
    H5FD_driver_t *driver    = NULL;    /* Pointer to VFD driver struct  */
    herr_t         ret_value = SUCCEED; /* Return value                 */

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (NULL == flags)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "flags parameter cannot be NULL");

    /* Check for the driver to query and then query it */
    if (NULL == (driver = (H5FD_driver_t *)H5I_object_verify(driver_id, H5I_VFL)))
        HGOTO_ERROR(H5E_ID, H5E_BADID, FAIL, "not a VFL ID");
    if (H5FD_driver_query(driver, flags) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "driver flag query failed");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDdriver_query() */

/*-------------------------------------------------------------------------
 * Function:    H5FDdelete
 *
 * Purpose:     Deletes a file
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FDdelete(const char *filename, hid_t fapl_id)
{
    H5P_genplist_t *fapl; /* File access property list */
    herr_t          ret_value = SUCCEED;

    FUNC_ENTER_API(FAIL)

    /* Check arguments */
    if (!filename || !*filename)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "no file name specified");

    /* Get the file access property list */
    if (NULL == (fapl = H5P_object_verify(fapl_id, H5P_TYPE_FILE_ACCESS, true)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list");

    /* Call private function */
    if (H5FD_delete(filename, fapl) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTDELETEFILE, FAIL, "unable to delete file");

done:
    FUNC_LEAVE_API(ret_value)
} /* end H5FDdelete() */
