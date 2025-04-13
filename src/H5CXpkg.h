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
 *          the H5CX package.  Source files outside the H5CX package should
 *          include H5CXprivate.h instead.
 */
#if !(defined H5CX_FRIEND || defined H5CX_MODULE)
#error "Do not include this file outside the H5CX package!"
#endif

#ifndef H5CXpkg_H
#define H5CXpkg_H

/* Get package's private header */
#include "H5CXprivate.h"

/* Other private headers needed by this file */

/**************************/
/* Package Private Macros */
/**************************/

#ifdef H5_HAVE_THREADSAFE_API
/*
 * The per-thread API context.
 *
 * In order for this macro to work, H5CX_get_my_context() must be preceded
 * by "H5CX_node_t **ctx =".
 */
#define H5CX_get_my_context() H5TS_get_api_ctx_ptr()
#else /* H5_HAVE_THREADSAFE_API */
/*
 * The current API context.
 */
#define H5CX_get_my_context() (&H5CX_head_g)
#endif /* H5_HAVE_THREADSAFE_API */

/****************************/
/* Package Private Typedefs */
/****************************/

/*****************************/
/* Package Private Variables */
/*****************************/

#ifndef H5_HAVE_THREADSAFE_API
H5_DLLVAR H5CX_node_t *H5CX_head_g; /* Pointer to head of context stack */
#endif                              /* H5_HAVE_THREADSAFE_API */

/******************************/
/* Package Private Prototypes */
/******************************/

/* Reset the property cache for the API context's FAPL */
H5_DLL void H5CX__reset_fapl(H5CX_node_t *head);

#endif /*H5CXpkg_H*/
