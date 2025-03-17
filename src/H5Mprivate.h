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
 * This file contains private information about the H5M module
 */
#ifndef H5Mprivate_H
#define H5Mprivate_H

/* Include package's public header */
#include "H5Mpublic.h"

/* Private headers needed by this file */

/**************************/
/* Library Private Macros */
/**************************/

/*
 * Feature: Define H5M_DEBUG on the compiler command line if you want to
 *        debug maps. NDEBUG must not be defined in order for this
 *        to have any effect.
 */
#ifdef NDEBUG
#undef H5M_DEBUG
#endif

/* ========  Map creation property names ======== */

/* ========  Map access property names ======== */
#define H5M_ACS_KEY_PREFETCH_SIZE_NAME                                                                       \
    "key_prefetch_size" /* Number of keys to prefetch during map iteration */
#define H5M_ACS_KEY_ALLOC_SIZE_NAME                                                                          \
    "key_alloc_size" /* Initial allocation size for keys prefetched during map iteration */

/****************************/
/* Library Private Typedefs */
/****************************/
H5_DLL herr_t H5M_init(void);

/*****************************/
/* Library Private Variables */
/*****************************/

/******************************/
/* Library Private Prototypes */
/******************************/

#endif /* H5Mprivate_H */
