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
 * Purpose:	Common routines for all MPI-based VFL drivers.
 */

#include "H5FDmodule.h" /* This source code file is part of the H5FD module */

#include "H5private.h" /* Generic Functions			*/

#ifdef H5_HAVE_PARALLEL

#include "H5Eprivate.h" /* Error handling		  	*/
#include "H5FDmpi.h"    /* Common MPI file driver		*/
#include "H5FDpkg.h"    /* File drivers                        */

/*-------------------------------------------------------------------------
 * Function:	H5FD_mpi_get_rank
 *
 * Purpose:	Retrieves the rank of an MPI process.
 *
 * Return:	Success:	The rank (non-negative)
 *
 *		Failure:	Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5FD_mpi_get_rank(H5FD_int_t *fh)
{
    H5FD_t *file;
    const H5FD_class_t *cls;
    uint64_t            flags     = H5FD_CTL_FAIL_IF_UNKNOWN_FLAG | H5FD_CTL_ROUTE_TO_TERMINAL_VFD_FLAG;
    int                 rank      = -1;
    void               *rank_ptr  = (void *)(&rank);
    int                 ret_value = -1;

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    cls = (const H5FD_class_t *)(fh->driver->cls);
    assert(cls);
    assert(cls->ctl); /* All MPI drivers must implement this */

    /* Get the file pointer */
    file = fh->file;
    assert(file);

   /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(-1)
        {
            /* Dispatch to driver */
            ret_value = (cls->ctl)(file, H5FD_CTL_GET_MPI_RANK_OPCODE, flags, NULL, &rank_ptr);
        }
    H5_AFTER_USER_CB(-1)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "driver get_rank request failed");
    assert(rank >= 0);

    ret_value = rank;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_mpi_get_rank() */

/*-------------------------------------------------------------------------
 * Function:	H5FD_mpi_get_size
 *
 * Purpose:	Retrieves the size of the communicator used for the file
 *
 * Return:	Success:	The communicator size (non-negative)
 *
 *		Failure:	Negative
 *
 *-------------------------------------------------------------------------
 */
int
H5FD_mpi_get_size(H5FD_int_t *fh)
{
    H5FD_t *file;
    const H5FD_class_t *cls;
    uint64_t            flags     = H5FD_CTL_FAIL_IF_UNKNOWN_FLAG | H5FD_CTL_ROUTE_TO_TERMINAL_VFD_FLAG;
    int                 size      = 0;
    void               *size_ptr  = (void *)(&size);
    int                 ret_value = 0;

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    cls = (const H5FD_class_t *)(fh->driver->cls);
    assert(cls);
    assert(cls->ctl); /* All MPI drivers must implement this */

    /* Get the file pointer */
    file = fh->file;
    assert(file);

   /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(-1)
        {
            /* Dispatch to driver */
            ret_value = (cls->ctl)(file, H5FD_CTL_GET_MPI_SIZE_OPCODE, flags, NULL, &size_ptr);
        }
    H5_AFTER_USER_CB(-1)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "driver get_size request failed");

    if (size < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "driver get_size request returned bad value");

    ret_value = size;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_mpi_get_size() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_mpi_get_comm
 *
 * Purpose:	    Retrieves the file's MPI_Comm communicator object
 *
 * Return:      Success:    The communicator object
 *              Failure:    MPI_COMM_NULL
 *
 *-------------------------------------------------------------------------
 */
MPI_Comm
H5FD_mpi_get_comm(H5FD_int_t *fh)
{
    H5FD_t *file;
    const H5FD_class_t *cls;
    uint64_t            flags     = H5FD_CTL_FAIL_IF_UNKNOWN_FLAG | H5FD_CTL_ROUTE_TO_TERMINAL_VFD_FLAG;
    MPI_Comm            comm      = MPI_COMM_NULL;
    void               *comm_ptr  = (void *)(&comm);
    MPI_Comm            ret_value = MPI_COMM_NULL;

    FUNC_ENTER_NOAPI(MPI_COMM_NULL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    cls = (const H5FD_class_t *)(fh->driver->cls);
    assert(cls);
    assert(cls->ctl); /* All MPI drivers must implement this */

    /* Get the file pointer */
    file = fh->file;
    assert(file);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(MPI_COMM_NULL)
        {
            /* Dispatch to driver */
            ret_value = (cls->ctl)(file, H5FD_CTL_GET_MPI_COMMUNICATOR_OPCODE, flags, NULL, &comm_ptr);
        }
    H5_AFTER_USER_CB(MPI_COMM_NULL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, MPI_COMM_NULL, "driver get_comm request failed");

    if (comm == MPI_COMM_NULL)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, MPI_COMM_NULL, "driver get_comm request failed -- bad comm");

    ret_value = comm;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_mpi_get_comm() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_mpi_get_info
 *
 * Purpose:     Retrieves the file's MPI_Info info object
 *
 * Return:      Success:    The info object
 *              Failure:    MPI_INFO_NULL
 *
 *-------------------------------------------------------------------------
 */
MPI_Info
H5FD_mpi_get_info(H5FD_int_t *fh)
{
    H5FD_t *file;
    const H5FD_class_t *cls;
    uint64_t            flags     = H5FD_CTL_FAIL_IF_UNKNOWN_FLAG | H5FD_CTL_ROUTE_TO_TERMINAL_VFD_FLAG;
    MPI_Info            info      = MPI_INFO_NULL;
    void               *info_ptr  = (void *)(&info);
    MPI_Info            ret_value = MPI_INFO_NULL;

    FUNC_ENTER_NOAPI(MPI_INFO_NULL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    cls = (const H5FD_class_t *)(fh->driver->cls);
    assert(cls);
    assert(cls->ctl); /* All MPI drivers must implement this */

    /* Get the file pointer */
    file = fh->file;
    assert(file);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(MPI_INFO_NULL)
        {
            /* Dispatch to driver */
            ret_value = (cls->ctl)(file, H5FD_CTL_GET_MPI_INFO_OPCODE, flags, NULL, &info_ptr);
        }
    H5_AFTER_USER_CB(MPI_INFO_NULL)
    if (ret_value < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, MPI_INFO_NULL, "driver get_info request failed");

    if (info == MPI_INFO_NULL)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, MPI_INFO_NULL, "driver get_info request failed -- bad info object");

    ret_value = info;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_mpi_get_info() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_mpi_MPIOff_to_haddr
 *
 * Purpose:     Convert an MPI_Offset value to haddr_t.
 *
 * Return:      Success:	The haddr_t equivalent of the MPI_OFF
 *				argument.
 *
 *              Failure:	HADDR_UNDEF
 *
 *-------------------------------------------------------------------------
 */
haddr_t
H5FD_mpi_MPIOff_to_haddr(MPI_Offset mpi_off)
{
    haddr_t ret_value = HADDR_UNDEF;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    if (mpi_off != (MPI_Offset)(haddr_t)mpi_off)
        ret_value = HADDR_UNDEF;
    else
        ret_value = (haddr_t)mpi_off;

    FUNC_LEAVE_NOAPI(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:    H5FD_mpi_haddr_to_MPIOff
 *
 * Purpose:     Convert an haddr_t value to MPI_Offset.
 *
 * Return:      Success:	Non-negative, the MPI_OFF argument contains
 *				the converted value.
 *
 * 		Failure:	Negative, MPI_OFF is undefined.
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_mpi_haddr_to_MPIOff(haddr_t addr, MPI_Offset *mpi_off /*out*/)
{
    herr_t ret_value = FAIL;

    FUNC_ENTER_NOAPI_NOINIT_NOERR

    assert(mpi_off);

    /* Convert the HDF5 address into an MPI offset */
    *mpi_off = (MPI_Offset)addr;

    if (addr != (haddr_t)((MPI_Offset)addr))
        ret_value = FAIL;
    else
        ret_value = SUCCEED;

    FUNC_LEAVE_NOAPI(ret_value)
}

/*-------------------------------------------------------------------------
 * Function:	H5FD_mpi_get_file_sync_required
 *
 * Purpose:	Retrieves the mpi_file_sync_required used for the file
 *
 * Return:	Success:	Non-negative
 *
 *              Failure:	Negative
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD_mpi_get_file_sync_required(H5FD_int_t *fh, bool *file_sync_required)
{
    H5FD_t *file;
    const H5FD_class_t *cls;
    uint64_t            flags                  = H5FD_CTL_ROUTE_TO_TERMINAL_VFD_FLAG;
    void               *file_sync_required_ptr = (void *)(&file_sync_required);
    herr_t              ret_value              = SUCCEED;

    FUNC_ENTER_NOAPI(FAIL)

    /* Sanity checks */
    assert(fh);
    assert(fh->driver);
    assert(fh->driver->cls);
    cls = (const H5FD_class_t *)(fh->driver->cls);
    assert(cls);
    assert(cls->ctl); /* All MPI drivers must implement this */

    /* Get the file pointer */
    file = fh->file;
    assert(file);

    /* Prepare & restore library for user callback */
    H5_BEFORE_USER_CB(FAIL)
        {
            /* Dispatch to driver */
            ret_value = (cls->ctl)(file, H5FD_CTL_GET_MPI_FILE_SYNC_OPCODE, flags, NULL, file_sync_required_ptr);
        }
    H5_AFTER_USER_CB(FAIL)
    if (ret_value < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "driver get_mpi_file_sync_required request failed");

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_mpi_get_file_sync_required() */

#endif /* H5_HAVE_PARALLEL */
