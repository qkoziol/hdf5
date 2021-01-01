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

/*
 * Programmer:  Quincey Koziol <koziol@lbl.gov>
 *              Friday, April 3, 2020
 *
 * Purpose: Wrappers for POSIX routines used by other VFDs.
 *
 */

#define H5FD_FRIEND     /*prevent warning from including H5FDpkg   */

#include "H5private.h"      /* Generic Functions        */
#include "H5Eprivate.h"     /* Error handling           */
#include "H5Fprivate.h"     /* File access              */
#include "H5FDpkg.h"        /* File drivers             */
#include "H5FDposix_common.h" /* Common POSIX file drivers */
#include "H5Iprivate.h"     /* IDs                      */

/*
 * Types and max sizes for POSIX I/O.
 * OS X (Darwin) is odd since the max I/O size does not match the types.
 */
#if defined(H5_HAVE_WIN32_API)
#   define h5_posix_io_t                unsigned int
#   define h5_posix_io_ret_t            int
#   define H5_POSIX_MAX_IO_BYTES        INT_MAX
#elif defined(H5_HAVE_DARWIN)
#   define h5_posix_io_t                size_t
#   define h5_posix_io_ret_t            ssize_t
#   define H5_POSIX_MAX_IO_BYTES        INT_MAX
#else
#   define h5_posix_io_t                size_t
#   define h5_posix_io_ret_t            ssize_t
#   define H5_POSIX_MAX_IO_BYTES        SSIZET_MAX
#endif

/* Prototypes */
#ifndef H5_HAVE_PREADWRITE
static herr_t H5FD__posix_common_seek(H5FD_posix_common_t *file, haddr_t addr, H5FD_posix_rw_info_t *rw_info);
#endif /* H5_HAVE_PREADWRITE */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_open
 *
 * Purpose:     Create and/or opens a file as an HDF5 file.
 *
 * Return:      Success:    A pointer to a new file data structure. The
 *                          public fields will be initialized by the
 *                          caller, which is always H5FD_open().
 *              Failure:    NULL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_open(const char *name, unsigned flags, haddr_t maxaddr,
    hid_t fapl_id, H5FD_posix_common_t *file, double *open_time, double *stat_time)
{
    H5_timer_t op_timer;        /* Timer for operation      */
    int fd = -1;                /* File descriptor          */
    int o_flags;                /* Flags for open() call    */
    h5_stat_t sb;               /* Stat buffer */
    herr_t ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    HDassert(file);
    HDcompile_assert(sizeof(HDoff_t) >= sizeof(size_t));

    /* Check arguments */
    if(!name || !*name)
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "invalid file name")
    if(0 == maxaddr || HADDR_UNDEF == maxaddr)
        HGOTO_ERROR(H5E_VFL, H5E_BADRANGE, FAIL, "bogus maxaddr")
    if(H5FD_POSIX_ADDR_OVERFLOW(maxaddr))
        HGOTO_ERROR(H5E_VFL, H5E_OVERFLOW, FAIL, "maxaddr too large")

    /* Build the open flags */
    o_flags = (H5F_ACC_RDWR & flags) ? O_RDWR : O_RDONLY;
    if(H5F_ACC_TRUNC & flags)
        o_flags |= O_TRUNC;
    if(H5F_ACC_CREAT & flags)
        o_flags |= O_CREAT;
    if(H5F_ACC_EXCL & flags)
        o_flags |= O_EXCL;
#ifdef H5_HAVE_DIRECT
    if(H5F_ACC_DIRECT & flags)
        o_flags |= O_DIRECT; /* Flag for Direct I/O */
#endif /* H5_HAVE_DIRECT */

    /* Start timer, if requested */
    if(open_time) {
        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Open the file */
    if((fd = HDopen(name, o_flags, H5_POSIX_CREATE_MODE_RW)) < 0) {
        int myerrno = errno;
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to open file: name = '%s', errno = %d, error message = '%s', flags = %x, o_flags = %x", name, myerrno, HDstrerror(myerrno), flags, (unsigned)o_flags);
    } /* end if */

    /* Stop timer, if requested */
    if(open_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *open_time = times.elapsed;
    } /* end if */

    /* Save the file handle */
    file->fd = fd;
#ifdef H5_HAVE_WIN32_API
    file->hFile = (HANDLE)_get_osfhandle(fd);
    if(INVALID_HANDLE_VALUE == file->hFile)
        HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to get Windows file handle")
#endif /* H5_HAVE_WIN32_API */

    /* Start timer, if requested */
    if(stat_time) {
        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Stat the file, to get its length, and the device+inode */
    if(HDfstat(fd, &sb) < 0)
        HSYS_GOTO_ERROR(H5E_FILE, H5E_BADFILE, FAIL, "unable to fstat file")

    /* Stop timer, if requested */
    if(stat_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *stat_time = times.elapsed;
    } /* end if */

    H5_CHECKED_ASSIGN(file->eof, haddr_t, sb.st_size, h5_stat_size_t);
#ifdef H5_HAVE_WIN32_API
    {
        struct _BY_HANDLE_FILE_INFORMATION fileinfo;

        if(!GetFileInformationByHandle((HANDLE)file->hFile, &fileinfo))
            HGOTO_ERROR(H5E_FILE, H5E_CANTOPENFILE, FAIL, "unable to get Windows file information")
        file->nFileIndexHigh = fileinfo.nFileIndexHigh;
        file->nFileIndexLow = fileinfo.nFileIndexLow;
        file->dwVolumeSerialNumber = fileinfo.dwVolumeSerialNumber;
    }
#else /* H5_HAVE_WIN32_API */
    file->device = sb.st_dev;
    file->inode = sb.st_ino;
#endif /* H5_HAVE_WIN32_API */

#ifndef H5_HAVE_PREADWRITE
    /* Start with an undefined previous operation & position */
    file->pos = HADDR_UNDEF;
    file->op = H5FD_POSIX_OP_UNKNOWN;
#endif /* H5_HAVE_PREADWRITE */

    /* Retain a copy of the name used to open the file, for possible error reporting */
    HDstrncpy(file->filename, name, sizeof(file->filename));
    file->filename[sizeof(file->filename) - 1] = '\0';

    /* Get the 'ignore file locking' flag */
    if (H5FD__get_ignore_disabled_file_locks(fapl_id, &file->ignore_disabled_file_locks) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "can't retrieve ignore disabled file locks flag")

done:
    if(ret_value < 0)
        if(fd >= 0)
            HDclose(fd);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_open() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_close
 *
 * Purpose:     Closes an HDF5 file.
 *
 * Return:      Success:    SUCCEED
 *              Failure:    FAIL, file not closed.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_close(H5FD_posix_common_t *file, double *close_time)
{
    H5_timer_t op_timer;        /* Timer for operation      */
    herr_t ret_value = SUCCEED;         /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* Start timer, if requested */
    if(close_time) {
        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Close the underlying file */
    if(HDclose(file->fd) < 0)
        HSYS_GOTO_ERROR(H5E_IO, H5E_CANTCLOSEFILE, FAIL, "unable to close file")

    /* Stop timer, if requested */
    if(close_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *close_time = times.elapsed;
    } /* end if */

    /* Reset the file descriptor */
    file->fd = (-1);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_close() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_cmp
 *
 * Purpose:     Compares two files belonging to this driver using an
 *              arbitrary (but consistent) ordering.
 *
 * Return:      Success:    A value like strcmp()
 *              Failure:    never fails (arguments were checked by the
 *                          caller).
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
int
H5FD__posix_common_cmp(const H5FD_posix_common_t *f1, const H5FD_posix_common_t *f2)
{
    int ret_value = 0;

    FUNC_ENTER_PACKAGE_NOERR

#ifdef H5_HAVE_WIN32_API
    if(f1->dwVolumeSerialNumber < f2->dwVolumeSerialNumber) HGOTO_DONE(-1)
    if(f1->dwVolumeSerialNumber > f2->dwVolumeSerialNumber) HGOTO_DONE(1)

    if(f1->nFileIndexHigh < f2->nFileIndexHigh) HGOTO_DONE(-1)
    if(f1->nFileIndexHigh > f2->nFileIndexHigh) HGOTO_DONE(1)

    if(f1->nFileIndexLow < f2->nFileIndexLow) HGOTO_DONE(-1)
    if(f1->nFileIndexLow > f2->nFileIndexLow) HGOTO_DONE(1)
#else /* H5_HAVE_WIN32_API */
#ifdef H5_DEV_T_IS_SCALAR
    if(f1->device < f2->device) HGOTO_DONE(-1)
    if(f1->device > f2->device) HGOTO_DONE(1)
#else /* H5_DEV_T_IS_SCALAR */
    /* If dev_t isn't a scalar value on this system, just use memcmp to
     * determine if the values are the same or not.  The actual return value
     * shouldn't really matter...
     */
    if(HDmemcmp(&(f1->device),&(f2->device),sizeof(dev_t)) < 0) HGOTO_DONE(-1)
    if(HDmemcmp(&(f1->device),&(f2->device),sizeof(dev_t)) > 0) HGOTO_DONE(1)
#endif /* H5_DEV_T_IS_SCALAR */
    if(f1->inode < f2->inode) HGOTO_DONE(-1)
    if(f1->inode > f2->inode) HGOTO_DONE(1)
#endif /* H5_HAVE_WIN32_API */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_get_eoa
 *
 * Purpose:     Gets the end-of-address marker for the file. The EOA marker
 *              is the first address past the last byte allocated in the
 *              format address space.
 *
 * Return:      The end-of-address marker.
 *
 * Programmer:  Robb Matzke
 *              Monday, August  2, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_get_eoa(const H5FD_posix_common_t *file, haddr_t *eoa)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    HDassert(file);
    HDassert(eoa);

    /* Set EOA value */
    *eoa = file->eoa;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__posix_common_get_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_set_eoa
 *
 * Purpose:     Set the end-of-address marker for the file. This function is
 *              called shortly after an existing HDF5 file is opened in order
 *              to tell the driver where the end of the HDF5 data is located.
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_set_eoa(H5FD_posix_common_t *file, haddr_t addr)
{
    herr_t  ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* Check arguments */
    if(H5FD_POSIX_ADDR_OVERFLOW(addr))
        HGOTO_ERROR(H5E_VFL, H5E_OVERFLOW, FAIL, "address overflow")

    file->eoa = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_posix_common_set_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_get_eof
 *
 * Purpose:     Returns the end-of-file marker, which is the greater of
 *              either the filesystem end-of-file or the HDF5 end-of-address
 *              markers.
 *
 * Return:      End of file address, the first address past the end of the
 *              "file", either the filesystem file or the HDF5 file.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_get_eof(const H5FD_posix_common_t *file, haddr_t *eof)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    HDassert(file);
    HDassert(eof);

    /* Set EOF value */
    *eof = file->eof;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__posix_common_get_eof() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_set_eof
 *
 * Purpose:     Set the end-of-file marker for the file.
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Saturday, April 4, 2020
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_set_eof(H5FD_posix_common_t *file, haddr_t addr)
{
    herr_t  ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* Check arguments */
    if(H5FD_POSIX_ADDR_OVERFLOW(addr))
        HGOTO_ERROR(H5E_VFL, H5E_OVERFLOW, FAIL, "address overflow")

    file->eof = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_posix_common_set_eof() */

/*-------------------------------------------------------------------------
 * Function:       H5FD__posix_common_get_handle
 *
 * Purpose:        Returns the file handle of a POSIX file driver.
 *
 * Returns:        SUCCEED/FAIL
 *
 * Programmer:     Raymond Lu
 *                 Sept. 16, 2002
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_get_handle(H5FD_posix_common_t *file, void **file_handle)
{
    FUNC_ENTER_PACKAGE_NOERR

    /* Sanity check */
    HDassert(file);
    HDassert(file_handle);

    /* Set file handle */
    *file_handle = &(file->fd);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__posix_common_get_handle() */

#ifndef H5_HAVE_PREADWRITE
/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_seek
 *
 * Purpose:     Common code to perform seek, w/updates to log info.
 *
 * Returns:     SUCCEED/FAIL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, December 30, 2020
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__posix_common_seek(H5FD_posix_common_t *file, haddr_t addr, H5FD_posix_rw_info_t *rw_info)
{
    H5_timer_t op_timer;                /* Timer for operation */
    herr_t  ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file);
    HDassert(H5F_addr_defined(addr));

    /* Indicate seek was performed */
    if (rw_info) {
        rw_info->did_seek = TRUE;

        /* Save previous offsets, if requested */
        if (rw_info->old_off)
            *rw_info->old_off = file->pos;
        if (rw_info->new_off)
            *rw_info->new_off = addr;

        /* Start timer, if requested */
        if (rw_info->seek_time) {
            /* Start time pointer must be set also, when elapsed time is set */
            HDassert(rw_info->seek_start_time);

            H5_timer_init(&op_timer);
            H5_timer_start(&op_timer);
        } /* end if  */
    } /* end if  */

    if (HDlseek(file->fd, addr, SEEK_SET) < 0)
        HSYS_GOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to seek to proper position")

    /* Stop timer */
    if (rw_info && rw_info->seek_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *rw_info->seek_start_time = op_timer.initial.elapsed;
        *rw_info->seek_time = times.elapsed;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_seek() */
#endif /* H5_HAVE_PREADWRITE */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_read
 *
 * Purpose:     Reads SIZE bytes of data from FILE beginning at address ADDR
 *              into buffer BUF according to data transfer properties in
 *              DXPL_ID.
 *
 * Return:      Success:    SUCCEED. Result is stored in caller-supplied
 *                          buffer BUF.
 *              Failure:    FAIL, Contents of buffer BUF are undefined.
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_read(H5FD_posix_common_t *file, haddr_t addr, size_t size,
    void *buf, H5FD_posix_rw_info_t *rw_info)
{
    HDoff_t offset = (HDoff_t)addr;     /* Current offset in the file */
    H5_timer_t op_timer;                /* Timer for operation */
    herr_t  ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity checks */
    HDassert(file);
    HDassert(buf);

    /* Check for overflow conditions */
    if(!H5F_addr_defined(addr))
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "addr undefined, addr = %llu", (unsigned long long)addr)
    if(H5FD_POSIX_REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_VFL, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu", (unsigned long long)addr)

#ifndef H5_HAVE_PREADWRITE
    /* Seek to the correct location (if we don't have pread) */
    if(addr != file->pos || H5FD_POSIX_OP_READ != file->op)
        if (H5FD__posix_common_seek(file, addr, rw_info) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_SEEKERROR, FAIL, "unable to seek to proper position")
#endif /* H5_HAVE_PREADWRITE */

    /* Start read timer, if requested */
    if (rw_info && rw_info->op_time) {
        /* Start time pointer must be set also, when elapsed time is set */
        HDassert(rw_info->op_start_time);

        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Read data, being careful of interrupted system calls, partial results,
     * and the end of the file.
     */
    while(size > 0) {
        h5_posix_io_t       bytes_in        = 0;    /* # of bytes to read       */
        h5_posix_io_ret_t   bytes_read      = -1;   /* # of bytes actually read */

        /* Trying to read more bytes than the return type can handle is
         * undefined behavior in POSIX.
         */
        if(size > H5_POSIX_MAX_IO_BYTES)
            bytes_in = H5_POSIX_MAX_IO_BYTES;
        else
            bytes_in = (h5_posix_io_t)size;

        do {
#ifdef H5_HAVE_PREADWRITE
            bytes_read = HDpread(file->fd, buf, bytes_in, offset);
            if(bytes_read > 0)
                offset += bytes_read;
#else
            bytes_read = HDread(file->fd, buf, bytes_in);
#endif /* H5_HAVE_PREADWRITE */
        } while(-1 == bytes_read && EINTR == errno);

        if(-1 == bytes_read) { /* error */
            int myerrno = errno;
            time_t mytime = HDtime(NULL);

#ifndef H5_HAVE_PREADWRITE
            offset = HDlseek(file->fd, (HDoff_t)0, SEEK_CUR);
#endif /* H5_HAVE_PREADWRITE */

            HGOTO_ERROR(H5E_IO, H5E_READERROR, FAIL,
                        "file read failed: time = %s, filename = '%s', file descriptor = %d, errno = %d, "
                        "error message = '%s', buf = %p, total read size = %llu, bytes this sub-read = %llu, "
                        "bytes actually read = %llu, offset = %llu",
                        HDctime(&mytime), file->filename, file->fd, myerrno, HDstrerror(myerrno), buf,
                        (unsigned long long)size, (unsigned long long)bytes_in,
                        (unsigned long long)bytes_read, (unsigned long long)offset);
        } /* end if */

        if(0 == bytes_read) {
            /* end of file but not end of format address space */
            HDmemset(buf, 0, size);
            break;
        } /* end if */

        HDassert(bytes_read >= 0);
        HDassert((size_t)bytes_read <= size);

        size -= (size_t)bytes_read;
        addr += (haddr_t)bytes_read;
        buf = (char *)buf + bytes_read;
    } /* end while */

    /* Stop timer */
    if (rw_info && rw_info->op_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *rw_info->op_start_time = op_timer.initial.elapsed;
        *rw_info->op_time = times.elapsed;
    } /* end if */

#ifndef H5_HAVE_PREADWRITE
    /* Update current position */
    file->pos = addr;
    file->op = H5FD_POSIX_OP_READ;
#endif /* H5_HAVE_PREADWRITE */

done:
    if(ret_value < 0) {
#ifndef H5_HAVE_PREADWRITE
        /* Reset last file I/O information */
        file->pos = HADDR_UNDEF;
        file->op = H5FD_POSIX_OP_UNKNOWN;
#endif /* H5_HAVE_PREADWRITE */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_read() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_write
 *
 * Purpose:     Writes SIZE bytes of data to FILE beginning at address ADDR
 *              from buffer BUF according to data transfer properties in
 *              DXPL_ID.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_write(H5FD_posix_common_t *file, haddr_t addr, size_t size,
    const void *buf, H5FD_posix_rw_info_t *rw_info)
{
    HDoff_t offset = (HDoff_t)addr;     /* Current offset in the file */
    H5_timer_t op_timer;                /* Timer for operation */
    herr_t  ret_value   = SUCCEED;      /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);
    HDassert(buf);

    /* Check for overflow conditions */
    if(!H5F_addr_defined(addr))
        HGOTO_ERROR(H5E_VFL, H5E_BADVALUE, FAIL, "addr undefined, addr = %llu", (unsigned long long)addr)
    if(H5FD_POSIX_REGION_OVERFLOW(addr, size))
        HGOTO_ERROR(H5E_VFL, H5E_OVERFLOW, FAIL, "addr overflow, addr = %llu, size = %llu", (unsigned long long)addr, (unsigned long long)size)

#ifndef H5_HAVE_PREADWRITE
    /* Seek to the correct location (if we don't have pwrite) */
    if(addr != file->pos || H5FD_POSIX_OP_WRITE != file->op)
        if (H5FD__posix_common_seek(file, addr, rw_info) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_SEEKERROR, FAIL, "unable to seek to proper position")
#endif /* H5_HAVE_PREADWRITE */

    /* Start write timer, if requested */
    if (rw_info && rw_info->op_time) {
        /* Start time pointer must be set also, when elapsed time is set */
        HDassert(rw_info->op_start_time);

        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Write the data, being careful of interrupted system calls and partial
     * results
     */
    while(size > 0) {
        h5_posix_io_t       bytes_in        = 0;    /* # of bytes to write  */
        h5_posix_io_ret_t   bytes_wrote     = -1;   /* # of bytes written   */

        /* Trying to write more bytes than the return type can handle is
         * undefined behavior in POSIX.
         */
        if(size > H5_POSIX_MAX_IO_BYTES)
            bytes_in = H5_POSIX_MAX_IO_BYTES;
        else
            bytes_in = (h5_posix_io_t)size;

        do {
#ifdef H5_HAVE_PREADWRITE
            bytes_wrote = HDpwrite(file->fd, buf, bytes_in, offset);
            if(bytes_wrote > 0)
                offset += bytes_wrote;
#else
            bytes_wrote = HDwrite(file->fd, buf, bytes_in);
#endif /* H5_HAVE_PREADWRITE */
        } while(-1 == bytes_wrote && EINTR == errno);

        if(-1 == bytes_wrote) { /* error */
            int myerrno = errno;
            time_t mytime = HDtime(NULL);

#ifndef H5_HAVE_PREADWRITE
            offset = HDlseek(file->fd, (HDoff_t)0, SEEK_CUR);
#endif /* H5_HAVE_PREADWRITE */

            HGOTO_ERROR(H5E_IO, H5E_WRITEERROR, FAIL,
                        "file write failed: time = %s, filename = '%s', file descriptor = %d, errno = %d, "
                        "error message = '%s', buf = %p, total write size = %llu, bytes this sub-write = %llu, "
                        "bytes actually written = %llu, offset = %llu",
                        HDctime(&mytime), file->filename, file->fd, myerrno, HDstrerror(myerrno), buf,
                        (unsigned long long)size, (unsigned long long)bytes_in,
                        (unsigned long long)bytes_wrote, (unsigned long long)offset);
        } /* end if */

        HDassert(bytes_wrote > 0);
        HDassert((size_t)bytes_wrote <= size);

        size -= (size_t)bytes_wrote;
        addr += (haddr_t)bytes_wrote;
        buf = (const char *)buf + bytes_wrote;
    } /* end while */

    /* Stop timer */
    if (rw_info && rw_info->op_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *rw_info->op_start_time = op_timer.initial.elapsed;
        *rw_info->op_time = times.elapsed;
    } /* end if */

#ifndef H5_HAVE_PREADWRITE
    /* Update current position */
    file->pos = addr;
    file->op = H5FD_POSIX_OP_WRITE;
#endif /* H5_HAVE_PREADWRITE */

    /* Update eof */
    if(addr > file->eof)
        file->eof = addr;

done:
    if(ret_value < 0) {
#ifndef H5_HAVE_PREADWRITE
        /* Reset last file I/O information */
        file->pos = HADDR_UNDEF;
        file->op = H5FD_POSIX_OP_UNKNOWN;
#endif /* H5_HAVE_PREADWRITE */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__common_posix_write() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_truncate
 *
 * Purpose:     Makes sure that the true file size is the same (or larger)
 *              than the end-of-address.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Wednesday, August  4, 1999
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_truncate(H5FD_posix_common_t *file, haddr_t new_eof,
    H5FD_posix_trunc_info_t *trunc_info)
{
    herr_t ret_value = SUCCEED;                 /* Return value */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* If the new_eof is HADDR_UNDEF, use the current EOA */
    if(!H5F_addr_defined(new_eof))
        new_eof = file->eoa;

    /* Extend the file to make sure it's large enough */
    if(!H5F_addr_eq(new_eof, file->eof)) {
#ifdef H5_HAVE_WIN32_API
        LARGE_INTEGER   li;         /* 64-bit (union) integer for SetFilePointer() call */
        DWORD           dwPtrLow;   /* Low-order pointer bits from SetFilePointer()
                                     * Only used as an error code here.
                                     */
        DWORD           dwError;    /* DWORD error code from GetLastError() */
        BOOL            bError;     /* Boolean error flag */

#endif /* H5_HAVE_WIN32_API */
        H5_timer_t op_timer;                /* Timer for operation */

        /* Check for truncate tracking info */
        if (trunc_info) {
            /* Indicate truncate was performed */
            trunc_info->did_trunc = TRUE;

            /* Start read timer, if requested */
            if(trunc_info->elap_time) {
                /* Start time pointer must be set also, when elapsed time is set */
                HDassert(trunc_info->start_time);

                H5_timer_init(&op_timer);
                H5_timer_start(&op_timer);
            } /* end if  */
        } /* end if  */

#ifdef H5_HAVE_WIN32_API
        /* Windows uses this odd QuadPart union for 32/64-bit portability */
        li.QuadPart = (__int64)new_eof;

        /* Extend the file to make sure it's large enough.
         *
         * Since INVALID_SET_FILE_POINTER can technically be a valid return value
         * from SetFilePointer(), we also need to check GetLastError().
         */
        dwPtrLow = SetFilePointer(file->hFile, li.LowPart, &li.HighPart, FILE_BEGIN);
        if(INVALID_SET_FILE_POINTER == dwPtrLow) {
            dwError = GetLastError();
            if(dwError != NO_ERROR )
                HGOTO_ERROR(H5E_FILE, H5E_FILEOPEN, FAIL, "unable to set file pointer")
        } /* end if */

        bError = SetEndOfFile(file->hFile);
        if(0 == bError)
            HGOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to extend file properly")
#else /* H5_HAVE_WIN32_API */
        if(-1 == HDftruncate(file->fd, (HDoff_t)new_eof))
            HSYS_GOTO_ERROR(H5E_IO, H5E_SEEKERROR, FAIL, "unable to extend file properly")
#endif /* H5_HAVE_WIN32_API */

        /* Stop timer */
        if (trunc_info && trunc_info->elap_time) {
            H5_timevals_t times;

            /* Stop timer */
            H5_timer_stop(&op_timer);

            /* Calculate the elapsed time */
            H5_timer_get_times(&op_timer, &times);
            *trunc_info->start_time = op_timer.initial.elapsed;
            *trunc_info->elap_time = times.elapsed;
        } /* end if */

        /* Update the eof value */
        file->eof = new_eof;

#ifndef H5_HAVE_PREADWRITE
        /* Reset last file I/O information */
        file->pos = HADDR_UNDEF;
        file->op = H5FD_POSIX_OP_UNKNOWN;
#endif /* H5_HAVE_PREADWRITE */
    } /* end if */

done:
    if(ret_value < 0) {
#ifndef H5_HAVE_PREADWRITE
        /* Reset last file I/O information */
        file->pos = HADDR_UNDEF;
        file->op = H5FD_POSIX_OP_UNKNOWN;
#endif /* H5_HAVE_PREADWRITE */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_truncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__posix_common_lock
 *
 * Purpose:     To place an advisory lock on a file.
 *		The lock type to apply depends on the parameter "rw":
 *			TRUE--opens for write: an exclusive lock
 *			FALSE--opens for read: a shared lock
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_lock(const H5FD_posix_common_t *file, hbool_t rw,
    double *lock_time)
{
    H5_timer_t op_timer;        /* Timer for operation      */
    int lock_flags;             /* File locking flags       */
    herr_t ret_value = SUCCEED; /* Return value             */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* Set exclusive or shared lock based on rw status */
    lock_flags = rw ? LOCK_EX : LOCK_SH;

    /* Start timer, if requested */
    if(lock_time) {
        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Place a non-blocking lock on the file */
    if(HDflock(file->fd, lock_flags | LOCK_NB) < 0) {
        /* When errno is set to ENOSYS, the file system does not support
         * locking, so ignore it.
         */
        if(file->ignore_disabled_file_locks && ENOSYS == errno)
            errno = 0;
        else
            HSYS_GOTO_ERROR(H5E_FILE, H5E_BADFILE, FAIL, "unable to lock file")
    } /* end if */

    /* Stop timer, if requested */
    if(lock_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *lock_time = times.elapsed;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_lock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_posix_common_unlock
 *
 * Purpose:     To remove the existing lock on the file
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5FD__posix_common_unlock(const H5FD_posix_common_t *file, double *unlock_time)
{
    H5_timer_t op_timer;        /* Timer for operation      */
    herr_t ret_value = SUCCEED;         /* Return value             */

    FUNC_ENTER_PACKAGE

    /* Sanity check */
    HDassert(file);

    /* Start timer, if requested */
    if(unlock_time) {
        H5_timer_init(&op_timer);
        H5_timer_start(&op_timer);
    } /* end if  */

    /* Unlock the file */
    if(HDflock(file->fd, LOCK_UN) < 0) {
        /* When errno is set to ENOSYS, the file system does not support
         * locking, so ignore it.
         */
        if(file->ignore_disabled_file_locks && ENOSYS == errno)
            errno = 0;
        else
            HSYS_GOTO_ERROR(H5E_FILE, H5E_BADFILE, FAIL, "unable to unlock file")
    } /* end if */

    /* Stop timer, if requested */
    if(unlock_time) {
        H5_timevals_t times;

        /* Stop timer */
        H5_timer_stop(&op_timer);

        /* Calculate the elapsed time */
        H5_timer_get_times(&op_timer, &times);
        *unlock_time = times.elapsed;
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__posix_common_unlock() */

