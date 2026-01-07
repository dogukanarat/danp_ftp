/* danp_ftp_types.h - Common types and definitions */

/* All Rights Reserved */

#ifndef INC_DANP_FTP_TYPES_H
#define INC_DANP_FTP_TYPES_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes */

#include <stdint.h>

/* Configurations */

/* API visibility */
#if defined(_WIN32) || defined(__CYGWIN__)
#ifdef DANP_FTP_EXPORTS
#define DANP_FTP_API __declspec(dllexport)
#else
#define DANP_FTP_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define DANP_FTP_API __attribute__((visibility("default")))
#else
#define DANP_FTP_API
#endif


/* Definitions */

#define DANP_FTP_MAX_STRING_LEN 256
#define DANP_FTP_VERSION_MAJOR 1
#define DANP_FTP_VERSION_MINOR 0
#define DANP_FTP_VERSION_PATCH 0

/* Types */

/**
 * @brief Return status codes for library functions
 */
typedef enum
{
    DANP_FTP_SUCCESS = 0,     /**< Operation successful */
    DANP_FTP_ERROR = -1,      /**< Generic error */
    DANP_FTP_ERROR_NULL = -2, /**< Null pointer error */
    DANP_FTP_ERROR_INVALID = -3 /**< Invalid parameter error */
} danp_ftp_status_t;

/**
 * @brief Operation result structure
 */
typedef struct
{
    int32_t value;               /**< Result value */
    danp_ftp_status_t status; /**< Operation status */
} danp_ftp_result_t;

/* External Declarations */


#ifdef __cplusplus
}
#endif

#endif /* INC_DANP_FTP_TYPES_H */
