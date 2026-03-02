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

#include <jni.h>
/* Header for class hdf_hdf5lib_H5_H5FD */

#ifndef Included_hdf_hdf5lib_H5_H5FD
#define Included_hdf_hdf5lib_H5_H5FD

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
/*
 * Class:     hdf_hdf5lib_H5
 * Method:    H5FDcmp_driver_cls
 * Signature: (JJ)Z
 */
JNIEXPORT jboolean JNICALL Java_hdf_hdf5lib_H5_H5FDcmp_1driver_1cls(JNIEnv *, jclass, jlong, jlong);

#ifdef __cplusplus
} /* end extern "C" */
#endif /* __cplusplus */

#endif /* Included_hdf_hdf5lib_H5_H5FD */
