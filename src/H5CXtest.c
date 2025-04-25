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
 * Purpose:	API context testing functions.
 */

/****************/
/* Module Setup */
/****************/

#include "H5CXmodule.h" /* This source code file is part of the H5CX module */
#define H5CX_TESTING    /*suppress warning about H5CX testing funcs*/

/***********/
/* Headers */
/***********/
#include "H5private.h"  /* Generic Functions                        */
#include "H5CXpkg.h"    /* API Contexts                             */
#include "H5Eprivate.h" /* Error handling                           */

/****************/
/* Local Macros */
/****************/

/******************/
/* Local Typedefs */
/******************/

/********************/
/* Local Prototypes */
/********************/

/*********************/
/* Package Variables */
/*********************/

/*******************/
/* Local Variables */
/*******************/

/*-------------------------------------------------------------------------
 * Function:    H5CX_reset_fapl_test
 *
 * Purpose:     Reset the property cache for the API context's FAPL in a test
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_reset_fapl_test(void)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Reset the cached data */
    H5CX__reset_fapl(*head);

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_reset_fapl_test() */

/*-------------------------------------------------------------------------
 * Function:    H5CX_reset_ocpl_test
 *
 * Purpose:     Reset the property cache for the API context's OCPL in a test
 *
 * Return:      None
 *
 *-------------------------------------------------------------------------
 */
void
H5CX_reset_ocpl_test(void)
{
    H5CX_node_t **head = NULL; /* Pointer to head of API context list */

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Sanity check */
    head = H5CX_get_my_context(); /* Get the pointer to the head of the API context, for this thread */
    assert(head && *head);

    /* Reset the cached data */
    H5CX__reset_ocpl(*head);

    FUNC_LEAVE_NOAPI_VOID
} /* end H5CX_reset_ocpl_test() */
