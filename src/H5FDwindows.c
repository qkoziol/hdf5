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

#include "H5FDmodule.h" /* This source code file is part of the H5FD module */

#include "H5private.h" /* Generic Functions        */

#ifdef H5_HAVE_WINDOWS

#include "H5Eprivate.h"       /* Error handling           */
#include "H5FDpkg.h"          /* File drivers             */
#include "H5FDsec2_private.h" /* sec2 VFD driver */
#include "H5FDwindows.h"      /* Windows file driver      */
#include "H5Pprivate.h"       /* Property lists           */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_windows
 *
 * Purpose: Modify the file access property list to use the H5FD_WINDOWS
 *          driver defined in this source file.  There are no driver
 *          specific properties.
 *
 * NOTE: The Windows VFD was merely a merge of the SEC2 and STDIO drivers
 *       so it has been retired.  Selecting the Windows VFD will actually
 *       set the SEC2 VFD (though for backwards compatibility, we'll keep
 *       the H5FD_WINDOWS symbol).
 *
 *
 * Return:  Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_windows(hid_t fapl_id)
{
    H5P_genplist_t *fapl = NULL; /* Property list pointer */
    herr_t          ret_value;

    FUNC_ENTER_API(FAIL)

    if (NULL == (fapl = H5P_acquire(fapl_id, H5P_TYPE_FILE_ACCESS, H5P_LOCK_EXCLUSIVE, false)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list");

    ret_value = H5P_set_driver(fapl, H5FD_WINDOWS_DRIVER, NULL, NULL);

done:
    /* Release resources */
    if (fapl && H5P_release(fapl, H5P_LOCK_EXCLUSIVE) < 0)
        HDONE_ERROR(H5E_VFL, H5E_CANTUNLOCK, FAIL, "unable to unlock property list");

    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_windows() */

#endif /* H5_HAVE_WINDOWS */
