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
 *  Header file for API contexts, etc.
 */
#ifndef H5CXprivate_H
#define H5CXprivate_H

/* Private headers needed by this file */
#include "H5private.h"   /* Generic Functions                    */
#include "H5ACprivate.h" /* Metadata cache                       */
#ifdef H5_HAVE_PARALLEL
#include "H5FDprivate.h" /* File drivers                         */
#endif                   /* H5_HAVE_PARALLEL */
#include "H5Oprivate.h"  /* Object headers                       */
#include "H5Pprivate.h"  /* Property lists                       */
#include "H5Tprivate.h"  /* Datatypes                            */
#include "H5Tconv.h"     /* Datatype conversions                 */
#include "H5Zprivate.h"  /* Data filters                         */

/* Includes needed to set default VFD values */
#ifdef H5_HAVE_SUBFILING_VFD
#include "H5FDsubfiling_private.h" /* subfiling VFD driver */
#endif

/**************************/
/* Library Private Macros */
/**************************/

/****************************/
/* Library Private Typedefs */
/****************************/

/* API context state */
typedef struct H5CX_state_t {
    hid_t dxpl_id;      /* DXPL for operation */
    hid_t fapl_id;      /* FAPL for operation */
    hid_t lapl_id;      /* LAPL for operation */
    hid_t lcpl_id;      /* LCPL for operation */
    hid_t ocpl_id;      /* DCPL/GCPL/TCPL for operation */
    hid_t ocpypl_id;    /* OCPYPL for operation */
    void *vol_wrap_ctx; /* VOL connector's "wrap context" for creating IDs */

#ifdef H5_HAVE_PARALLEL
    /* Internal: Parallel I/O settings */
    bool coll_metadata_read; /* Whether to use collective I/O for metadata read */
#endif                       /* H5_HAVE_PARALLEL */
} H5CX_state_t;

/* Typedef for context about each API call, as it proceeds */
/* Fields in this struct are of several types:
 * - The DXPL & LAPL ID are either library default ones (from the API context
 *      initialization) or passed in from the application via an API call
 *      parameter.  The corresponding H5P_genplist_t* is just the underlying
 *      property list struct for the ID, to optimize retrieving properties
 *      from the list multiple times.
 *
 * - Internal fields, used and set only within the library, for managing the
 *      operation under way.  These do not correspond to properties in the
 *      DXPL or LAPL and can have any name.
 *
 * - Cached fields, which are not returned to the application, for managing
 *      the operation under way.  These correspond to properties in the DXPL
 *      or LAPL, and are retrieved either from the (global) cache for a
 *      default property list, or from the corresponding property in the
 *      application's (non-default) property list.  Getting / setting these
 *      properties within the library does _not_ affect the application's
 *      property list.  Note that the naming of these fields, <foo> and
 *      <foo>_valid, is important for the H5CX_RETRIEVE_PROP_VALID
 *      macro to work properly.
 *
 * - "Return-only" properties that are returned to the application, mainly
 *      for sending out "introspection" information ("Why did collective I/O
 *      get broken for this operation?", "Which filters are set on the chunk I
 *      just directly read in?", etc) Setting these fields will cause the
 *      corresponding property in the property list to be set when the API
 *      context is popped, when returning from the API routine.  Note that the
 *      naming of these fields, <foo> and <foo>_set, is important for the
 *      H5CX_TEST_SET_PROP and H5CX_SET_PROP macros to work properly.
 *
 * - "Return-and-read" properties that are returned to the application to send out introspection information,
 *      but are also queried by the library internally. If the context value has been 'set' by an accessor,
 *      all future queries will return the stored value from the context, to avoid later queries overwriting
 *      that stored value with the value from the property list.
 *
 *      These properties have both a 'valid' and 'set' flag. <foo>_valid is true if the field has ever been
 *      populated from its underlying property list. <foo>_set flag is true if this field has ever been set on
 *      the context for application introspection. The naming of these fields is important for the
 *      H5CX_RETRIEVE_PROP_VALID_SET macro to work properly.
 *
 *      If a field has been set on the context but never read internally, <foo>_valid will be false
 *      despite the context containing a meaningful cached value.
 */
typedef struct H5CX_t {
    /* DXPL */
    hid_t           dxpl_id; /* DXPL ID for API operation */
    H5P_genplist_t *dxpl;    /* Dataset Transfer Property List */

    /* LCPL */
    hid_t           lcpl_id; /* LCPL ID for API operation */
    H5P_genplist_t *lcpl;    /* Link Creation Property List */

    /* LAPL */
    hid_t           lapl_id; /* LAPL ID for API operation */
    H5P_genplist_t *lapl;    /* Link Access Property List */

    /* OCPL */
    hid_t           ocpl_id; /* OCPL (i.e. DCPL, GCPL, or TCPL) ID for API operation */
    H5P_genplist_t *ocpl;    /* Object Creation Property List */

    /* OCPYPL */
    hid_t           ocpypl_id; /* OCPYPL ID for API operation */
    H5P_genplist_t *ocpypl;    /* Object Copy Property List */

    /* DAPL */
    hid_t           dapl_id; /* DAPL ID for API operation */
    H5P_genplist_t *dapl;    /* Dataset Access Property List */

    /* FAPL */
    hid_t           fapl_id; /* FAPL ID for API operation */
    H5P_genplist_t *fapl;    /* File Access Property List */

    /* Internal: Object tagging info */
    haddr_t tag; /* Current object's tag (ohdr chunk #0 address) */

    /* Internal: Metadata cache info */
    H5AC_ring_t ring; /* Current metadata cache ring for entries */

    /* Internal: Want POSIX file descriptor from core VFD*/
    bool want_posix_fd; /* Whether to want the POSIX file descriptor from get_vfd_handle call to core VFD */

#ifdef H5_HAVE_PARALLEL
    /* Internal: Parallel I/O settings */
    bool         coll_metadata_read; /* Whether to use collective I/O for metadata read */
    MPI_Datatype btype;              /* MPI datatype for buffer, when using collective I/O */
    MPI_Datatype ftype;              /* MPI datatype for file, when using collective I/O */
    bool         mpi_file_flushing;  /* Whether an MPI-opened file is being flushed */
    bool         rank0_bcast;        /* Whether a dataset meets read-with-rank0-and-bcast requirements */
#ifdef H5_HAVE_SUBFILING_VFD
    uint64_t sf_stub_file_id; /* Stub file ID for subfiling/IOC VFDs */
#endif                        /* H5_HAVE_SUBFILING_VFD */
#endif                        /* H5_HAVE_PARALLEL */

    /* Cached DXPL properties */
    size_t    max_temp_buf;         /* Maximum temporary buffer size (H5D_XFER_MAX_TEMP_BUF_NAME) .*/
    void     *tconv_buf;            /* Temporary conversion buffer (H5D_XFER_TCONV_BUF_NAME) */
    void     *bkgr_buf;             /* Background conversion buffer (H5D_XFER_BKGR_BUF_NAME) */
    H5T_bkg_t bkgr_buf_type;        /* Background buffer type (H5D_XFER_BKGR_BUF_TYPE_NAME) */
    double    btree_split_ratio[3]; /* B-tree split ratios (H5D_XFER_BTREE_SPLIT_RATIO_NAME) */
    size_t    vec_size;             /* Size of hyperslab vector (H5D_XFER_HYPER_VECTOR_SIZE_NAME) */
#ifdef H5_HAVE_PARALLEL
    H5FD_mpio_xfer_t io_xfer_mode; /* Parallel transfer mode for this request (H5D_XFER_IO_XFER_MODE_NAME) */
    H5FD_mpio_collective_opt_t mpio_coll_opt; /* Parallel transfer with independent IO or collective IO with
                                                 this mode (H5D_XFER_MPIO_COLLECTIVE_OPT_NAME) */
    H5FD_mpio_chunk_opt_t
             mpio_chunk_opt_mode;       /* Collective chunk option (H5D_XFER_MPIO_CHUNK_OPT_HARD_NAME) */
    unsigned mpio_chunk_opt_num;        /* Collective chunk threshold (H5D_XFER_MPIO_CHUNK_OPT_NUM_NAME) */
    unsigned mpio_chunk_opt_ratio;      /* Collective chunk ratio (H5D_XFER_MPIO_CHUNK_OPT_RATIO_NAME) */
#endif                                  /* H5_HAVE_PARALLEL */
    H5Z_EDC_t               err_detect; /* Error detection info (H5D_XFER_EDC_NAME) */
    H5Z_cb_t                filter_cb;  /* Filter callback function (H5D_XFER_FILTER_CB_NAME) */
    H5Z_data_xform_t       *data_transform;    /* Data transform info (H5D_XFER_XFORM_NAME) */
    H5T_vlen_alloc_info_t   vl_alloc_info;     /* VL datatype alloc info (H5D_XFER_VLEN_*_NAME) */
    H5T_conv_cb_t           dt_conv_cb;        /* Datatype conversion struct (H5D_XFER_CONV_CB_NAME) */
    H5D_selection_io_mode_t selection_io_mode; /* Selection I/O mode (H5D_XFER_SELECTION_IO_MODE_NAME) */
    bool modify_write_buf; /* Whether the library can modify write buffers (H5D_XFER_MODIFY_WRITE_BUF_NAME)*/

    /* Return-only DXPL properties to return to application */
#ifdef H5_HAVE_PARALLEL
    H5D_mpio_actual_chunk_opt_mode_t mpio_actual_chunk_opt; /* Chunk optimization mode used for parallel I/O
                                                               (H5D_MPIO_ACTUAL_CHUNK_OPT_MODE_NAME) */
    H5D_mpio_actual_io_mode_t
             mpio_actual_io_mode; /* Actual I/O mode used for parallel I/O (H5D_MPIO_ACTUAL_IO_MODE_NAME) */
    uint32_t mpio_local_no_coll_cause;  /* Local reason for breaking collective I/O
                                           (H5D_MPIO_LOCAL_NO_COLLECTIVE_CAUSE_NAME) */
    uint32_t mpio_global_no_coll_cause; /* Global reason for breaking collective I/O
                                           (H5D_MPIO_GLOBAL_NO_COLLECTIVE_CAUSE_NAME) */
#ifdef H5_HAVE_INSTRUMENTED_LIBRARY
    int mpio_coll_chunk_link_hard;        /* Instrumented "collective chunk link hard" value
                                             (H5D_XFER_COLL_CHUNK_LINK_HARD_NAME) */
    int mpio_coll_chunk_multi_hard;       /* Instrumented "collective chunk multi hard" value
                                             (H5D_XFER_COLL_CHUNK_MULTI_HARD_NAME) */
    int mpio_coll_chunk_link_num_true;    /* Instrumented "collective chunk link num true" value
                                             (H5D_XFER_COLL_CHUNK_LINK_NUM_TRUE_NAME) */
    int mpio_coll_chunk_link_num_false;   /* Instrumented "collective chunk link num false" value
                                             (H5D_XFER_COLL_CHUNK_LINK_NUM_FALSE_NAME) */
    int mpio_coll_chunk_multi_ratio_coll; /* Instrumented "collective chunk multi ratio coll" value
                                             (H5D_XFER_COLL_CHUNK_MULTI_RATIO_COLL_NAME) */
    int mpio_coll_chunk_multi_ratio_ind;  /* Instrumented "collective chunk multi ratio ind" value
                                             (H5D_XFER_COLL_CHUNK_MULTI_RATIO_IND_NAME) */
    bool mpio_coll_rank0_bcast;           /* Instrumented "collective rank 0 broadcast" value
                                             (H5D_XFER_COLL_RANK0_BCAST_NAME) */
#endif                                    /* H5_HAVE_INSTRUMENTED_LIBRARY */
#endif                                    /* H5_HAVE_PARALLEL */
    uint32_t no_selection_io_cause;       /* Reason for not performing selection I/O
                                             (H5D_XFER_NO_SELECTION_IO_CAUSE_NAME) */
    uint32_t actual_selection_io_mode;    /* Actual selection I/O mode used
                                             (H5D_XFER_ACTUAL_SELECTION_IO_MODE_NAME) */
    H5S_t *dset_io_selection;             /* Dataset I/O selection */

    /* Cached LCPL properties */
    H5T_cset_t encoding;         /* Link name character encoding (H5P_STRCRT_CHAR_ENCODING_NAME) */
    unsigned intermediate_group; /* Whether to create intermediate groups (H5L_CRT_INTERMEDIATE_GROUP_NAME) */

    /* Cached LAPL properties */
#ifdef H5_HAVE_PARALLEL
    H5P_coll_md_read_flag_t
        lapl_coll_md_read;    /* Property for collective metadata read (H5_COLL_MD_READ_FLAG_NAME) */
#endif                        /* H5_HAVE_PARALLEL */
    const char *elink_prefix; /* Prefix for external link prefix (H5L_ACS_ELINK_PREFIX_NAME) */
    size_t      nlinks;       /* Number of soft / UD links to traverse (H5L_ACS_NLINKS_NAME) */

    /* Cached OCPL properties */
#ifdef H5O_ENABLE_BAD_MESG_COUNT
    bool bad_mesg_count; /* Write a bad message count to the object header (H5O_CRT_BAD_MESG_COUNT_NAME) */
#endif                   /* H5O_ENABLE_BAD_MESG_COUNT */
    unsigned attr_max_compact;  /* Maximum # of attributes to store in compact form
                                   (H5O_CRT_ATTR_MAX_COMPACT_NAME) */
    unsigned    attr_min_dense; /* Minimum # of attributes to store in dense form */
    uint8_t     ohdr_flags;     /* Object header flags (H5O_CRT_OHDR_FLAGS_NAME) */
    H5O_pline_t pline;          /* Filter pipeline for object creation (H5O_CRT_PLINE_NAME) */

    /* Cached OCPYPL properties */
    H5O_copy_dtype_merge_list_t *comm_dtype_merge_list; /* Committed datatype merge list for object copy
                                                           (H5O_CPY_MERGE_COMM_DT_LIST_NAME) */

    /* Cached DCPL properties */
    bool min_dset_ohdr;  /* Whether to minimize dataset object header (H5D_CRT_MIN_DSET_HDR_SIZE_NAME) */
    H5O_layout_t layout; /* Storage layout for object creation (H5D_CRT_LAYOUT_NAME) */

    /* Cached DAPL properties */
    const char *extfile_prefix; /* Prefix for external file (H5D_ACS_EFILE_PREFIX_NAME) */
    const char *vds_prefix;     /* Prefix for VDS (H5D_ACS_VDS_PREFIX_NAME) */

    /* Cached FAPL properties */
#ifdef H5_HAVE_PARALLEL
    MPI_Comm mpi_comm; /* MPI communicator (H5F_ACS_MPI_COMM_NAME) */
    MPI_Info mpi_info; /* MPI info (H5F_ACS_MPI_INFO_NAME) */
    H5P_coll_md_read_flag_t
         fapl_coll_md_read; /* Property for collective metadata read (H5_COLL_MD_READ_FLAG_NAME) */
    bool coll_md_write;     /* Property for collective metadata write (H5F_ACS_COLL_MD_WRITE_FLAG_NAME) */
#ifdef H5_HAVE_SUBFILING_VFD
    H5FD_subfiling_params_t
        sf_ioc_params; /* Property for subfiling IOC parameters (H5F_ACS_SUBFILING_CONFIG_PROP_NAME) */
#endif                 /* H5_HAVE_SUBFILING_VFD */
#endif                 /* H5_HAVE_PARALLEL */
    H5VL_connector_prop_t
        vol_connector_prop; /* Property for VOL connector ID & info (H5F_ACS_VOL_CONN_NAME) */
    H5FD_driver_prop_t
        driver_prop; /* Property for driver, info & configuration string (H5F_ACS_FILE_DRV_NAME) */
    H5FD_file_image_info_t file_image_info; /* Property for file image info (H5F_ACS_FILE_IMAGE_INFO_NAME) */
    H5F_libver_t
        low_bound; /* low_bound property for H5Pset_libver_bounds() (H5F_ACS_LIBVER_LOW_BOUND_NAME) */
    H5F_libver_t
         high_bound;       /* high_bound property for H5Pset_libver_bounds (H5F_ACS_LIBVER_HIGH_BOUND_NAME) */
    bool use_file_locking; /* Property to use file locking (H5F_ACS_USE_FILE_LOCKING_NAME) */
    bool ignore_disabled_locks;  /* Property to ignore disabled file locks
                                    (H5F_ACS_IGNORE_DISABLED_FILE_LOCKS_NAME) */
    hsize_t  align_bound;        /* alignment property (H5F_ACS_ALIGNMENT_NAME) */
    hsize_t  align_threshold;    /* alignment threshold property (H5F_ACS_ALIGN_THRHD_NAME) */
    bool     clear_status_flags; /* Private property used by h5clear (H5F_ACS_CLEAR_STATUS_FLAGS_NAME) */
    unsigned gc_ref; /* Property for garbage collection of references (H5F_ACS_GARBG_COLCT_REF_NAME) */
    bool     use_mdc_logging; /* Property for metadata cache logging enabled (H5F_ACS_USE_MDC_LOGGING_NAME) */
    char    *mdc_log_location; /* Property for metadata cache log location (H5F_ACS_MDC_LOG_LOCATION_NAME) */
    bool     start_mdc_logging_on_access;       /* Property for starting metadata cache logging on access
                                                   (H5F_ACS_START_MDC_LOG_ON_ACCESS_NAME) */
    unsigned mdc_read_attempts;                 /* Property for metadata cache read attempts
                                                   (H5F_ACS_METADATA_READ_ATTEMPTS_NAME) */
    hsize_t meta_alloc_block_size;              /* Property for metadata allocation block size
                                                   (H5F_ACS_META_BLOCK_SIZE_NAME) */
    H5AC_cache_config_t mdc_init_config;        /* Property for metadata cache initialization configuration
                                                   (H5F_ACS_META_CACHE_INIT_CONFIG_NAME) */
    H5AC_cache_image_config_t mdc_image_config; /* Property for metadata cache image initial configuration
                                                   (H5F_ACS_META_CACHE_INIT_IMAGE_CONFIG_NAME) */
    H5F_object_flush_t
             object_flush_strategy; /* Property for object flush strategy (H5F_ACS_OBJECT_FLUSH_CB_NAME) */
    size_t   pb_size;               /* Property for page buffer size (H5F_ACS_PAGE_BUFFER_SIZE_NAME) */
    unsigned pb_min_meta_perc;      /* Property for minimum metadata percentage
                                       (H5F_ACS_PAGE_BUFFER_MIN_META_PERC_NAME) */
    unsigned
           pb_min_raw_perc; /* Property for minimum raw percentage (H5F_ACS_PAGE_BUFFER_MIN_RAW_PERC_NAME) */
    size_t rdcc_nbytes;     /* Property for size of the raw data cache (H5F_ACS_DATA_CACHE_BYTE_SIZE_NAME) */
    size_t rdcc_nslots;     /* Property for number of slots in the raw data cache
                               (H5F_ACS_DATA_CACHE_NUM_SLOTS_NAME) */
    double   rdcc_w0;  /* Property for chunk cache preemption factor (H5F_ACS_PREEMPT_READ_CHUNKS_NAME) */
    unsigned efc_size; /* Property for size of the external file cache (H5F_ACS_EFC_SIZE_NAME) */
    H5F_close_degree_t close_degree;   /* Property for file close degree (H5F_ACS_CLOSE_DEGREE_NAME) */
    bool               evict_on_close; /* Property for evicting an object's metadata on close
                                          (H5F_ACS_EVICT_ON_CLOSE_FLAG_NAME) */
    uint64_t rfic_flags;       /* Property for relaxed file integrity checks (H5F_ACS_RFIC_FLAGS_NAME) */
    hsize_t  sdata_block_size; /* Property for "small" raw data block size (H5F_ACS_SDATA_BLOCK_SIZE_NAME) */
    size_t   sieve_buf_size;   /* Property for sieve buffer size (H5F_ACS_SIEVE_BUF_SIZE_NAME) */
    bool     null_fsm_addr;    /* Property for null file space map address (H5F_ACS_NULL_FSM_ADDR_NAME) */
    bool     skip_eof_check;   /* Property for skipping EOF check (H5F_ACS_SKIP_EOF_CHECK_NAME) */
    bool    fam_to_single; /* Property for converting family to single file (H5F_ACS_FAMILY_TO_SINGLE_NAME) */
    hsize_t fam_offset;    /* Property for family offset (H5F_ACS_FAMILY_OFFSET_NAME) */
    hsize_t fam_newsize;   /* Property for size of new family file (H5F_ACS_FAMILY_NEWSIZE_NAME) */

    /* Cached VOL settings */
    void *vol_wrap_ctx; /* VOL connector's "wrap context" for creating IDs */

    /*********************************************************************
     * Keep the 'valid' and 'set' flags separate from the actual fields, *
     * which helps to keep the size of the struct down.                  *
     *********************************************************************/

    /* Cached DXPL properties */
    bool max_temp_buf_valid : 1;      /* Whether maximum temporary buffer size is valid */
    bool tconv_buf_valid : 1;         /* Whether temporary conversion buffer is valid */
    bool bkgr_buf_valid : 1;          /* Whether background conversion buffer is valid */
    bool bkgr_buf_type_valid : 1;     /* Whether background buffer type is valid */
    bool btree_split_ratio_valid : 1; /* Whether B-tree split ratios are valid */
    bool vec_size_valid : 1;          /* Whether hyperslab vector is valid */
#ifdef H5_HAVE_PARALLEL
    bool io_xfer_mode_valid : 1;         /* Whether parallel transfer mode is valid */
    bool mpio_coll_opt_valid : 1;        /* Whether parallel transfer option is valid */
    bool mpio_chunk_opt_mode_valid : 1;  /* Whether collective chunk option is valid */
    bool mpio_chunk_opt_num_valid : 1;   /* Whether collective chunk threshold is valid */
    bool mpio_chunk_opt_ratio_valid : 1; /* Whether collective chunk ratio is valid */
#endif                                   /* H5_HAVE_PARALLEL */
    bool err_detect_valid : 1;           /* Whether error detection info is valid */
    bool filter_cb_valid : 1;            /* Whether filter callback function is valid */
    bool data_transform_valid : 1;       /* Whether data transform info is valid */
    bool vl_alloc_info_valid : 1;        /* Whether VL datatype alloc info is valid */
    bool dt_conv_cb_valid : 1;           /* Whether datatype conversion struct is valid */
    bool selection_io_mode_valid : 1;    /* Whether selection I/O mode is valid */
    bool modify_write_buf_valid : 1;     /* Whether the modify_write_buf field is valid */

    /* Return-only DXPL properties to return to application */
#ifdef H5_HAVE_PARALLEL
    bool mpio_actual_chunk_opt_set : 1;    /* Whether chunk optimization mode used for parallel I/O is set */
    bool mpio_actual_io_mode_set : 1;      /* Whether actual I/O mode used for parallel I/O is set */
    bool mpio_local_no_coll_cause_set : 1; /* Whether local reason for breaking collective I/O is set */
    bool mpio_local_no_coll_cause_valid : 1;  /* Whether local reason for breaking collective I/O is valid */
    bool mpio_global_no_coll_cause_set : 1;   /* Whether global reason for breaking collective I/O is set */
    bool mpio_global_no_coll_cause_valid : 1; /* Whether global reason for breaking collective I/O is valid */
#ifdef H5_HAVE_INSTRUMENTED_LIBRARY
    bool mpio_coll_chunk_link_hard_set : 1;  /* Whether instrumented "collective chunk link hard" value is set
                                              */
    bool mpio_coll_chunk_multi_hard_set : 1; /* Whether instrumented "collective chunk multi hard" value is
                                                set */
    bool mpio_coll_chunk_link_num_true_set : 1; /* Whether instrumented "collective chunk link num true" value
                                                   is set */
    bool mpio_coll_chunk_link_num_false_set : 1;   /* Whether instrumented "collective chunk link num false"
                                                      value is set */
    bool mpio_coll_chunk_multi_ratio_coll_set : 1; /* Whether instrumented "collective chunk multi ratio coll"
                                                      value is set */
    bool mpio_coll_chunk_multi_ratio_ind_set : 1;  /* Whether instrumented "collective chunk multi ratio ind"
                                                      value is set */
    bool mpio_coll_rank0_bcast_set : 1; /* Whether instrumented "collective rank 0 broadcast" value is set */
#endif                                  /* H5_HAVE_INSTRUMENTED_LIBRARY */
#endif                                  /* H5_HAVE_PARALLEL */
    bool no_selection_io_cause_set : 1; /* Whether reason for not performing selection I/O is set */
    bool no_selection_io_cause_valid : 1; /* Whether reason for not performing selection I/O is valid */

    bool actual_selection_io_mode_set : 1;   /* Whether actual selection I/O mode is set */
    bool actual_selection_io_mode_valid : 1; /* Whether actual selection I/O mode is valid */
    bool dset_io_selection_valid : 1;        /* Whether dataset I/O selection is valid */

    /* Cached LCPL properties */
    bool encoding_valid : 1;           /* Whether link name character encoding is valid */
    bool intermediate_group_valid : 1; /* Whether create intermediate group flag is valid */

    /* Cached LAPL properties */
#ifdef H5_HAVE_PARALLEL
    bool lapl_coll_md_read_valid : 1; /* Whether collective metadata read property is valid */
#endif                                /* H5_HAVE_PARALLEL */
    bool elink_prefix_valid : 1;      /* Whether the prefix for external link prefix is valid */
    bool nlinks_valid : 1;            /* Whether number of soft / UD links to traverse is valid */

    /* Cached OCPL properties */
#ifdef H5O_ENABLE_BAD_MESG_COUNT
    bool bad_mesg_count_valid : 1; /* Whether the write a bad message count to the object header flag is valid
                                    */
#endif                             /* H5O_ENABLE_BAD_MESG_COUNT */
    bool attr_max_compact_valid : 1; /* Whether the min dense attrs value is valid */
    bool attr_min_dense_valid : 1; /* Whether the min dense attrs value is valid (H5O_CRT_ATTR_MIN_DENSE_NAME)
                                    */
    bool ohdr_flags_valid : 1;     /* Whether the object headers flags are valid */
    bool pline_valid : 1;          /* Whether the filter pipeline for object creation is valid */

    /* Cached OCPYPL properties */
    bool comm_dtype_merge_list_valid : 1; /* Whether the committed datatype merge list for object copy is
                                             valid */

    /* Cached DCPL properties */
    bool min_dset_ohdr_valid : 1; /* Whether minimize dataset object header flag is valid */
    bool layout_valid : 1;        /* Whether the storage layout for object creation is valid */

    /* Cached DAPL properties */
    bool extfile_prefix_valid : 1; /* Whether the prefix for external file is valid */
    bool vds_prefix_valid : 1;     /* Whether the prefix for VDS is valid           */

    /* Cached FAPL properties */
#ifdef H5_HAVE_PARALLEL
    bool mpi_comm_valid : 1;          /* Whether the MPI communicator is valid */
    bool mpi_info_valid : 1;          /* Whether the MPI info object is valid */
    bool fapl_coll_md_read_valid : 1; /* Whether collective metadata read property is valid */
    bool coll_md_write_valid : 1;     /* Whether collective metadata write property is valid */
#ifdef H5_HAVE_SUBFILING_VFD
    bool sf_ioc_params_valid : 1;      /* Whether subfiling IOC parameters property is valid */
#endif                                 /* H5_HAVE_SUBFILING_VFD */
#endif                                 /* H5_HAVE_PARALLEL */
    bool vol_connector_prop_valid : 1; /* Whether property for VOL connector ID & info is valid */
    bool driver_prop_valid : 1;        /* Whether property for driver, info & configuration string is valid */
    bool file_image_info_valid : 1;    /* Whether property for file image info is valid */
    bool low_bound_valid : 1;          /* Whether low_bound property is valid */
    bool high_bound_valid : 1;         /* Whether high_bound property is valid */
    bool use_file_locking_valid : 1;   /* Whether use_file_locking property is valid */
    bool ignore_disabled_locks_valid : 1;       /* Whether ignore_disabled_locks property is valid */
    bool align_bound_valid : 1;                 /* Whether alignment bound property is valid */
    bool align_threshold_valid : 1;             /* Whether alignment threshold property is valid */
    bool clear_status_flags_valid : 1;          /* Whether clear_status_flags property is valid */
    bool gc_ref_valid : 1;                      /* Whether gc_ref property is valid */
    bool use_mdc_logging_valid : 1;             /* Whether use_mdc_logging property is valid */
    bool mdc_log_location_valid : 1;            /* Whether mdc_log_location property is valid */
    bool start_mdc_logging_on_access_valid : 1; /* Whether start_mdc_logging_on_access property is valid */
    bool mdc_read_attempts_valid : 1;           /* Whether metadata cache read attempts property is valid */
    bool meta_alloc_block_size_valid : 1;       /* Whether metadata allocation block size property is valid */
    bool
        mdc_init_config_valid : 1; /* Whether metadata cache initialization configuration property is valid */
    bool
        mdc_image_config_valid : 1; /* Whether metadata cache image initial configuration property is valid */
    bool object_flush_strategy_valid : 1; /* Whether object flush strategy property is valid */
    bool pb_size_valid : 1;               /* Whether page buffer size property is valid */
    bool pb_min_meta_perc_valid : 1;      /* Whether minimum metadata percentage property is valid */
    bool pb_min_raw_perc_valid : 1;       /* Whether minimum raw percentage property is valid */
    bool rdcc_nbytes_valid : 1;           /* Whether raw data cache byte size property is valid */
    bool rdcc_nslots_valid : 1;           /* Whether raw data cache number of slots property is valid */
    bool rdcc_w0_valid : 1;               /* Whether raw data cache preemption factor property is valid */
    bool efc_size_valid : 1;              /* Whether external file cache size property is valid */
    bool close_degree_valid : 1;          /* Whether file close degree property is valid */
    bool evict_on_close_valid : 1;        /* Whether evict on close property is valid */
    bool rfic_flags_valid : 1;            /* Whether relaxed file integrity checks property is valid */
    bool sdata_block_size_valid : 1;      /* Whether "small" raw data block size property is valid */
    bool sieve_buf_size_valid : 1;        /* Whether sieve buffer size property is valid */
    bool null_fsm_addr_valid : 1;         /* Whether null file space map address property is valid */
    bool skip_eof_check_valid : 1;        /* Whether skip EOF check property is valid */
    bool fam_to_single_valid : 1;         /* Whether family to single file property is valid */
    bool fam_offset_valid : 1;            /* Whether family offset property is valid */
    bool fam_newsize_valid : 1;           /* Whether family new size property is valid */

    /* Cached VOL settings */
    bool vol_wrap_ctx_valid : 1; /* Whether VOL connector's "wrap context" for creating IDs is valid */
} H5CX_t;

/* Typedef for nodes on the API context stack */
/* Each entry into the library through an API routine invokes H5CX_push()
 * in a FUNC_ENTER_API* macro, which pushes an H5CX_node_t on the API
 * context [thread-local] stack, after initializing it with default values.
 */
typedef struct H5CX_node_t {
    H5CX_t              ctx;  /* Context for current API call */
    struct H5CX_node_t *next; /* Pointer to previous context, on stack */
} H5CX_node_t;

/*****************************/
/* Library-private Variables */
/*****************************/

/***************************************/
/* Library-private Function Prototypes */
/***************************************/

/* Utility functions */
H5_DLL herr_t H5CX_init_phase2(void);

/* Library private routines */
H5_DLL herr_t H5CX_push(H5CX_node_t *cnode);
H5_DLL herr_t H5CX_pop(bool update_dxpl_props);
H5_DLL bool   H5CX_pushed(void);
H5_DLL bool   H5CX_is_def_dxpl(void);

/* API context state routines */
H5_DLL herr_t H5CX_retrieve_state(H5CX_state_t **api_state);
H5_DLL herr_t H5CX_restore_state(const H5CX_state_t *api_state);
H5_DLL herr_t H5CX_free_state(H5CX_state_t *api_state);

/* "Setter" routines for API context info */
H5_DLL herr_t H5CX_set_cpl(hid_t crtpl_id, const struct H5P_libclass_t *libclass);
H5_DLL herr_t H5CX_set_dxpl(hid_t dxpl_id);
H5_DLL void   H5CX_set_lcpl(hid_t lcpl_id);
H5_DLL herr_t H5CX_set_libver_bounds(H5F_t *f);
H5_DLL herr_t H5CX_set_apl(hid_t *acspl_id, const struct H5P_libclass_t *libclass, hid_t loc_id,
                           bool is_collective);
H5_DLL void   H5CX_set_fapl(hid_t fapl_id);
H5_DLL void   H5CX_set_ocpypl(hid_t ocpypl_id);
H5_DLL herr_t H5CX_set_loc(hid_t loc_id);
H5_DLL herr_t H5CX_set_vol_wrap_ctx(void *wrap_ctx);

/* "Getter" routines for API context info */
H5_DLL hid_t       H5CX_get_fapl(void);
H5_DLL hid_t       H5CX_get_dxpl(void);
H5_DLL hid_t       H5CX_get_lapl(void);
H5_DLL herr_t      H5CX_get_vol_wrap_ctx(void **wrap_ctx);
H5_DLL haddr_t     H5CX_get_tag(void);
H5_DLL H5AC_ring_t H5CX_get_ring(void);
H5_DLL bool        H5CX_get_want_posix_fd(void);
#ifdef H5_HAVE_PARALLEL
H5_DLL bool   H5CX_get_coll_metadata_read(void);
H5_DLL herr_t H5CX_get_mpi_coll_datatypes(MPI_Datatype *btype, MPI_Datatype *ftype);
H5_DLL bool   H5CX_get_mpi_file_flushing(void);
H5_DLL bool   H5CX_get_mpio_rank0_bcast(void);
#ifdef H5_HAVE_SUBFILING_VFD
H5_DLL uint64_t H5CX_get_sf_stub_file_id(void);
#endif /* H5_HAVE_SUBFILING_VFD */
#endif /* H5_HAVE_PARALLEL */

/* "Getter" routines for DXPL properties cached in API context */
H5_DLL herr_t H5CX_get_btree_split_ratios(double split_ratio[3]);
H5_DLL herr_t H5CX_get_max_temp_buf(size_t *max_temp_buf);
H5_DLL herr_t H5CX_get_tconv_buf(void **tconv_buf);
H5_DLL herr_t H5CX_get_bkgr_buf(void **bkgr_buf);
H5_DLL herr_t H5CX_get_bkgr_buf_type(H5T_bkg_t *bkgr_buf_type);
H5_DLL herr_t H5CX_get_vec_size(size_t *vec_size);
#ifdef H5_HAVE_PARALLEL
H5_DLL herr_t H5CX_get_io_xfer_mode(H5FD_mpio_xfer_t *io_xfer_mode);
H5_DLL herr_t H5CX_get_mpio_coll_opt(H5FD_mpio_collective_opt_t *mpio_coll_opt);
H5_DLL herr_t H5CX_get_mpio_local_no_coll_cause(uint32_t *mpio_local_no_coll_cause);
H5_DLL herr_t H5CX_get_mpio_global_no_coll_cause(uint32_t *mpio_global_no_coll_cause);
H5_DLL herr_t H5CX_get_mpio_chunk_opt_mode(H5FD_mpio_chunk_opt_t *mpio_chunk_opt_mode);
H5_DLL herr_t H5CX_get_mpio_chunk_opt_num(unsigned *mpio_chunk_opt_num);
H5_DLL herr_t H5CX_get_mpio_chunk_opt_ratio(unsigned *mpio_chunk_opt_ratio);
#endif /* H5_HAVE_PARALLEL */
H5_DLL herr_t H5CX_get_err_detect(H5Z_EDC_t *err_detect);
H5_DLL herr_t H5CX_get_filter_cb(H5Z_cb_t *filter_cb);
H5_DLL herr_t H5CX_peek_data_transform(H5Z_data_xform_t **data_transform);
H5_DLL herr_t H5CX_get_vlen_alloc_info(H5T_vlen_alloc_info_t *vl_alloc_info);
H5_DLL herr_t H5CX_get_dt_conv_cb(H5T_conv_cb_t *cb_struct);
H5_DLL herr_t H5CX_get_selection_io_mode(H5D_selection_io_mode_t *selection_io_mode);
H5_DLL herr_t H5CX_get_no_selection_io_cause(uint32_t *no_selection_io_cause);
H5_DLL herr_t H5CX_get_actual_selection_io_mode(uint32_t *actual_selection_io_mode);
H5_DLL herr_t H5CX_get_modify_write_buf(bool *modify_write_buf);
H5_DLL herr_t H5CX_get_dset_io_selection(H5S_t **space);

/* "Getter" routines for LCPL properties cached in API context */
H5_DLL herr_t H5CX_get_encoding(H5T_cset_t *encoding);
H5_DLL herr_t H5CX_get_intermediate_group(unsigned *crt_intermed_group);

/* "Getter" routines for LAPL properties cached in API context */
#ifdef H5_HAVE_PARALLEL
H5_DLL herr_t H5CX_get_lapl_coll_md_read(H5P_coll_md_read_flag_t *coll_md_read);
#endif /* H5_HAVE_PARALLEL */
H5_DLL herr_t H5CX_peek_elink_prefix(const char **elink_prefix);
H5_DLL herr_t H5CX_get_nlinks(size_t *nlinks);

/* "Getter" routines for OCPL properties cached in API context */
#ifdef H5O_ENABLE_BAD_MESG_COUNT
H5_DLL herr_t H5CX_get_bad_mesg_count(bool *bad_mesg_count);
#endif /* H5O_ENABLE_BAD_MESG_COUNT */
H5_DLL herr_t H5CX_get_attr_max_compact(unsigned *attr_max_compact);
H5_DLL herr_t H5CX_get_attr_min_dense(unsigned *attr_min_dense);
H5_DLL herr_t H5CX_get_ohdr_flags(uint8_t *ohdr_flags);
H5_DLL herr_t H5CX_get_pline(H5O_pline_t *pline);
H5_DLL herr_t H5CX_peek_pline(H5O_pline_t *pline);

/* "Getter" routines for OCPYPL properties cached in API context */
H5_DLL herr_t H5CX_peek_comm_dtype_merge_list(H5O_copy_dtype_merge_list_t **comm_dtype_merge_list);

/* "Getter" routines for DCPL properties cached in API context */
H5_DLL herr_t H5CX_get_min_dset_hdr(bool *dset_min_ohdr);
H5_DLL herr_t H5CX_get_layout(H5O_layout_t *layout);

/* "Getter" routines for DAPL properties cached in API context */
H5_DLL herr_t H5CX_peek_ext_file_prefix(const char **prefix_extfile);
H5_DLL herr_t H5CX_peek_vds_prefix(const char **prefix_vds);

/* "Getter" routines for FAPL properties cached in API context */
#ifdef H5_HAVE_PARALLEL
H5_DLL herr_t H5CX_get_mpi_comm(MPI_Comm *mpi_comm);
H5_DLL herr_t H5CX_peek_mpi_comm(MPI_Comm *mpi_comm);
H5_DLL herr_t H5CX_get_mpi_info(MPI_Info *mpi_info);
H5_DLL herr_t H5CX_peek_mpi_info(MPI_Info *mpi_info);
H5_DLL herr_t H5CX_get_fapl_coll_md_read(H5P_coll_md_read_flag_t *coll_md_read);
H5_DLL herr_t H5CX_get_coll_md_write(bool *coll_md_write);
#ifdef H5_HAVE_SUBFILING_VFD
H5_DLL herr_t H5CX_get_sf_ioc_params(H5FD_subfiling_params_t *sf_ioc_params);
#endif /* H5_HAVE_SUBFILING_VFD */
#endif /* H5_HAVE_PARALLEL */
H5_DLL herr_t         H5CX_peek_vol_connector_prop(H5VL_connector_prop_t *vol_connector_prop);
H5_DLL herr_t         H5CX_peek_driver_prop(H5FD_driver_prop_t *driver_prop);
H5_DLL H5FD_driver_t *H5CX_peek_driver(void);
H5_DLL const void    *H5CX_peek_driver_info(void);
H5_DLL const char    *H5CX_peek_driver_config_str(void);
H5_DLL herr_t         H5CX_peek_file_image_info(H5FD_file_image_info_t *file_image_info);
H5_DLL herr_t         H5CX_get_libver_bounds(H5F_libver_t *low_bound, H5F_libver_t *high_bound);
H5_DLL herr_t         H5CX_get_use_file_locking(bool *use_file_locking);
H5_DLL herr_t         H5CX_get_ignore_disabled_locks(bool *ignore_disabled_locks);
H5_DLL herr_t         H5CX_get_alignment(hsize_t *align_bound, hsize_t *align_threshold);
H5_DLL herr_t         H5CX_test_get_clear_status_flags(bool *clear_status_flags);
H5_DLL herr_t         H5CX_get_gc_ref(unsigned *gc_ref);
H5_DLL herr_t         H5CX_get_use_mdc_logging(bool *use_mdc_logging);
H5_DLL herr_t         H5CX_peek_mdc_log_location(char **mdc_log_location);
H5_DLL herr_t         H5CX_get_start_mdc_logging_on_access(bool *start_mdc_logging_on_access);
H5_DLL herr_t         H5CX_get_metadata_read_attempts(unsigned *mdc_read_attempts);
H5_DLL herr_t         H5CX_get_meta_alloc_block_size(hsize_t *meta_alloc_block_size);
H5_DLL herr_t         H5CX_get_mdc_init_config(H5AC_cache_config_t *mdc_init_config);
H5_DLL herr_t         H5CX_get_mdc_image_config(H5AC_cache_image_config_t *mdc_image_config);
H5_DLL herr_t         H5CX_get_object_flush_strategy(H5F_object_flush_t *object_flush_strategy);
H5_DLL herr_t         H5CX_get_page_buffer_size(size_t *page_buf_size);
H5_DLL herr_t         H5CX_get_page_buffer_percs(unsigned *min_meta_perc, unsigned *min_raw_perc);
H5_DLL herr_t         H5CX_get_rdcc_info(size_t *nslots, size_t *nbytes, double *w0);
H5_DLL herr_t         H5CX_get_efc_size(unsigned *efc_size);
H5_DLL herr_t         H5CX_get_close_degree(H5F_close_degree_t *close_degree);
H5_DLL herr_t         H5CX_get_evict_on_close(bool *evict_on_close);
H5_DLL herr_t         H5CX_get_rfic_flags(uint64_t *rfic_flags);
H5_DLL herr_t         H5CX_get_sdata_block_size(hsize_t *sdata_block_size);
H5_DLL herr_t         H5CX_get_sieve_buf_size(size_t *sieve_buf_size);
H5_DLL herr_t         H5CX_get_null_fsm_addr(bool *null_fsm_addr);
H5_DLL herr_t         H5CX_get_skip_eof_check(bool *skip_eof_check);
H5_DLL herr_t         H5CX_get_family_to_single(bool *fam_to_single);
H5_DLL herr_t         H5CX_get_family_offset(hsize_t *fam_offset);
H5_DLL herr_t         H5CX_get_family_newsize(hsize_t *fam_newsize);

/* "Setter" routines for API context info */
H5_DLL void H5CX_set_tag(haddr_t tag);
H5_DLL void H5CX_set_ring(H5AC_ring_t ring);
H5_DLL void H5CX_set_want_posix_fd(bool want_posix_fd);
#ifdef H5_HAVE_PARALLEL
H5_DLL void   H5CX_set_coll_metadata_read(bool cmdr);
H5_DLL herr_t H5CX_set_mpi_coll_datatypes(MPI_Datatype btype, MPI_Datatype ftype);
H5_DLL herr_t H5CX_set_mpio_coll_opt(H5FD_mpio_collective_opt_t mpio_coll_opt);
H5_DLL void   H5CX_set_mpi_file_flushing(bool flushing);
H5_DLL void   H5CX_set_mpio_rank0_bcast(bool rank0_bcast);
#ifdef H5_HAVE_SUBFILING_VFD
H5_DLL void H5CX_set_sf_stub_file_id(uint64_t sf_stub_file_id);
#endif /* H5_HAVE_SUBFILING_VFD */
#endif /* H5_HAVE_PARALLEL */

/* "Setter" routines for DXPL properties cached in API context */
#ifdef H5_HAVE_PARALLEL
H5_DLL herr_t H5CX_set_io_xfer_mode(H5FD_mpio_xfer_t io_xfer_mode);
#endif /* H5_HAVE_PARALLEL */
H5_DLL herr_t H5CX_set_vlen_alloc_info(H5MM_allocate_t alloc_func, void *alloc_info, H5MM_free_t free_func,
                                       void *free_info);

/* "Setter" routines for cached DXPL properties that must be returned to application */
H5_DLL void H5CX_set_no_selection_io_cause(uint32_t no_selection_io_cause);
H5_DLL void H5CX_set_actual_selection_io_mode(uint32_t actual_selection_io_mode);
#ifdef H5_HAVE_PARALLEL
H5_DLL void H5CX_set_mpio_actual_chunk_opt(H5D_mpio_actual_chunk_opt_mode_t chunk_opt);
H5_DLL void H5CX_set_mpio_actual_io_mode(H5D_mpio_actual_io_mode_t actual_io_mode);
H5_DLL void H5CX_set_mpio_local_no_coll_cause(uint32_t mpio_local_no_coll_cause);
H5_DLL void H5CX_set_mpio_global_no_coll_cause(uint32_t mpio_global_no_coll_cause);
#ifdef H5_HAVE_INSTRUMENTED_LIBRARY
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_link_hard(int mpio_coll_chunk_link_hard);
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_multi_hard(int mpio_coll_chunk_multi_hard);
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_link_num_true(int mpio_coll_chunk_link_num_true);
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_link_num_false(int mpio_coll_chunk_link_num_false);
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_multi_ratio_coll(int mpio_coll_chunk_multi_ratio_coll);
H5_DLL herr_t H5CX_test_set_mpio_coll_chunk_multi_ratio_ind(int mpio_coll_chunk_multi_ratio_ind);
H5_DLL herr_t H5CX_test_set_mpio_coll_rank0_bcast(bool rank0_bcast);
#endif /* H5_HAVE_INSTRUMENTED_LIBRARY */
#endif /* H5_HAVE_PARALLEL */

/* "Setter" routines for LAPL properties cached in API context */
H5_DLL herr_t H5CX_set_nlinks(size_t nlinks);

/* "Setter" routines for FAPL properties cached in API context */
H5_DLL herr_t H5CX_set_mdc_init_config(H5AC_cache_config_t *mdc_init_config);
H5_DLL herr_t H5CX_set_close_degree(H5F_close_degree_t close_degree);

/* Testing functions */
#ifdef H5CX_TESTING
H5_DLL void H5CX_reset_fapl_test(void);
#endif /* H5CX_TESTING */

#endif /* H5CXprivate_H */
