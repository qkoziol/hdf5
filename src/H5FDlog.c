/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Copyright by The HDF Group.                                               *
 * Copyright by the Board of Trustees of the University of Illinois.         *
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
 * Programmer:  Quincey Koziol
 *              Monday, April 17, 2000
 *
 * Purpose:     The POSIX unbuffered file I/O driver, with logging added.
 */

#include "H5FDdrvr_module.h" /* This source code file is part of the H5FD driver module */

#include "H5private.h"   /* Generic Functions    */
#include "H5Eprivate.h"  /* Error handling       */
#include "H5Fprivate.h"  /* File access          */
#include "H5FDprivate.h" /* File drivers         */
#include "H5FDlog.h"     /* Logging file driver  */
#include "H5FDposix_common.h" /* Common POSIX file drivers */
#include "H5FLprivate.h" /* Free Lists           */
#include "H5Iprivate.h"  /* IDs                  */
#include "H5MMprivate.h" /* Memory management    */
#include "H5Pprivate.h"  /* Property lists       */

/* The driver identification number, initialized at runtime */
static hid_t H5FD_LOG_g = 0;

/* Driver-specific file access properties */
typedef struct H5FD_log_fapl_t {
    char *             logfile; /* Allocated log file name */
    unsigned long long flags;   /* Flags for logging behavior */
    size_t buf_size; /* Size of buffers for track flavor and number of times each byte is accessed */
} H5FD_log_fapl_t;

/* Define strings for the different file memory types
 * These are defined in the H5F_mem_t enum from H5Fpublic.h
 * Note that H5FD_MEM_NOLIST is not listed here since it has
 * a negative value.
 */
static const char *flavors[] = {
    "H5FD_MEM_DEFAULT", "H5FD_MEM_SUPER", "H5FD_MEM_BTREE", "H5FD_MEM_DRAW",
    "H5FD_MEM_GHEAP",   "H5FD_MEM_LHEAP", "H5FD_MEM_OHDR",
};

/* The description of a file belonging to this driver. The `eoa' and `eof'
 * determine the amount of hdf5 address space in use and the high-water mark
 * of the file (the current size of the underlying filesystem file). The
 * `pos' value is used to eliminate file position updates when they would be a
 * no-op. Unfortunately we've found systems that use separate file position
 * indicators for reading and writing so the lseek can only be eliminated if
 * the current operation is the same as the previous operation.  When opening
 * a file the `eof' will be set to the current file size, `eoa' will be set
 * to zero, `pos' will be set to H5F_ADDR_UNDEF (as it is when an error
 * occurs), and `op' will be set to H5F_OP_UNKNOWN.
 */
typedef struct H5FD_log_t {
    H5FD_t         pub; /* public stuff, must be first      */
    H5FD_posix_common_t pos_com;    /* Common POSIX info        */

    /* Fields for tracking I/O operations */
    unsigned char *    nread;               /* Number of reads from a file location             */
    unsigned char *    nwrite;              /* Number of write to a file location               */
    unsigned char *    flavor;              /* Flavor of information written to file location   */
    unsigned long long total_read_ops;      /* Total number of read operations                  */
    unsigned long long total_write_ops;     /* Total number of write operations                 */
    unsigned long long total_seek_ops;      /* Total number of seek operations                  */
    unsigned long long total_truncate_ops;  /* Total number of truncate operations              */
    double             total_read_time;     /* Total time spent in read operations              */
    double             total_write_time;    /* Total time spent in write operations             */
    double             total_seek_time;     /* Total time spent in seek operations              */
    double             total_truncate_time; /* Total time spent in truncate operations              */
    size_t             iosize;              /* Size of I/O information buffers                  */
    FILE *             logfp;               /* Log file pointer                                 */
    H5FD_log_fapl_t    fa;                  /* Driver-specific file access properties           */
} H5FD_log_t;

/* Prototypes */
static herr_t  H5FD__log_term(void);
static void *  H5FD__log_fapl_get(H5FD_t *file);
static void *  H5FD__log_fapl_copy(const void *_old_fa);
static herr_t  H5FD__log_fapl_free(void *_fa);
static H5FD_t *H5FD__log_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr);
static herr_t  H5FD__log_close(H5FD_t *_file);
static int     H5FD__log_cmp(const H5FD_t *_f1, const H5FD_t *_f2);
static herr_t  H5FD__log_query(const H5FD_t *_f1, unsigned long *flags);
static haddr_t H5FD__log_alloc(H5FD_t *_file, H5FD_mem_t type, hid_t dxpl_id, hsize_t size);
static herr_t  H5FD__log_free(H5FD_t *_file, H5FD_mem_t type, hid_t dxpl_id, haddr_t addr, hsize_t size);
static haddr_t H5FD__log_get_eoa(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__log_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr);
static haddr_t H5FD__log_get_eof(const H5FD_t *_file, H5FD_mem_t type);
static herr_t  H5FD__log_get_handle(H5FD_t *_file, hid_t fapl, void **file_handle);
static herr_t  H5FD__log_read(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                              void *buf);
static herr_t  H5FD__log_write(H5FD_t *_file, H5FD_mem_t type, hid_t fapl_id, haddr_t addr, size_t size,
                               const void *buf);
static herr_t  H5FD__log_truncate(H5FD_t *_file, hid_t dxpl_id, hbool_t closing);
static herr_t  H5FD__log_lock(H5FD_t *_file, hbool_t rw);
static herr_t  H5FD__log_unlock(H5FD_t *_file);

static const H5FD_class_t H5FD_log_g = {
    "log",                   /* name		*/
    H5_POSIX_MAXADDR,	     /* maxaddr		*/
    H5F_CLOSE_WEAK,	     /* fc_degree	*/
    H5FD__log_term,          /* terminate       */
    NULL,                    /* sb_size		*/
    NULL,                    /* sb_encode	*/
    NULL,                    /* sb_decode	*/
    sizeof(H5FD_log_fapl_t), /* fapl_size	*/
    H5FD__log_fapl_get,      /* fapl_get	*/
    H5FD__log_fapl_copy,     /* fapl_copy	*/
    H5FD__log_fapl_free,     /* fapl_free	*/
    0,                       /* dxpl_size	*/
    NULL,                    /* dxpl_copy	*/
    NULL,                    /* dxpl_free	*/
    H5FD__log_open,          /* open		*/
    H5FD__log_close,         /* close		*/
    H5FD__log_cmp,           /* cmp		*/
    H5FD__log_query,         /* query		*/
    NULL,                    /* get_type_map	*/
    H5FD__log_alloc,         /* alloc		*/
    H5FD__log_free,          /* free		*/
    H5FD__log_get_eoa,       /* get_eoa		*/
    H5FD__log_set_eoa,       /* set_eoa		*/
    H5FD__log_get_eof,       /* get_eof		*/
    H5FD__log_get_handle,    /* get_handle      */
    H5FD__log_read,          /* read		*/
    H5FD__log_write,         /* write		*/
    NULL,                    /* flush		*/
    H5FD__log_truncate,      /* truncate	*/
    H5FD__log_lock,          /* lock            */
    H5FD__log_unlock,        /* unlock          */
    H5FD_FLMAP_DICHOTOMY     /* fl_map		*/
};

/* Declare a free list to manage the H5FD_log_t struct */
H5FL_DEFINE_STATIC(H5FD_log_t);

/*-------------------------------------------------------------------------
 * Function:    H5FD__init_package
 *
 * Purpose:     Initializes any interface-specific data or routines.
 *
 * Return:      Non-negative on success/Negative on failure
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__init_package(void)
{
    herr_t ret_value    = SUCCEED;

    FUNC_ENTER_STATIC

    if (H5FD_log_init() < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTINIT, FAIL, "unable to initialize log VFD")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* H5FD__init_package() */

/*-------------------------------------------------------------------------
 * Function:    H5FD_log_init
 *
 * Purpose:     Initialize this driver by registering the driver with the
 *              library.
 *
 * Return:      Success:    The driver ID for the log driver
 *              Failure:    H5I_INVALID_HID
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
hid_t
H5FD_log_init(void)
{
    hid_t ret_value = H5I_INVALID_HID; /* Return value */

    FUNC_ENTER_NOAPI(H5I_INVALID_HID)

    if (H5I_VFL != H5I_get_type(H5FD_LOG_g))
        H5FD_LOG_g = H5FD_register(&H5FD_log_g, sizeof(H5FD_class_t), FALSE);

    /* Set return value */
    ret_value = H5FD_LOG_g;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD_log_init() */

/*---------------------------------------------------------------------------
 * Function:    H5FD__log_term
 *
 * Purpose:     Shut down the VFD
 *
 * Returns:     SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Friday, Jan 30, 2004
 *
 *---------------------------------------------------------------------------
 */
static herr_t
H5FD__log_term(void)
{
    FUNC_ENTER_STATIC_NOERR

    /* Reset VFL ID */
    H5FD_LOG_g = 0;

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__log_term() */

/*-------------------------------------------------------------------------
 * Function:    H5Pset_fapl_log
 *
 * Purpose:     Modify the file access property list to use the H5FD_LOG
 *              driver defined in this source file.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Robb Matzke
 *              Thursday, February 19, 1998
 *
 *-------------------------------------------------------------------------
 */
herr_t
H5Pset_fapl_log(hid_t fapl_id, const char *logfile, unsigned long long flags, size_t buf_size)
{
    H5FD_log_fapl_t fa;        /* File access property list information */
    H5P_genplist_t *plist;     /* Property list pointer */
    herr_t          ret_value; /* Return value */

    FUNC_ENTER_API(FAIL)
    H5TRACE4("e", "i*sULz", fapl_id, logfile, flags, buf_size);

    /* Check arguments */
    if (NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, FAIL, "not a file access property list")

    HDmemset(&fa, 0, sizeof(H5FD_log_fapl_t));

    /* Duplicate the log file string
     * A little wasteful, since this string will just be copied later, but
     * passing it in as a pointer sets off a chain of impossible-to-resolve
     * const cast warnings.
     */
    if (logfile != NULL && NULL == (fa.logfile = H5MM_xstrdup(logfile)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, FAIL, "unable to copy log file name")

    fa.flags    = flags;
    fa.buf_size = buf_size;
    ret_value   = H5P_set_driver(plist, H5FD_LOG, &fa);

done:
    if (fa.logfile)
        H5MM_free(fa.logfile);

    FUNC_LEAVE_API(ret_value)
} /* end H5Pset_fapl_log() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_fapl_get
 *
 * Purpose:     Returns a file access property list which indicates how the
 *              specified file is being accessed. The return list could be
 *              used to access another file the same way.
 *
 * Return:      Success:    Ptr to new file access property list with all
 *                          members copied from the file struct.
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Thursday, April 20, 2000
 *
 *-------------------------------------------------------------------------
 */
static void *
H5FD__log_fapl_get(H5FD_t *_file)
{
    H5FD_log_t *file      = (H5FD_log_t *)_file;
    void *      ret_value = NULL; /* Return value */

    FUNC_ENTER_STATIC_NOERR

    /* Set return value */
    ret_value = H5FD__log_fapl_copy(&(file->fa));

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_fapl_get() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_fapl_copy
 *
 * Purpose:     Copies the log-specific file access properties.
 *
 * Return:      Success:    Ptr to a new property list
 *              Failure:    NULL
 *
 * Programmer:  Quincey Koziol
 *              Thursday, April 20, 2000
 *
 *-------------------------------------------------------------------------
 */
static void *
H5FD__log_fapl_copy(const void *_old_fa)
{
    const H5FD_log_fapl_t *old_fa    = (const H5FD_log_fapl_t *)_old_fa;
    H5FD_log_fapl_t *      new_fa    = NULL; /* New FAPL info */
    void *                 ret_value = NULL; /* Return value */

    FUNC_ENTER_STATIC

    HDassert(old_fa);

    /* Allocate the new FAPL info */
    if (NULL == (new_fa = (H5FD_log_fapl_t *)H5MM_calloc(sizeof(H5FD_log_fapl_t))))
        HGOTO_ERROR(H5E_FILE, H5E_CANTALLOC, NULL, "unable to allocate log file FAPL")

    /* Copy the general information */
    H5MM_memcpy(new_fa, old_fa, sizeof(H5FD_log_fapl_t));

    /* Deep copy the log file name */
    if (old_fa->logfile != NULL)
        if (NULL == (new_fa->logfile = H5MM_strdup(old_fa->logfile)))
            HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "unable to allocate log file name")

    /* Set return value */
    ret_value = new_fa;

done:
    if (NULL == ret_value)
        if (new_fa) {
            if (new_fa->logfile)
                new_fa->logfile = (char *)H5MM_xfree(new_fa->logfile);
            H5MM_free(new_fa);
        } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_fapl_copy() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_fapl_free
 *
 * Purpose:     Frees the log-specific file access properties.
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Thursday, April 20, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_fapl_free(void *_fa)
{
    H5FD_log_fapl_t *fa = (H5FD_log_fapl_t *)_fa;

    FUNC_ENTER_STATIC_NOERR

    /* Free the fapl information */
    if (fa->logfile)
        fa->logfile = (char *)H5MM_xfree(fa->logfile);
    H5MM_xfree(fa);

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__log_fapl_free() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_open
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
static H5FD_t *
H5FD__log_open(const char *name, unsigned flags, hid_t fapl_id, haddr_t maxaddr)
{
    H5FD_log_t *           file = NULL;
    hbool_t file_opened = FALSE;  /* Whether the file was opened */
    H5P_genplist_t *plist;        /* Property list */
    const H5FD_log_fapl_t *fa;    /* File access property list information */
    double _open_time;            /* Time for file open operation */
    double _stat_time;            /* Time for file stat operation */
    double *open_time;            /* Pointer to time for file open operation */
    double *stat_time;            /* Pointer to time for file stat operation */
    H5FD_t *   ret_value = NULL;  /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check on file offsets */
    HDcompile_assert(sizeof(HDoff_t) >= sizeof(size_t));

    /* Get the driver specific information */
    if (NULL == (plist = H5P_object_verify(fapl_id, H5P_FILE_ACCESS)))
        HGOTO_ERROR(H5E_ARGS, H5E_BADTYPE, NULL, "not a file access property list")
    if (NULL == (fa = (const H5FD_log_fapl_t *)H5P_peek_driver_info(plist)))
        HGOTO_ERROR(H5E_PLIST, H5E_BADVALUE, NULL, "bad VFL driver info")

    /* Create the new file struct */
    if (NULL == (file = H5FL_CALLOC(H5FD_log_t)))
        HGOTO_ERROR(H5E_RESOURCE, H5E_NOSPACE, NULL, "unable to allocate file struct")

    /* Set up pointers for open & stat times, if requested */
    open_time = (fa->flags & H5FD_LOG_TIME_OPEN) ? &_open_time : NULL;
    stat_time = (fa->flags & H5FD_LOG_TIME_STAT) ? &_stat_time : NULL;

    /* Open the file */
    if (H5FD__posix_common_open(name, flags, maxaddr, fapl_id, &file->pos_com, open_time, stat_time) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTOPENFILE, NULL, "can't open file")
    file_opened = TRUE;

    /* Get the flags for logging */
    file->fa.flags = fa->flags;
    if (fa->logfile)
        file->fa.logfile = H5MM_strdup(fa->logfile);
    else
        file->fa.logfile = NULL;
    file->fa.buf_size = fa->buf_size;

    /* Check if we are doing any logging at all */
    if (file->fa.flags != 0) {
        /* Allocate buffers for tracking file accesses and data "flavor" */
        file->iosize = fa->buf_size;
        if (file->fa.flags & H5FD_LOG_FILE_READ) {
            file->nread = (unsigned char *)H5MM_calloc(file->iosize);
            HDassert(file->nread);
        } /* end if */
        if (file->fa.flags & H5FD_LOG_FILE_WRITE) {
            file->nwrite = (unsigned char *)H5MM_calloc(file->iosize);
            HDassert(file->nwrite);
        } /* end if */
        if (file->fa.flags & H5FD_LOG_FLAVOR) {
            file->flavor = (unsigned char *)H5MM_calloc(file->iosize);
            HDassert(file->flavor);
        } /* end if */

        /* Set the log file pointer */
        if (fa->logfile)
            file->logfp = HDfopen(fa->logfile, "w");
        else
            file->logfp = stderr;

        /* Output the open & stat times, if requested */
        if (file->fa.flags & H5FD_LOG_TIME_OPEN)
            HDfprintf(file->logfp, "Open took: (%f s)\n", *open_time);
        if (file->fa.flags & H5FD_LOG_TIME_STAT)
            HDfprintf(file->logfp, "Stat took: (%f s)\n", *stat_time);
    } /* end if */

    /* Set return value */
    ret_value = (H5FD_t *)file;

done:
    if (NULL == ret_value) {
        if (file) {
            if (file_opened)
                H5FD__posix_common_close(&file->pos_com, NULL);
            if (file->fa.flags & H5FD_LOG_FILE_WRITE)
                file->nwrite = (unsigned char *)H5MM_xfree(file->nwrite);
            if (file->fa.flags & H5FD_LOG_FILE_READ)
                file->nread = (unsigned char *)H5MM_xfree(file->nread);
            if (file->fa.flags & H5FD_LOG_FLAVOR)
                file->flavor = (unsigned char *)H5MM_xfree(file->flavor);
            if (file->logfp != stderr)
                HDfclose(file->logfp);
            if (file->fa.logfile)
                file->fa.logfile = (char *)H5MM_xfree(file->fa.logfile);
            file = H5FL_FREE(H5FD_log_t, file);
        } /* end if */
    } /* end if */

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_open() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_close
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
static herr_t
H5FD__log_close(H5FD_t *_file)
{
    H5FD_log_t *file = (H5FD_log_t *)_file;
    double      _close_time;         /* Time for file close operation */
    double *    close_time;          /* Pointer to time for file close operation */
    herr_t      ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Set up pointer for close time, if requested */
    close_time = (file->fa.flags & H5FD_LOG_TIME_CLOSE) ? &_close_time : NULL;

    /* Close the underlying file */
    if (H5FD__posix_common_close(&file->pos_com, close_time) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTCLOSEFILE, FAIL, "unable to close file")

    /* Dump I/O information */
    if (file->fa.flags != 0) {
        haddr_t       eoa = HADDR_UNDEF;          /* EOA for the file */
        haddr_t       addr;
        haddr_t       last_addr;
        unsigned char last_val;

        if (file->fa.flags & H5FD_LOG_TIME_CLOSE)
            HDfprintf(file->logfp, "Close took: (%f s)\n", *close_time);

        /* Dump the total number of seek/read/write operations */
        if (file->fa.flags & H5FD_LOG_NUM_READ)
            HDfprintf(file->logfp, "Total number of read operations: %llu\n", file->total_read_ops);
        if (file->fa.flags & H5FD_LOG_NUM_WRITE)
            HDfprintf(file->logfp, "Total number of write operations: %llu\n", file->total_write_ops);
        if (file->fa.flags & H5FD_LOG_NUM_SEEK)
            HDfprintf(file->logfp, "Total number of seek operations: %llu\n", file->total_seek_ops);
        if (file->fa.flags & H5FD_LOG_NUM_TRUNCATE)
            HDfprintf(file->logfp, "Total number of truncate operations: %llu\n", file->total_truncate_ops);

        /* Dump the total time in seek/read/write */
        if (file->fa.flags & H5FD_LOG_TIME_READ)
            HDfprintf(file->logfp, "Total time in read operations: %f s\n", file->total_read_time);
        if (file->fa.flags & H5FD_LOG_TIME_WRITE)
            HDfprintf(file->logfp, "Total time in write operations: %f s\n", file->total_write_time);
        if (file->fa.flags & H5FD_LOG_TIME_SEEK)
            HDfprintf(file->logfp, "Total time in seek operations: %f s\n", file->total_seek_time);
        if (file->fa.flags & H5FD_LOG_TIME_TRUNCATE)
            HDfprintf(file->logfp, "Total time in truncate operations: %f s\n", file->total_truncate_time);

        /* Get the file's EOA */
        if (file->fa.flags & (H5FD_LOG_FILE_WRITE | H5FD_LOG_FILE_READ | H5FD_LOG_FLAVOR))
            if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOA for file")

        /* Dump the write I/O information */
        if (file->fa.flags & H5FD_LOG_FILE_WRITE) {
            /* Sanity check */
            HDassert(H5F_addr_defined(eoa));

            HDfprintf(file->logfp, "Dumping write I/O information:\n");
            last_val  = file->nwrite[0];
            last_addr = 0;
            addr      = 1;
            while(addr < eoa) {
                if (file->nwrite[addr] != last_val) {
                    HDfprintf(file->logfp,
                              "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) written to %3d times\n",
                              last_addr, (addr - 1), (unsigned long)(addr - last_addr), (int)last_val);
                    last_val  = file->nwrite[addr];
                    last_addr = addr;
                } /* end if */
                addr++;
            } /* end while */
            HDfprintf(file->logfp,
                      "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) written to %3d times\n",
                      last_addr, (addr - 1), (unsigned long)(addr - last_addr), (int)last_val);
        } /* end if */

        /* Dump the read I/O information */
        if (file->fa.flags & H5FD_LOG_FILE_READ) {
            /* Sanity check */
            HDassert(H5F_addr_defined(eoa));

            HDfprintf(file->logfp, "Dumping read I/O information:\n");
            last_val  = file->nread[0];
            last_addr = 0;
            addr      = 1;
            while(addr < eoa) {
                if (file->nread[addr] != last_val) {
                    HDfprintf(file->logfp,
                              "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) read from %3d times\n",
                              last_addr, (addr - 1), (unsigned long)(addr - last_addr), (int)last_val);
                    last_val  = file->nread[addr];
                    last_addr = addr;
                } /* end if */
                addr++;
            } /* end while */
            HDfprintf(file->logfp,
                      "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) read from %3d times\n",
                      last_addr, (addr - 1), (unsigned long)(addr - last_addr), (int)last_val);
        } /* end if */

        /* Dump the I/O flavor information */
        if (file->fa.flags & H5FD_LOG_FLAVOR) {
            /* Sanity check */
            HDassert(H5F_addr_defined(eoa));

            HDfprintf(file->logfp, "Dumping I/O flavor information:\n");
            last_val  = file->flavor[0];
            last_addr = 0;
            addr      = 1;
            while(addr < eoa) {
                if (file->flavor[addr] != last_val) {
                    HDfprintf(file->logfp,
                              "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) flavor is %s\n",
                              last_addr, (addr - 1), (unsigned long)(addr - last_addr), flavors[last_val]);
                    last_val  = file->flavor[addr];
                    last_addr = addr;
                } /* end if */
                addr++;
            } /* end while */
            HDfprintf(file->logfp, "\tAddr %10" PRIuHADDR "-%10" PRIuHADDR " (%10lu bytes) flavor is %s\n",
                      last_addr, (addr - 1), (unsigned long)(addr - last_addr), flavors[last_val]);
        } /* end if */

        /* Free the logging information */
        if (file->fa.flags & H5FD_LOG_FILE_WRITE)
            file->nwrite = (unsigned char *)H5MM_xfree(file->nwrite);
        if (file->fa.flags & H5FD_LOG_FILE_READ)
            file->nread = (unsigned char *)H5MM_xfree(file->nread);
        if (file->fa.flags & H5FD_LOG_FLAVOR)
            file->flavor = (unsigned char *)H5MM_xfree(file->flavor);
        if (file->logfp != stderr)
            HDfclose(file->logfp);
    } /* end if */

    if (file->fa.logfile)
        file->fa.logfile = (char *)H5MM_xfree(file->fa.logfile);

    /* Release the file info */
    file = H5FL_FREE(H5FD_log_t, file);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_close() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_cmp
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
static int
H5FD__log_cmp(const H5FD_t *_f1, const H5FD_t *_f2)
{
    const H5FD_log_t *f1        = (const H5FD_log_t *)_f1;
    const H5FD_log_t *f2        = (const H5FD_log_t *)_f2;
    int               ret_value = 0;

    FUNC_ENTER_STATIC_NOERR

    ret_value = H5FD__posix_common_cmp(&f1->pos_com, &f2->pos_com);

    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_cmp() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_query
 *
 * Purpose:     Set the flags that this VFL driver is capable of supporting.
 *              (listed in H5FDpublic.h)
 *
 * Return:      SUCCEED (Can't fail)
 *
 * Programmer:  Quincey Koziol
 *              Friday, August 25, 2000
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_query(const H5FD_t H5_ATTR_UNUSED *_file, unsigned long *flags)
{
    FUNC_ENTER_STATIC_NOERR

    /* clang-format off */
    /* Set the VFL feature flags that this driver supports */
    if (flags) {
        *flags = 0;
        *flags |= H5FD_FEAT_AGGREGATE_METADATA;     /* OK to aggregate metadata allocations                             */
        *flags |= H5FD_FEAT_ACCUMULATE_METADATA;    /* OK to accumulate metadata for faster writes                      */
        *flags |= H5FD_FEAT_DATA_SIEVE;             /* OK to perform data sieving for faster raw data reads & writes    */
        *flags |= H5FD_FEAT_AGGREGATE_SMALLDATA;    /* OK to aggregate "small" raw data allocations                     */
        *flags |= H5FD_FEAT_POSIX_COMPAT_HANDLE;    /* Get_handle callback returns a POSIX file descriptor */
        *flags |= H5FD_FEAT_SUPPORTS_SWMR_IO;       /* VFD supports the single-writer/multiple-readers (SWMR) pattern   */
        *flags |= H5FD_FEAT_DEFAULT_VFD_COMPATIBLE; /* VFD creates a file which can be opened with the default VFD */
    } /* end if */
    /* clang-format on */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__log_query() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_alloc
 *
 * Purpose:     Allocate file memory.
 *
 * Return:      Success:    Address of new memory
 *              Failure:    HADDR_UNDEF
 *
 * Programmer:  Quincey Koziol
 *              Monday, April 17, 2000
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__log_alloc(H5FD_t *_file, H5FD_mem_t type, hid_t H5_ATTR_UNUSED dxpl_id, hsize_t size)
{
    H5FD_log_t *file = (H5FD_log_t *)_file;
    haddr_t     addr;
    haddr_t     ret_value = HADDR_UNDEF; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Get the file's EOA, for the address for the block to allocate */
    if (H5FD__posix_common_get_eoa(&file->pos_com, &addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOA for file")

    /* Extend the end-of-allocated space address */
    if (H5FD__posix_common_set_eoa(&file->pos_com, (addr + size)) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, HADDR_UNDEF, "unable to set EOA for file")

    /* Retain the (first) flavor of the information written to the file */
    if (file->fa.flags != 0) {
        if (file->fa.flags & H5FD_LOG_FLAVOR) {
            HDassert(addr < file->iosize);
            H5_CHECK_OVERFLOW(size, hsize_t, size_t);
            HDmemset(&file->flavor[addr], (int)type, (size_t)size);
        } /* end if */

        if (file->fa.flags & H5FD_LOG_ALLOC)
            HDfprintf(file->logfp,
                      "%10" PRIuHADDR "-%10" PRIuHADDR " (%10" PRIuHSIZE " bytes) (%s) Allocated\n", addr,
                      (haddr_t)((addr + size) - 1), size, flavors[type]);
    } /* end if */

    /* Set return value */
    ret_value = addr;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_alloc() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_free
 *
 * Purpose:     Release file memory.
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Quincey Koziol
 *              Wednesday, September 28, 2016
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_free(H5FD_t *_file, H5FD_mem_t type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr, hsize_t size)
{
    H5FD_log_t *file = (H5FD_log_t *)_file;

    FUNC_ENTER_STATIC_NOERR

    /* Sanity check */
    HDassert(file);

    if (file->fa.flags != 0) {
        /* Reset the flavor of the information in the file */
        if (file->fa.flags & H5FD_LOG_FLAVOR) {
            HDassert(addr < file->iosize);
            H5_CHECK_OVERFLOW(size, hsize_t, size_t);
            HDmemset(&file->flavor[addr], H5FD_MEM_DEFAULT, (size_t)size);
        } /* end if */

        /* Log the file memory freed */
        if (file->fa.flags & H5FD_LOG_FREE)
            HDfprintf(file->logfp, "%10" PRIuHADDR "-%10" PRIuHADDR " (%10" PRIuHSIZE " bytes) (%s) Freed\n",
                      addr, (haddr_t)((addr + size) - 1), size, flavors[type]);
    } /* end if */

    FUNC_LEAVE_NOAPI(SUCCEED)
} /* end H5FD__log_free() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_get_eoa
 *
 * Purpose:     Gets the end-of-address marker for the file. The EOA marker
 *              is the first address past the last byte allocated in the
 *              format address space.
 *
 * Return:      Success:    The end-of-address marker.
 *              Failure:    HADDR_UNDEF
 *
 * Programmer:  Robb Matzke
 *              Monday, August  2, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__log_get_eoa(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_log_t *file = (const H5FD_log_t *)_file;
    haddr_t eoa = HADDR_UNDEF;          /* EOA for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Get the file's EOA */
    if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOA for file")

    /* Set the return value */
    ret_value = eoa;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_get_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_set_eoa
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
static herr_t
H5FD__log_set_eoa(H5FD_t *_file, H5FD_mem_t type, haddr_t addr)
{
    H5FD_log_t *file = (H5FD_log_t *)_file;
    herr_t ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    if (file->fa.flags != 0) {
        haddr_t eoa = HADDR_UNDEF;          /* EOA for the file */

        /* Get the file's EOA */
        if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
            HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOA for file")

        /* Check for increasing file size */
        if (H5F_addr_gt(addr, eoa) && H5F_addr_gt(addr, 0)) {
            hsize_t size = addr - eoa;

            /* Retain the flavor of the space allocated by the extension */
            if (file->fa.flags & H5FD_LOG_FLAVOR) {
                HDassert(addr < file->iosize);
                H5_CHECK_OVERFLOW(size, hsize_t, size_t);
                HDmemset(&file->flavor[eoa], (int)type, (size_t)size);
            } /* end if */

            /* Log the extension like an allocation */
            if (file->fa.flags & H5FD_LOG_ALLOC)
                HDfprintf(file->logfp,
                          "%10" PRIuHADDR "-%10" PRIuHADDR " (%10" PRIuHSIZE " bytes) (%s) Allocated\n",
                          eoa, addr, size, flavors[type]);
        } /* end if */

        /* Check for decreasing file size */
        if (H5F_addr_lt(addr, eoa) && H5F_addr_gt(addr, 0)) {
            hsize_t size = eoa - addr;

            /* Reset the flavor of the space freed by the shrink */
            if (file->fa.flags & H5FD_LOG_FLAVOR) {
                HDassert((addr + size) < file->iosize);
                H5_CHECK_OVERFLOW(size, hsize_t, size_t);
                HDmemset(&file->flavor[addr], H5FD_MEM_DEFAULT, (size_t)size);
            } /* end if */

            /* Log the shrink like a free */
            if (file->fa.flags & H5FD_LOG_FREE)
                HDfprintf(file->logfp,
                          "%10" PRIuHADDR "-%10" PRIuHADDR " (%10" PRIuHSIZE " bytes) (%s) Freed\n",
                          eoa, addr, size, flavors[type]);
        } /* end if */
    } /* end if */

    /* Set the file's EOA */
    if (H5FD__posix_common_set_eoa(&file->pos_com, addr) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTSET, FAIL, "unable to set EOA for file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_set_eoa() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_get_eof
 *
 * Purpose:     Returns the end-of-file marker, which is the greater of
 *              either the filesystem end-of-file or the HDF5 end-of-address
 *              markers.
 *
 * Return:      Success:    End of file address, the first address past
 *                          the end of the "file", either the filesystem file
 *                          or the HDF5 file.
 *              Failure:    HADDR_UNDEF
 *
 * Programmer:  Robb Matzke
 *              Thursday, July 29, 1999
 *
 *-------------------------------------------------------------------------
 */
static haddr_t
H5FD__log_get_eof(const H5FD_t *_file, H5FD_mem_t H5_ATTR_UNUSED type)
{
    const H5FD_log_t *file = (const H5FD_log_t *)_file;
    haddr_t eof = HADDR_UNDEF;          /* EOF for the file */
    haddr_t ret_value = HADDR_UNDEF;    /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Get the file's EOF */
    if (H5FD__posix_common_get_eof(&file->pos_com, &eof) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, HADDR_UNDEF, "unable to get EOF for file")

    /* Set the return value */
    ret_value = eof;

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_get_eof() */

/*-------------------------------------------------------------------------
 * Function:       H5FD__log_get_handle
 *
 * Purpose:        Returns the file handle of LOG file driver.
 *
 * Returns:        SUCCEED/FAIL
 *
 * Programmer:     Raymond Lu
 *                 Sept. 16, 2002
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_get_handle(H5FD_t *_file, hid_t H5_ATTR_UNUSED fapl, void **file_handle)
{
    H5FD_log_t *file      = (H5FD_log_t *)_file;
    herr_t      ret_value = SUCCEED;

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Check args */
    if (!file_handle)
        HGOTO_ERROR(H5E_ARGS, H5E_BADVALUE, FAIL, "file handle not valid")

    /* Get the file's handle */
    if (H5FD__posix_common_get_handle(&file->pos_com, file_handle) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get handle for file")

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_get_handle() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_read
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
static herr_t
H5FD__log_read(H5FD_t *_file, H5FD_mem_t type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr, size_t size,
               void *buf /*out*/)
{
    H5FD_log_t *         file      = (H5FD_log_t *)_file;
    double               _read_time;         /* Elapsed time for read operation */
    double               _read_start_time;   /* Start time for read operation */
#ifndef H5_HAVE_PREADWRITE
    double               _seek_time;         /* Elapsed time for seek operation */
    double               _seek_start_time;   /* Start time for seek operation */
    haddr_t              _old_off;           /* Previous offset, before seek */
    haddr_t              _new_off;           /* Current offset, after seek */
#endif /* H5_HAVE_PREADWRITE */
    H5FD_posix_rw_info_t rw_info;   /* Info for read operation */
    herr_t               ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(buf);

#ifndef H5_HAVE_PREADWRITE
    /* Set up seek info, if requested */
    rw_info.did_seek = FALSE;
    if (file->fa.flags & H5FD_LOG_TIME_SEEK) {
        rw_info.seek_time = &_seek_time;
        rw_info.seek_start_time = &_seek_start_time;
    } /* end if */
    else
        rw_info.seek_time = rw_info.seek_start_time = NULL;
    if (file->fa.flags & H5FD_LOG_LOC_SEEK) {
        rw_info.old_off = &_old_off;
        rw_info.new_off = &_new_off;
    } /* end if */
    else
        rw_info.old_off = rw_info.new_off = NULL;
#endif /* H5_HAVE_PREADWRITE */

    /* Set up pointers for read time, if requested */
    if (file->fa.flags & H5FD_LOG_TIME_READ) {
        rw_info.op_time = &_read_time;
        rw_info.op_start_time = &_read_start_time;
    } /* end if */
    else
        rw_info.op_time = rw_info.op_start_time = NULL;

    /* Perform the read */
    if (H5FD__posix_common_read(&file->pos_com, addr, size, buf, &rw_info) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_READERROR, FAIL, "can't read from file")

#ifndef H5_HAVE_PREADWRITE
    /* Check for seek to the correct location (if we don't have pread) */
    if (rw_info.did_seek) {
        /* Add to the number of seeks, when tracking that */
        if (file->fa.flags & H5FD_LOG_NUM_SEEK)
            file->total_seek_ops++;

        /* Add to the total seek time, when tracking that */
        if (file->fa.flags & H5FD_LOG_TIME_SEEK)
            file->total_seek_time += *rw_info.seek_time;

        /* Emit log string if we're tracking individual seek events. */
        if (file->fa.flags & H5FD_LOG_LOC_SEEK) {
            HDfprintf(file->logfp, "Seek: From %10" PRIuHADDR " To %10" PRIuHADDR, *rw_info.old_pos, *rw_info.new_pos);

            /* Add the seek time, if we're tracking that.
             * Note that the seek time is NOT emitted for when just H5FD_LOG_TIME_SEEK
             * is set.
             */
            if (file->fa.flags & H5FD_LOG_TIME_SEEK)
                HDfprintf(file->logfp, " (%fs @ %f)\n", *rw_info.seek_time, *rw_info.seek_start_time);
            else
                HDfprintf(file->logfp, "\n");
        } /* end if */
    }     /* end if */
#endif /* H5_HAVE_PREADWRITE */

    /* Log the I/O information about the read */
    if (file->fa.flags & H5FD_LOG_FILE_READ) {
        size_t tmp_size = size;
        haddr_t tmp_addr = addr;

        /* Log information about the number of times these locations are read */
        HDassert((tmp_addr + tmp_size) < file->iosize);
        while(tmp_size-- > 0)
            file->nread[tmp_addr++]++;
    } /* end if */

    /* Add to the number of reads, when tracking that */
    if (file->fa.flags & H5FD_LOG_NUM_READ)
        file->total_read_ops++;

    /* Add to the total read time, when tracking that */
    if (file->fa.flags & H5FD_LOG_TIME_READ)
        file->total_read_time += *rw_info.op_time;

    /* Log information about the read */
    if (file->fa.flags & H5FD_LOG_LOC_READ) {
        HDfprintf(file->logfp, "%10" PRIuHADDR "-%10" PRIuHADDR " (%10zu bytes) (%s) Read", addr,
                  (addr + size) - 1, size, flavors[type]);

        /* Verify that we are reading in the type of data we allocated in this location */
        if (file->flavor) {
            HDassert(type == H5FD_MEM_DEFAULT || type == (H5FD_mem_t)file->flavor[addr] ||
                     (H5FD_mem_t)file->flavor[addr] == H5FD_MEM_DEFAULT);
            HDassert(type == H5FD_MEM_DEFAULT ||
                     type == (H5FD_mem_t)file->flavor[(addr + size) - 1] ||
                     (H5FD_mem_t)file->flavor[(addr + size) - 1] == H5FD_MEM_DEFAULT);
        } /* end if */

        /* Add the read time, if we're tracking that.
         * Note that the read time is NOT emitted for when just H5FD_LOG_TIME_READ
         * is set.
         */
        if (file->fa.flags & H5FD_LOG_TIME_READ)
            HDfprintf(file->logfp, " (%fs @ %f)\n", *rw_info.op_time, *rw_info.op_start_time);
        else
            HDfprintf(file->logfp, "\n");
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_read() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_write
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
static herr_t
H5FD__log_write(H5FD_t *_file, H5FD_mem_t type, hid_t H5_ATTR_UNUSED dxpl_id, haddr_t addr, size_t size,
                const void *buf)
{
    H5FD_log_t *  file      = (H5FD_log_t *)_file;
    double               _write_time;        /* Elapsed time for write operation */
    double               _write_start_time;  /* Start time for write operation */
#ifndef H5_HAVE_PREADWRITE
    double               _seek_time;         /* Elapsed time for seek operation */
    double               _seek_start_time;   /* Start time for seek operation */
    haddr_t              _old_off;           /* Previous offset, before seek */
    haddr_t              _new_off;           /* Current offset, after seek */
#endif /* H5_HAVE_PREADWRITE */
    H5FD_posix_rw_info_t rw_info;   /* Info for read operation */
    herr_t  ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    /* Sanity checks */
    HDassert(file && file->pub.cls);
    HDassert(size > 0);
    HDassert(buf);

    /* Verify that we are writing out the type of data we allocated in this location */
    if (file->flavor) {
        HDassert(type == H5FD_MEM_DEFAULT || type == (H5FD_mem_t)file->flavor[addr] ||
                 (H5FD_mem_t)file->flavor[addr] == H5FD_MEM_DEFAULT);
        HDassert(type == H5FD_MEM_DEFAULT || type == (H5FD_mem_t)file->flavor[(addr + size) - 1] ||
                 (H5FD_mem_t)file->flavor[(addr + size) - 1] == H5FD_MEM_DEFAULT);
    } /* end if */

#ifndef H5_HAVE_PREADWRITE
    /* Set up seek info, if requested */
    rw_info.did_seek = FALSE;
    if (file->fa.flags & H5FD_LOG_TIME_SEEK) {
        rw_info.seek_time = &_seek_time;
        rw_info.seek_start_time = &_seek_start_time;
    } /* end if */
    else
        rw_info.seek_time = rw_info.seek_start_time = NULL;
    if (file->fa.flags & H5FD_LOG_LOC_SEEK) {
        rw_info.old_off = &_old_off;
        rw_info.new_off = &_new_off;
    } /* end if */
    else
        rw_info.old_off = rw_info.new_off = NULL;
#endif /* H5_HAVE_PREADWRITE */

    /* Set up pointers for write time, if requested */
    if (file->fa.flags & H5FD_LOG_TIME_WRITE) {
        rw_info.op_time = &_write_time;
        rw_info.op_start_time = &_write_start_time;
    } /* end if */
    else
        rw_info.op_time = rw_info.op_start_time = NULL;

    /* Perform the write */
    if (H5FD__posix_common_write(&file->pos_com, addr, size, buf, &rw_info) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_WRITEERROR, FAIL, "can't write to file")

#ifndef H5_HAVE_PREADWRITE
    /* Check for seek to the correct location (if we don't have pwrite) */
    if (rw_info.did_seek) {
        /* Add to the number of seeks, when tracking that */
        if (file->fa.flags & H5FD_LOG_NUM_SEEK)
            file->total_seek_ops++;

        /* Add to the total seek time, when tracking that */
        if (file->fa.flags & H5FD_LOG_TIME_SEEK)
            file->total_seek_time += *rw_info.seek_time;

        /* Emit log string if we're tracking individual seek events. */
        if (file->fa.flags & H5FD_LOG_LOC_SEEK) {
            HDfprintf(file->logfp, "Seek: From %10" PRIuHADDR " To %10" PRIuHADDR, *rw_info.old_pos, *rw_info.new_pos);

            /* Add the seek time, if we're tracking that.
             * Note that the seek time is NOT emitted for when just H5FD_LOG_TIME_SEEK
             * is set.
             */
            if (file->fa.flags & H5FD_LOG_TIME_SEEK)
                HDfprintf(file->logfp, " (%fs @ %f)\n", *rw_info.seek_time, *rw_info.seek_start_time);
            else
                HDfprintf(file->logfp, "\n");
        } /* end if */
    }     /* end if */
#endif /* H5_HAVE_PREADWRITE */

    /* Log the I/O information about the write */
    if (file->fa.flags & H5FD_LOG_FILE_WRITE) {
        size_t tmp_size = size;
        haddr_t tmp_addr = addr;

        /* Log information about the number of times these locations are read */
        HDassert((tmp_addr + tmp_size) < file->iosize);
        while(tmp_size-- > 0)
            file->nwrite[tmp_addr++]++;
    } /* end if */

    /* Add to the number of writes, when tracking that */
    if (file->fa.flags & H5FD_LOG_NUM_WRITE)
        file->total_write_ops++;

    /* Add to the total write time, when tracking that */
    if (file->fa.flags & H5FD_LOG_TIME_WRITE)
        file->total_write_time += *rw_info.op_time;

    /* Log information about the write */
    if (file->fa.flags & H5FD_LOG_LOC_WRITE) {
        HDfprintf(file->logfp, "%10" PRIuHADDR "-%10" PRIuHADDR " (%10zu bytes) (%s) Written", addr,
                  (addr + size) - 1, size, flavors[type]);

        /* Check if this is the first write into a "default" section,
         * grabbed by the metadata agregation algorithm */
        if (file->fa.flags & H5FD_LOG_FLAVOR)
            if ((H5FD_mem_t)file->flavor[addr] == H5FD_MEM_DEFAULT) {
                HDmemset(&file->flavor[addr], (int)type, size);
                HDfprintf(file->logfp, " (fresh)");
            } /* end if */

        /* Add the write time, if we're tracking that.
         * Note that the write time is NOT emitted for when just H5FD_LOG_TIME_WRITE
         * is set.
         */
        if (file->fa.flags & H5FD_LOG_TIME_WRITE)
            HDfprintf(file->logfp, " (%fs @ %f)\n", *rw_info.op_time, *rw_info.op_start_time);
        else
            HDfprintf(file->logfp, "\n");
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_write() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_truncate
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
static herr_t
H5FD__log_truncate(H5FD_t *_file, hid_t H5_ATTR_UNUSED dxpl_id, hbool_t H5_ATTR_UNUSED closing)
{
    H5FD_log_t *file      = (H5FD_log_t *)_file;
    double      _trunc_time;         /* Elapsed time for truncate operation */
    double      _trunc_start_time;   /* Start time for truncate operation */
    H5FD_posix_trunc_info_t trunc_info;   /* Info for truncate operation */
    herr_t      ret_value = SUCCEED; /* Return value */

    FUNC_ENTER_STATIC

    HDassert(file);

    /* Set up pointers for truncate time, if requested */
    trunc_info.did_trunc = FALSE;
    if (file->fa.flags & H5FD_LOG_TIME_TRUNCATE) {
        trunc_info.start_time = &_trunc_start_time;
        trunc_info.elap_time = &_trunc_time;
    } /* end if */
    else
        trunc_info.start_time = trunc_info.elap_time = NULL;

    /* Truncate the file to the current EOA */
    if (H5FD__posix_common_truncate(&file->pos_com, HADDR_UNDEF, &trunc_info) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTTRUNCATE, FAIL, "can't truncate file")

    /* Check for truncate performed */
    if (trunc_info.did_trunc) {
        /* Add to the number of truncates, when tracking that */
        if (file->fa.flags & H5FD_LOG_NUM_TRUNCATE)
            file->total_truncate_ops++;

        /* Add to the total truncate time, when tracking that */
        if (file->fa.flags & H5FD_LOG_TIME_TRUNCATE)
            file->total_truncate_time += *trunc_info.elap_time;

        /* Emit log string if we're tracking individual truncate events. */
        if (file->fa.flags & H5FD_LOG_TRUNCATE) {
            haddr_t eoa;

            /* Get the new length of the file */
            if (H5FD__posix_common_get_eoa(&file->pos_com, &eoa) < 0)
                HGOTO_ERROR(H5E_VFL, H5E_CANTGET, FAIL, "unable to get EOA for file")

            HDfprintf(file->logfp, "Truncate: To %10" PRIuHADDR, eoa);

            /* Add the truncate time, if we're tracking that.
             * Note that the truncate time is NOT emitted for when just H5FD_LOG_TIME_TRUNCATE
             * is set.
             */
            if (file->fa.flags & H5FD_LOG_TIME_TRUNCATE)
                HDfprintf(file->logfp, " (%fs @ %f)\n", *trunc_info.elap_time, *trunc_info.start_time);
            else
                HDfprintf(file->logfp, "\n");
        } /* end if */
    } /* end if */

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_truncate() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_lock
 *
 * Purpose:     Place a lock on the file
 *
 * Return:      Success:    SUCCEED
 *              Failure:    FAIL, file not locked.
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_lock(H5FD_t *_file, hbool_t rw)
{
    H5FD_log_t *file = (H5FD_log_t *)_file; /* VFD file struct          */
    double      _lock_time;         /* Time for file lock operation */
    double *    lock_time;          /* Pointer to time for file lock operation */
    herr_t      ret_value = SUCCEED;        /* Return value             */

    FUNC_ENTER_STATIC

    /* Sanity check */
    HDassert(file);

    /* Set up pointer for lock time, if requested */
    lock_time = (file->fa.flags & H5FD_LOG_TIME_LOCK) ? &_lock_time : NULL;

    /* Lock the file */
    if (H5FD__posix_common_lock(&file->pos_com, rw, lock_time) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTLOCK, FAIL, "can't lock file")

    /* Output the lock time, if requested */
    if (file->fa.flags & H5FD_LOG_TIME_LOCK)
        HDfprintf(file->logfp, "Lock took: (%f s)\n", *lock_time);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_lock() */

/*-------------------------------------------------------------------------
 * Function:    H5FD__log_unlock
 *
 * Purpose:     Remove the existing lock on the file
 *
 * Return:      SUCCEED/FAIL
 *
 * Programmer:  Vailin Choi; May 2013
 *
 *-------------------------------------------------------------------------
 */
static herr_t
H5FD__log_unlock(H5FD_t *_file)
{
    H5FD_log_t *file      = (H5FD_log_t *)_file; /* VFD file struct          */
    double      _unlock_time;         /* Time for file unlock operation */
    double *    unlock_time;          /* Pointer to time for file unlock operation */
    herr_t      ret_value = SUCCEED;             /* Return value             */

    FUNC_ENTER_STATIC

    HDassert(file);

    /* Set up pointer for unlock time, if requested */
    unlock_time = (file->fa.flags & H5FD_LOG_TIME_UNLOCK) ? &_unlock_time : NULL;

    /* Unlock the file */
    if (H5FD__posix_common_unlock(&file->pos_com, unlock_time) < 0)
        HGOTO_ERROR(H5E_VFL, H5E_CANTUNLOCK, FAIL, "can't unlock file")

    /* Output the unlock time, if requested */
    if (file->fa.flags & H5FD_LOG_TIME_UNLOCK)
        HDfprintf(file->logfp, "Unlock took: (%f s)\n", *unlock_time);

done:
    FUNC_LEAVE_NOAPI(ret_value)
} /* end H5FD__log_unlock() */
