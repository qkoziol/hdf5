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

/****************/
/* Module Setup */
/****************/

#include "H5Fmodule.h" /* This source code file is part of the H5F module */

/***********/
/* Headers */
/***********/
#include "H5private.h"   /* Generic Functions		*/
#include "H5Eprivate.h"  /* Error handling		  	*/
#include "H5Fpkg.h"      /* File access				*/
#include "H5FLprivate.h" /* Free Lists              */

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

/* Declare extern the free list to manage the H5F_super_t struct */
H5FL_EXTERN(H5F_super_t);

/*****************************/
/* Library Private Variables */
/*****************************/

/*******************/
/* Local Variables */
/*******************/


/*-------------------------------------------------------------------------
 * Function:    H5F_fake_alloc
 *
 * Purpose:     Allocate a "fake" file structure, for various routines to
 *              use for encoding/decoding data structures using internal API
 *              routines that need a file structure, but don't ultimately
 *              depend on having a "real" file.
 *
 * Return:      Success:        Pointer to 'faked up' file structure
 *              Failure:        NULL
 *
 *-------------------------------------------------------------------------
 */
H5F_t *
H5F_fake_alloc(uint8_t sizeof_size)
{
    H5F_t *f         = NULL; /* Pointer to fake file struct */
    H5F_t *ret_value = NULL; /* Return value */

    FUNC_ENTER_NOAPI(NULL)

    /* Allocate faked file struct */
    if (NULL == (f = H5FL_CALLOC(H5F_t)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate top file structure");
    if (NULL == (f->shared = H5FL_CALLOC(H5F_shared_t)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate shared file structure");
    if (NULL == (f->shared->sblock = H5FL_CALLOC(H5F_super_t)))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "can't allocate superblock");

    /* Only set fields necessary for clients */
    if (sizeof_size == 0)
        f->shared->sblock->sizeof_size = H5F_OBJ_SIZE_SIZE;
    else
        f->shared->sblock->sizeof_size = sizeof_size;
    f->shared->sizeof_size = f->shared->sblock->sizeof_size;

    /* Set return value */
    ret_value = f;

done:
    if (!ret_value)
        H5F_fake_free(f);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5F_fake_alloc() */

/*-------------------------------------------------------------------------
 * Function:    H5F_fake_free
 *
 * Purpose:     Free a "fake" file structure.
 *
 * Return:	Success:	non-negative
 *		Failure:	negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5F_fake_free(H5F_t *f)
{
    FUNC_ENTER_NOAPI_NOINIT_NOERR

    /* Free faked file struct */
    if (f) {
        /* Destroy shared file & superblock struct */
        if (f->shared) {
            if (f->shared->sblock)
                f->shared->sblock = H5FL_FREE(H5F_super_t, f->shared->sblock);
            f->shared = H5FL_FREE(H5F_shared_t, f->shared);
        }
        f = H5FL_FREE(H5F_t, f);
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5F_fake_free() */
