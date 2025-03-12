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
 * Purpose: This file contains declarations which are visible only within
 *          the H5FD package.  Source files outside the H5FD package should
 *          include H5FDprivate.h instead.
 */
#include "H5Pprivate.h"
#if !(defined H5FD_FRIEND || defined H5FD_MODULE)
#error "Do not include this file outside the H5FD package!"
#endif

#ifndef H5FDpkg_H
#define H5FDpkg_H

/* Get package's private header */
#include "H5FDprivate.h" /* File drivers			*/

/* Other private headers needed by this file */

/**************************/
/* Package Private Macros */
/**************************/

/* These macros check for overflow of various quantities. They are suitable
 * for VFDs that are "file-like" where lseek(2), etc. is used to move around
 * via HDoff_t units (i.e., most VFDs aside from the core VFD).
 *
 * These macros assume that HDoff_t is signed and haddr_t and size_t are unsigned.
 *
 * H5FD_ADDR_OVERFLOW:   Checks whether a file address of type `haddr_t'
 *                       is too large to be represented by the second argument
 *                       of the file seek function.
 *
 * H5FD_SIZE_OVERFLOW:   Checks whether a buffer size of type `hsize_t' is too
 *                       large to be represented by the `size_t' type.
 *
 * H5FD_REGION_OVERFLOW: Checks whether an address and size pair describe data
 *                       which can be addressed entirely by the second
 *                       argument of the file seek function.
 */
#define H5FD_MAXADDR          (((haddr_t)1 << (8 * sizeof(HDoff_t) - 1)) - 1)
#define H5FD_ADDR_OVERFLOW(A) (HADDR_UNDEF == (A) || ((A) & ~(haddr_t)H5FD_MAXADDR))
#define H5FD_SIZE_OVERFLOW(Z) ((Z) & ~(hsize_t)H5FD_MAXADDR)
#define H5FD_REGION_OVERFLOW(A, Z)                                                                           \
    (H5FD_ADDR_OVERFLOW(A) || H5FD_SIZE_OVERFLOW(Z) || HADDR_UNDEF == (A) + (Z) ||                           \
     (HDoff_t)((A) + (Z)) < (HDoff_t)(A))

/* Length of stack allocated arrays for dataspace IDs/structs for selection I/O
 * operations. Corresponds to the number of file selection/memory selection
 * pairs (along with addresses, etc.) in a selection I/O operation. If more
 * space is needed dynamic allocation will be used instead */
#define H5FD_LOCAL_SEL_ARR_LEN 8

#define H5FD_IO_SKIP_NO_CB        0x00u
#define H5FD_IO_SKIP_SELECTION_CB 0x01u
#define H5FD_IO_SKIP_VECTOR_CB    0x02u

/****************************/
/* Package Private Typedefs */
/****************************/

/* Internal struct to track VFD drivers */
struct H5FD_driver_t {
    const H5FD_class_t   *cls;         /* Pointer to driver class struct   */
    int64_t               nrefs;       /* Number of references to this struct */
    struct H5FD_driver_t *next, *prev; /* Pointers to the next & previous */
};

/*****************************/
/* Package Private Variables */
/*****************************/

/* Whether to ignore file locks when disabled (env var value) */
H5_DLLVAR htri_t H5FD_ignore_disabled_file_locks_p;

/* Global count of the number of H5FD_t's handed out. */
H5_DLLVAR unsigned long H5FD_file_serial_no_p;

/******************************/
/* Package Private Prototypes */
/******************************/
H5_DLL haddr_t H5FD__alloc_real(H5FD_int_t *fh, H5FD_mem_t type, hsize_t size, haddr_t *align_addr,
                                hsize_t *align_size);
H5_DLL herr_t  H5FD__free_real(H5FD_int_t *fh, H5FD_mem_t type, haddr_t addr, hsize_t size);
H5_DLL herr_t  H5FD__read_selection_id(uint32_t skip_cb, H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                       hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                                       size_t element_sizes[], void *bufs[] /* out */);
H5_DLL herr_t  H5FD__write_selection_id(uint32_t skip_cb, H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                        hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                                        size_t element_sizes[], const void *bufs[]);
H5_DLL herr_t  H5FD__read_vector_from_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                                hid_t mem_space_ids[], hid_t file_space_ids[],
                                                haddr_t offsets[], size_t element_sizes[], void *bufs[]);
H5_DLL herr_t  H5FD__write_vector_from_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                                 hid_t mem_space_ids[], hid_t file_space_ids[],
                                                 haddr_t offsets[], size_t element_sizes[],
                                                 const void *bufs[]);
H5_DLL herr_t  H5FD__read_from_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                         hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                                         size_t element_sizes[], void *bufs[]);
H5_DLL herr_t  H5FD__write_from_selection(H5FD_int_t *fh, H5FD_mem_t type, uint32_t count,
                                          hid_t mem_space_ids[], hid_t file_space_ids[], haddr_t offsets[],
                                          size_t element_sizes[], const void *bufs[]);
H5_DLL herr_t  H5FD__read_selection_translate(bool skip_vector_cb, H5FD_int_t *fh, H5FD_mem_t type,
                                              uint32_t count, H5S_t **mem_spaces, H5S_t **file_spaces,
                                              haddr_t offsets[], size_t element_sizes[],
                                              void *bufs[] /* out */);
H5_DLL herr_t  H5FD__write_selection_translate(bool skip_vector_cb, H5FD_int_t *fh, H5FD_mem_t type,
                                               uint32_t count, H5S_t **mem_spaces, H5S_t **file_spaces,
                                               haddr_t offsets[], size_t element_sizes[], const void *bufs[]);

/* Internal VFD init/term routines */
H5_DLL herr_t H5FD__core_register(void);
H5_DLL herr_t H5FD__core_unregister(void);
#ifdef H5_HAVE_DIRECT
H5_DLL herr_t H5FD__direct_register(void);
H5_DLL herr_t H5FD__direct_unregister(void);
#endif
H5_DLL herr_t H5FD__family_register(void);
H5_DLL herr_t H5FD__family_unregister(void);
#ifdef H5_HAVE_LIBHDFS
H5_DLL herr_t H5FD__hdfs_register(void);
H5_DLL herr_t H5FD__hdfs_unregister(void);
#endif
#ifdef H5_HAVE_IOC_VFD
H5_DLL herr_t H5FD__ioc_register(void);
H5_DLL herr_t H5FD__ioc_unregister(void);
#endif
H5_DLL herr_t H5FD__log_register(void);
H5_DLL herr_t H5FD__log_unregister(void);
#ifdef H5_HAVE_MIRROR_VFD
H5_DLL herr_t H5FD__mirror_register(void);
H5_DLL herr_t H5FD__mirror_unregister(void);
#endif
#ifdef H5_HAVE_PARALLEL
H5_DLL herr_t H5FD__mpio_register(void);
H5_DLL herr_t H5FD__mpio_unregister(void);
#endif
H5_DLL herr_t H5FD__multi_register(void);
H5_DLL herr_t H5FD__multi_unregister(void);
H5_DLL herr_t H5FD__onion_register(void);
H5_DLL herr_t H5FD__onion_unregister(void);
#ifdef H5_HAVE_ROS3_VFD
H5_DLL herr_t H5FD__ros3_register(void);
H5_DLL herr_t H5FD__ros3_unregister(void);
#endif
H5_DLL herr_t H5FD__sec2_register(void);
H5_DLL herr_t H5FD__sec2_unregister(void);
H5_DLL herr_t H5FD__splitter_register(void);
H5_DLL herr_t H5FD__splitter_unregister(void);
H5_DLL herr_t H5FD__stdio_register(void);
H5_DLL herr_t H5FD__stdio_unregister(void);
#ifdef H5_HAVE_SUBFILING_VFD
H5_DLL herr_t H5FD__subfiling_register(void);
H5_DLL herr_t H5FD__subfiling_unregister(void);
#endif

/* Driver operations */
H5_DLL H5FD_driver_t *H5FD__driver_register(const H5FD_class_t *cls);
H5_DLL H5FD_driver_t *H5FD__register_driver_by_name(const char *name);
H5_DLL H5FD_driver_t *H5FD__register_driver_by_value(H5FD_class_value_t value);
H5_DLL htri_t  H5FD__is_driver_registered_by_name(const char *driver_name, H5FD_driver_t **registered_driver);
H5_DLL htri_t  H5FD__is_driver_registered_by_value(H5FD_class_value_t driver_value,
                                                   H5FD_driver_t    **registered_driver);
H5_DLL int64_t H5FD__driver_inc_rc(H5FD_driver_t *driver);
H5_DLL int64_t H5FD__driver_dec_rc(H5FD_driver_t *driver);

/* Driver callback equivalents */
H5_DLL herr_t H5FD__sb_decode(H5FD_int_t *fh, const char *name, const uint8_t *buf);
H5_DLL herr_t H5FD__query(const H5FD_int_t *f, unsigned long *flags /*out*/);

/* Testing functions */
#ifdef H5FD_TESTING
H5_DLL bool H5FD__supports_swmr_test(const char *vfd_name);
#endif /* H5FD_TESTING */

/*-------------------------------------------------------------------------
 * Function:    H5FD__construct_tmp_fh
 *
 * Purpose:     Constructs a temporary 'internal' file handle for an
 *              'external' one.
 *
 * Return:      SUCCEED/FAIL
 *
 *-------------------------------------------------------------------------
 */
static inline void H5_ATTR_UNUSED
H5FD__construct_tmp_fh(H5FD_t *file, H5FD_int_t /*OUT*/ *fh, H5FD_driver_t /*OUT*/ *driver)
{
    FUNC_ENTER_PACKAGE_NAMECHECK_ONLY

    assert(file);
    assert(fh);
    assert(driver);

    /* Set up internal file handle, using info from external one */
    driver->cls   = file->cls;
    driver->nrefs = 1;
    driver->next = driver->prev = NULL;
    fh->file                    = file;
    fh->driver                  = driver;

    FUNC_LEAVE_NOAPI_VOID_NAMECHECK_ONLY
} /* end H5FD__construct_tmp_fh() */

#endif /* H5FDpkg_H */
