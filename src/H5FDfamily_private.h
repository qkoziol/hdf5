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
 * Purpose:	The private header file for the family VFD driver.
 */

#ifndef H5FDfamily_private_H
#define H5FDfamily_private_H

/* Include driver's public header */
#include "H5FDfamily.h" /* family VFD driver     */

/* Private headers needed by this file */
#include "H5FDprivate.h" /* File drivers        */

/**************************/
/* Library Private Macros */
/**************************/

/****************************/
/* Library Private Typedefs */
/****************************/

/*****************************/
/* Library Private Variables */
/*****************************/

/* The family VFD driver */
H5_DLLVAR H5FD_driver_t *H5FD_FAMILY_driver_g;

/******************************/
/* Library Private Prototypes */
/******************************/

#endif /* H5FDfamily_private_H */
