/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * All rights reserved.                                                      *
 *                                                                           *
 * This file is part of HDF5.  The full HDF5 copyright notice, including     *
 * terms governing use, modification, and redistribution, is contained in    *
 * the COPYING file, which can be found at the root of the source code       *
 * distribution tree, or in https://support.hdfgroup.org/ftp/HDF5/releases.  *
 * If you do not have access to either file, you may request a copy from     *
 * help@hdfgroup.org.                                                        *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*-------------------------------------------------------------------------
 *
 * Created:		H5ES.c
 *			Apr  6 2020
 *			Quincey Koziol <koziol@lbl.gov>
 *
 * Purpose:		Implements an "event set" for managing asynchronous
 *                      operations.
 *
 *                      Please see the asynchronous I/O RFC document
 *                      for a full description of how they work, etc.
 *
 *-------------------------------------------------------------------------
 */

/****************/
/* Module Setup */
/****************/

#include "H5ESmodule.h"         /* This source code file is part of the H5ES module */


/***********/
/* Headers */
/***********/
#include "H5private.h"		/* Generic Functions			*/
#include "H5Eprivate.h"		/* Error handling		  	*/
#include "H5ESprivate.h"        /* Event Sets                           */
#include "H5FLprivate.h"        /* Free Lists                           */
#include "H5Iprivate.h"         /* IDs                                  */


/****************/
/* Local Macros */
/****************/


/******************/
/* Local Typedefs */
/******************/

/* Typedef for event set objects */
typedef struct H5ES_t {
    int x;
} H5ES_t;


/********************/
/* Package Typedefs */
/********************/


/********************/
/* Local Prototypes */
/********************/
static herr_t H5ES__close_cb(void *es, void **request_token);
static herr_t H5ES__close(H5ES_t *es);


/*********************/
/* Package Variables */
/*********************/

/* Package initialization variable */
hbool_t H5_PKG_INIT_VAR = FALSE;


/*****************************/
/* Library Private Variables */
/*****************************/


/*******************/
/* Local Variables */
/*******************/

/* Event Set ID class */
static const H5I_class_t H5I_EVENTSET_CLS[1] = {{
    H5I_EVENTSET,               /* ID class value */
    0,                          /* Class flags */
    0,                          /* # of reserved IDs for class */
    (H5I_free_t)H5ES__close_cb  /* Callback routine for closing objects of this class */
}};

/* Declare a static free list to manage H5ES_t structs */
H5FL_DEFINE_STATIC(H5ES_t);



/*-------------------------------------------------------------------------
 * Function:    H5ES__init_package
 *
 * Purpose:     Initializes any interface-specific data or routines.
 *
 * Return:      Non-negative on success / Negative on failure
 *
 * Programmer:	Quincey Koziol
 *	        Monday, April 6, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5ES__init_package(void)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_PACKAGE

    /* Initialize the ID group for the event set IDs */
    if(H5I_register_type(H5I_EVENTSET_CLS) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CANTINIT, FAIL, "unable to initialize interface")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5ES__init_package() */


/*-------------------------------------------------------------------------
 * Function:    H5ES_term_package
 *
 * Purpose:     Terminate this interface.
 *
 * Return:      Success:    Positive if anything is done that might
 *                          affect other interfaces; zero otherwise.
 *              Failure:    Negative
 *
 * Programmer:	Quincey Koziol
 *	        Monday, April 6, 2020
 *
 *-------------------------------------------------------------------------
 */
int
H5ES_term_package(void)
{
    int    n = 0;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if(H5_PKG_INIT_VAR) {
        /* Destroy the event set ID group */
        n += (H5I_dec_type_ref(H5I_EVENTSET) > 0);

        /* Mark closed */
        if(0 == n)
            H5_PKG_INIT_VAR = FALSE;
    } /* end if */

    FUNC_LEAVE_NOAPI(n)
} /* end H5ES_term_package() */


/*-------------------------------------------------------------------------
 * Function:    H5ES__close_cb
 *
 * Purpose:     Called when the ref count reaches zero on an event set's ID
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:	Quincey Koziol
 *	        Monday, April 6, 2020
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5ES__close_cb(void *_es, void H5_ATTR_UNUSED **rt)
{
    H5ES_t *es = (H5ES_t *)_es;         /* The event set to close */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(es);

    /* Close the event set object */
    if(H5ES__close(es) < 0)
        HGOTO_ERROR(H5E_EVENTSET, H5E_CLOSEERROR, FAIL, "unable to close event set");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5ES__close_cb() */


/*-------------------------------------------------------------------------
 * Function:    H5ES__close
 *
 * Purpose:     Destroy an event set object
 *
 * Return:      SUCCEED / FAIL
 *
 * Programmer:	Quincey Koziol
 *	        Monday, April 6, 2020
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5ES__close(H5ES_t *es)
{
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(es);

    /* Release the event set */
    es = H5FL_FREE(H5ES_t, es);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5ES__close() */
