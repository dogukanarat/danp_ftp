/* danp_ftp.h - Main API header for danp_ftp library */

/* All Rights Reserved */

#ifndef INC_DANP_FTP_H
#define INC_DANP_FTP_H

#ifdef __cplusplus
extern "C"
{
#endif

/* Includes */

#include "danp_ftp_types.h"
#include <stdbool.h>
#include <stddef.h>

/* Configurations */


/* Definitions */


/* Types */


/* External Declarations */

/**
 * @brief Get library version string
 *
 * @return Version string in format "major.minor.patch"
 */
DANP_FTP_API const char *danp_ftp_get_version(void);

/**
 * @brief Add two integers with saturation on overflow/underflow
 *
 * @param a First operand
 * @param b Second operand
 * @return Saturated sum of a and b
 */
DANP_FTP_API int32_t danp_ftp_add(int32_t a, int32_t b);

/**
 * @brief Multiply two integers with error handling
 *
 * @param a First operand
 * @param b Second operand
 * @param result Pointer to store result
 * @return DANP_FTP_SUCCESS on success, error code otherwise
 */
DANP_FTP_API danp_ftp_status_t danp_ftp_multiply(
    int32_t a,
    int32_t b,
    int32_t *result);

/**
 * @brief Example function that processes a string
 *
 * @param input Input string to process
 * @param output Buffer to store processed string
 * @param outputSize Size of output buffer
 * @return DANP_FTP_SUCCESS on success, error code otherwise
 */
DANP_FTP_API danp_ftp_status_t danp_ftp_foo(
    const char *input,
    char *output,
    size_t outputSize);

/**
 * @brief Example function that validates input
 *
 * @param value Value to validate
 * @return true if valid, false otherwise
 */
DANP_FTP_API bool danp_ftp_bar(int32_t value);

/**
 * @brief Compute factorial of a number
 *
 * @param n Input number (must be >= 0 and <= 12)
 * @return Result structure with factorial value and status
 */
DANP_FTP_API danp_ftp_result_t danp_ftp_factorial(int32_t n);

#ifdef __cplusplus
}
#endif

#endif /* INC_DANP_FTP_H */
