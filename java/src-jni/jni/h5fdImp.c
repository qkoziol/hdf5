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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#include <jni.h>
#include <stdlib.h>
#include "hdf5.h"
#include "h5jni.h"
#include "h5fdImp.h"
#include "H5FDdevelop.h"

/*
 * Class:     hdf_hdf5lib_H5
 * Method:    _H5FDcmp_driver_cls
 * Signature: (JJ)Z
 */
JNIEXPORT jboolean JNICALL
Java_hdf_hdf5lib_H5__1H5FDcmp_1driver_1cls(JNIEnv *env, jclass clss, jlong drvr_id1, jlong drvr_id2)
{
    int      cmp_value = 0;
    jboolean bval      = JNI_FALSE;
    herr_t   retValue  = FAIL;

    UNUSED(clss);

    if ((retValue = H5FDcmp_driver_cls(&cmp_value, (hid_t)drvr_id1, (hid_t)drvr_id2)) < 0)
        H5_LIBRARY_ERROR(ENVONLY);

    bval = (cmp_value == 0) ? JNI_TRUE : JNI_FALSE;

done:
    return bval;
} /* end Java_hdf_hdf5lib_H5_H5FDcmp_driver_cls */

#ifdef __cplusplus
} /* end extern "C" */
#endif /* __cplusplus */
