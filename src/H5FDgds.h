/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of the HDF5 GDS Virtual File Driver. The full copyright *
 * notice, including terms governing use, modification, and redistribution,  *
 * is contained in the COPYING file, which can be found at the root of the   *
 * source code distribution tree.                                            *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * Programmer:  John Ravi <jjravi@lbl.gov>
 *              Wednesday, July  1, 2020
 *
 * Purpose:	The public header file for the CUDA GPUDirect Storage driver.
 */
#ifndef H5FDgds_H
#define H5FDgds_H

/* Public header files */
#include "H5FDpublic.h" /* File drivers             */

#ifdef H5_HAVE_GDS_VFD

/** ID for the GDS VFD */
#define H5FD_GDS (H5OPEN H5FD_GDS_id_g)

/** Identifier for the GDS VFD \since 1.14.0 */
#define H5FD_GDS_VALUE H5_VFD_GDS

#define H5FD_GDS_NAME "gds"

#else

/** Initializer for the GDS VFD (disabled) */
#define H5FD_GDS       (H5I_INVALID_HID)

/** Identifier for the GDS VFD (disabled) */
#define H5FD_GDS_VALUE H5_VFD_INVALID

#endif /* H5_HAVE_GDS_VFD */

/* Default values for memory boundary, file block size, and maximal copy buffer size.
 * Application can set these values through the function H5Pset_fapl_gds. */
#define H5FD_GDS_MBOUNDARY_DEF 4096
#define H5FD_GDS_FBSIZE_DEF    4096
#define H5FD_GDS_CBSIZE_DEF    (16 * 1024 * 1024)

#ifdef H5_HAVE_GDS_VFD
#ifdef __cplusplus
extern "C" {
#endif

/** @private
 *
 * \brief ID for the mpio VFD
 */
H5_DLLVAR hid_t H5FD_GDS_id_g;

herr_t H5Pset_fapl_gds(hid_t fapl_id, size_t alignment, size_t block_size, size_t cbuf_size);
herr_t H5Pget_fapl_gds(hid_t fapl_id, size_t *boundary /*out*/, size_t *block_size /*out*/,
                       size_t *cbuf_size /*out*/);

#ifdef __cplusplus
}
#endif
#endif /* H5_HAVE_GDS_VFD */

#endif
