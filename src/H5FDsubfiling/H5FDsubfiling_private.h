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
 * Purpose:	The private header file for the subfiling VFD driver.
 */

#ifndef H5FDsubfiling_private_H
#define H5FDsubfiling_private_H

/* Include driver's public header */
#include "H5FDsubfiling.h" /* subfiling VFD driver     */

/* Private headers needed by this file */
#include "H5FDprivate.h" /* File drivers        */

/**************************/
/* Library Private Macros */
/**************************/

/*
 * Name of the HDF5 FAPL property that the Subfiling VFD uses to pass its
 * configuration down to the underlying IOC VFD
 */
#define H5F_ACS_SUBFILING_CONFIG_PROP_NAME "H5FD_SUBFILING_CONFIG_PROP"

/* Value of invalid stub file ID */
#define H5FD_SUBFILING_BAD_FILE_ID UINT64_MAX

/****************************/
/* Library Private Typedefs */
/****************************/

/*****************************/
/* Library Private Variables */
/*****************************/

/* The subfiling VFD driver */
H5_DLLVAR H5FD_driver_t *H5FD_SUBFILING_driver_g;

/******************************/
/* Library Private Prototypes */
/******************************/

#endif /* H5FDsubfiling_private_H */
