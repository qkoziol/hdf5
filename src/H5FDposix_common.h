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
 *              Saturday, April  4, 2020
 *
 * Purpose:	The private header file for drivers that use POSIX I/O.
 */

#ifndef H5FDposix_common_H
#define H5FDposix_common_H

/* Private headers needed by this file */
#include "H5private.h"          /* Generic Functions			    */
#include "H5FDprivate.h"        /* File drivers                             */

/* Maximum offset in a POSIX file */
#define H5_POSIX_MAXADDR        (((haddr_t)1 << (8 * sizeof(HDoff_t) - 1)) - 1)

/*
 * These macros check for overflow of various quantities.  These macros
 * assume that HDoff_t is signed and haddr_t and size_t are unsigned.
 *
 * H5FD_POSIX_ADDR_OVERFLOW:   Checks whether a file address of type `haddr_t'
 *                  is too large to be represented by the second argument
 *                  of the file seek function.
 *
 * H5FD_POSIX_SIZE_OVERFLOW:   Checks whether a buffer size of type `hsize_t'
 *                  is too large to be represented by the `size_t' type.
 *
 * H5FD_POSIX_REGION_OVERFLOW: Checks whether an address and size pair describe
 *                  data which can be addressed entirely by the second
 *                  argument of the file seek function.
 */
#define H5FD_POSIX_ADDR_OVERFLOW(A)    (HADDR_UNDEF == (A) || ((A) & ~(haddr_t)H5_POSIX_MAXADDR))
#define H5FD_POSIX_SIZE_OVERFLOW(Z)    ((Z) & ~(hsize_t)H5_POSIX_MAXADDR)
#define H5FD_POSIX_REGION_OVERFLOW(A, Z) (H5FD_POSIX_ADDR_OVERFLOW(A) || \
                                H5FD_POSIX_SIZE_OVERFLOW(Z) || \
                               (HADDR_UNDEF == (A) + (Z)) || \
                               ((HDoff_t)((A) + (Z)) < (HDoff_t)(A)))

#ifndef H5_HAVE_PREADWRITE
/* File operations */
typedef enum H5FD_posix_op_t {
    H5FD_POSIX_OP_UNKNOWN = 0,      /* Unknown last file operation */
    H5FD_POSIX_OP_READ = 1,         /* Last file I/O operation was a read */
    H5FD_POSIX_OP_WRITE = 2         /* Last file I/O operation was a write */
} H5FD_posix_op_t;
#endif /* H5_HAVE_PREADWRITE */

/* The common description for a POSIX-based file.
 *
 * The 'eoa' and 'eof' determine the amount of HDF5 address space in use and
 * the high-water mark of the file (the current size of the underlying
 * filesystem file).
 *
 * The 'pos' value is used to eliminate file position updates when they would
 * be a no-op. Unfortunately we've found systems that use separate file position
 * indicators for reading and writing so the lseek can only be eliminated if
 * the current operation is the same as the previous operation.  When opening
 * a file the 'eof' will be set to the current file size, `eoa' will be set
 * to zero, 'pos' will be set to H5F_ADDR_UNDEF (as it is when an error
 * occurs), and 'op' will be set to H5F_OP_UNKNOWN.
 */
typedef struct H5FD_posix_common_t {
    int             fd;     /* the filesystem file descriptor   */
    char            filename[H5FD_MAX_FILENAME_LEN];    /* Copy of file name from open operation */
    haddr_t         eoa;    /* end of allocated region          */
    haddr_t         eof;    /* end of file; current file size   */
#ifndef H5_HAVE_PREADWRITE
    haddr_t         pos;    /* current file I/O position        */
    H5FD_posix_op_t op;     /* last operation                   */
#endif /* H5_HAVE_PREADWRITE */
#ifndef H5_HAVE_WIN32_API
    /* On most systems the combination of device and i-node number uniquely
     * identify a file.  Note that Cygwin, MinGW and other Windows POSIX
     * environments have the stat function (which fakes inodes)
     * and will use the 'device + inodes' scheme as opposed to the
     * Windows code further below.
     */
    dev_t           device;     /* file device number   */
    ino_t           inode;      /* file i-node number   */
#else
    /* Files in windows are uniquely identified by the volume serial
     * number and the file index (both low and high parts).
     *
     * There are caveats where these numbers can change, especially
     * on FAT file systems.  On NTFS, however, a file should keep
     * those numbers the same until renamed or deleted (though you
     * can use ReplaceFile() on NTFS to keep the numbers the same
     * while renaming).
     *
     * See the MSDN "BY_HANDLE_FILE_INFORMATION Structure" entry for
     * more information.
     *
     * http://msdn.microsoft.com/en-us/library/aa363788(v=VS.85).aspx
     */
    DWORD           nFileIndexLow;
    DWORD           nFileIndexHigh;
    DWORD           dwVolumeSerialNumber;

    HANDLE          hFile;      /* Native windows file handle */
#endif  /* H5_HAVE_WIN32_API */
    hbool_t         ignore_disabled_file_locks;
} H5FD_posix_common_t;

/* Common logging info for read & write operations */
typedef struct H5FD_posix_rw_info_t {
#ifndef H5_HAVE_PREADWRITE
    hbool_t  did_seek;          /* Whether a seek was performed */
    double  *seek_start_time;   /* Start time for seek operation */
    double  *seek_time,         /* Elapsed time for seek operation */
    haddr_t *old_off;           /* Old offset, before seek */
    haddr_t *new_off;           /* New offset, after seek */
#endif /* H5_HAVE_PREADWRITE */
    double  *op_start_time;     /* Operation (read/write) start time */
    double  *op_time;           /* Elapsed time for operation */
} H5FD_posix_rw_info_t;

/* Logging info for truncate operation */
typedef struct H5FD_posix_trunc_info_t {
    hbool_t  did_trunc;         /* Whether a truncate was performed */
    double  *start_time;        /* Start time for operation */
    double  *elap_time;         /* Elapsed time for operation */
} H5FD_posix_trunc_info_t;

/* Common POSIX methods */
H5_DLL herr_t H5FD__posix_common_open(const char *name, unsigned flags,
    haddr_t maxaddr, hid_t fapl_id, H5FD_posix_common_t *file,
    double *open_time, double *stat_time);
H5_DLL herr_t H5FD__posix_common_close(H5FD_posix_common_t *file,
    double *close_time);
H5_DLL int H5FD__posix_common_cmp(const H5FD_posix_common_t *f1,
    const H5FD_posix_common_t *f2);
H5_DLL herr_t H5FD__posix_common_get_eoa(const H5FD_posix_common_t *file,
    haddr_t *eoa);
H5_DLL herr_t H5FD__posix_common_set_eoa(H5FD_posix_common_t *file, haddr_t addr);
H5_DLL herr_t H5FD__posix_common_get_eof(const H5FD_posix_common_t *file, haddr_t *eof);
H5_DLL herr_t H5FD__posix_common_set_eof(H5FD_posix_common_t *file, haddr_t addr);
H5_DLL herr_t H5FD__posix_common_get_handle(H5FD_posix_common_t *file, void **file_handle);
H5_DLL herr_t H5FD__posix_common_read(H5FD_posix_common_t *file, haddr_t addr,
    size_t size, void *buf, H5FD_posix_rw_info_t *rw_info);
H5_DLL herr_t H5FD__posix_common_write(H5FD_posix_common_t *file, haddr_t addr,
    size_t size, const void *buf, H5FD_posix_rw_info_t *rw_info);
H5_DLL herr_t H5FD__posix_common_truncate(H5FD_posix_common_t *file,
    haddr_t new_eof, H5FD_posix_trunc_info_t *trunc_info);
H5_DLL herr_t H5FD__posix_common_lock(const H5FD_posix_common_t *file, hbool_t rw,
    double *lock_time);
H5_DLL herr_t H5FD__posix_common_unlock(const H5FD_posix_common_t *file,
    double *unlock_time);

#endif /* H5FDposix_common_H */

