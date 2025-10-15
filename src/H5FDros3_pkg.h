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
 * Package-private definitions for HDF5 ROS3 VFD
 */

#ifndef H5FDros3_pkg_H
#define H5FDros3_pkg_H

/* Private headers */
#include "H5private.h" /* Generic Functions                        */

#ifdef H5_HAVE_ROS3_VFD

/* Private headers */
#include "H5FDros3_private.h" /* ROS3 VFD                                 */

/**********
 * MACROS *
 **********/

/*
 * Macro to define level of debugging from the S3comms module.
 *
 *  0 -> no debugging
 *  1 -> minimal debugging information
 *  2 -> trace level debugging
 */
#define S3COMMS_DEBUG 0

/* Defines for the use of HTTP status codes */
#define HTTP_CLIENT_SUCCESS_MIN 200 /* Minimum and maximum values for the 200 class of */
#define HTTP_CLIENT_SUCCESS_MAX 299 /* HTTP client success responses */

#define HTTP_CLIENT_ERROR_MIN 400 /* Minimum and maximum values for the 400 class of */
#define HTTP_CLIENT_ERROR_MAX 499 /* HTTP client error responses */

#define HTTP_SERVER_ERROR_MIN 500 /* Minimum and maximum values for the 500 class of */
#define HTTP_SERVER_ERROR_MAX 599 /* HTTP server error responses */

/* Macros to check for classes of HTTP response */
#define HTTP_CLIENT_SUCCESS(status_code)                                                                     \
    (status_code >= HTTP_CLIENT_SUCCESS_MIN && status_code <= HTTP_CLIENT_SUCCESS_MAX)
#define HTTP_CLIENT_ERROR(status_code)                                                                       \
    (status_code >= HTTP_CLIENT_ERROR_MIN && status_code <= HTTP_CLIENT_ERROR_MAX)
#define HTTP_SERVER_ERROR(status_code)                                                                       \
    (status_code >= HTTP_SERVER_ERROR_MIN && status_code <= HTTP_SERVER_ERROR_MAX)

/*********************
 * PUBLIC STRUCTURES *
 *********************/

/*----------------------------------------------------------------------------
 * Structure: parsed_url_t
 *
 * Represent a URL with easily-accessed pointers to logical elements within.
 * These elements (components) are stored as null-terminated strings (or just
 * NULLs). These components should be allocated for the structure, making the
 * data as safe as possible from modification. If a component is NULL, it is
 * either implicit in or absent from the URL.
 *
 * "http://mybucket.s3.amazonaws.com:8080/somefile.h5?param=value&arg=value"
 *  ^--^   ^-----------------------^ ^--^ ^---------^ ^-------------------^
 * Scheme             Host           Port  Resource        Query/-ies
 *
 *
 * `scheme` (char *)
 *
 *     String representing which protocol is to be expected.
 *     _Must_ be present.
 *     "http", "https", "s3", "ftp", e.g.
 *
 * `host` (char *)
 *
 *     String of host, either domain name, IPv4, or IPv6 format.
 *     _Must_ be present.
 *     "over.rainbow.oz", "192.168.0.1", "[0000:0000:0000:0001]"
 *
 * `port` (char *)
 *
 *     String representation of specified port. Must resolve to a valid unsigned
 *     integer.
 *     "9000", "80"
 *
 * `path` (char *)
 *
 *     Path to resource on host. If not specified, assumes root "/".
 *     "lollipop_guild.wav", "characters/witches/white.dat"
 *
 * `query` (char *)
 *
 *     Single string of all query parameters in url (if any).
 *     "arg1=value1&arg2=value2"
 *
 * `bucket_name` (char *)
 *
 *     Name of S3 bucket to access.
 *
 * `key` (char *)
 *
 *     S3 object key to access.
 *----------------------------------------------------------------------------
 */
typedef struct {
    char *scheme; /* required */
    char *host;   /* required */
    char *port;
    char *path;
    char *query;
    char *bucket_name;
    char *key;
} parsed_url_t;

/*----------------------------------------------------------------------------
 * Structure: s3r_t
 *
 * S3 request structure "handle".
 *
 * Holds persistent information for Amazon S3 requests.
 *
 * Instantiated through `H5FD__s3comms_s3r_open()`, copies data into self.
 *
 * Intended to be reused for operations on a remote object.
 *
 * Cleaned up through `H5FD__s3comms_s3r_close()`.
 *
 * filesize
 *
 *     Size of the associated file, in bytes
 *
 * purl ("parsed url")
 *
 *     Pointer to structure holding the elements of URL for file open
 *
 *     e.g., "http://bucket.aws.com:8080/myfile.dat?q1=v1&q2=v2"
 *     parsed into...
 *     {   scheme:      "http"
 *         host:        "bucket.aws.com"
 *         port:        "8080"
 *         path:        "/myfile.dat"
 *         query:       "q1=v1&q2=v2"
 *         bucket_name: "bucket"
 *         key:         "myfile.dat"
 *     }
 *
 * alternate_purl
 *
 *     Pointer to structure holding the elements of an alternate endpoint
 *     URL when specified
 *
 * aws_region
 *
 *     Pointer to NULL-terminated string, specifying S3 "region"
 *     e.g., "us-east-1".
 *----------------------------------------------------------------------------
 */
typedef struct {
    parsed_url_t *purl;
    parsed_url_t *alternate_purl;
    size_t        filesize;
    char         *aws_region;

    /* Information specific to the backend used for S3 communication */
    void *priv_data;
} s3r_t;

#ifdef __cplusplus
extern "C" {
#endif

H5_DLL herr_t H5FD__s3comms_init(void);
H5_DLL herr_t H5FD__s3comms_term(void);

/* S3 request buffer routines */
H5_DLL s3r_t *H5FD__s3comms_s3r_open(const char *url, const H5FD_ros3_fapl_t *fa, const char *fapl_token,
                                     const char *alt_endpoint);
H5_DLL herr_t H5FD__s3comms_s3r_close(s3r_t *handle);
H5_DLL size_t H5FD__s3comms_s3r_get_filesize(s3r_t *handle);
H5_DLL herr_t H5FD__s3comms_s3r_read(s3r_t *handle, haddr_t offset, size_t len, void *dest, size_t dest_size);

#ifdef __cplusplus
}
#endif

#endif /* H5_HAVE_ROS3_VFD */

#endif /* H5FDros3_pkg_H */
