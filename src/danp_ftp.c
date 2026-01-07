/* danp_ftp.c - Core implementation of danp_ftp library */

/* All Rights Reserved */

/* Includes */

#include "danp_ftp/danp_ftp.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

/* Imports */


/* Definitions */

#define MAX_FACTORIAL_INPUT 12

/* Version string helpers */
#define DANP_FTP_STRINGIFY_HELPER(x) #x
#define DANP_FTP_STRINGIFY(x) DANP_FTP_STRINGIFY_HELPER(x)

static const char VersionString[] = DANP_FTP_STRINGIFY(DANP_FTP_VERSION_MAJOR) "."
                                   DANP_FTP_STRINGIFY(DANP_FTP_VERSION_MINOR) "."
                                   DANP_FTP_STRINGIFY(DANP_FTP_VERSION_PATCH);

/* Types */


/* Forward Declarations */


/* Variables */


/* Functions */

const char *danp_ftp_get_version(void)
{
    return VersionString;
}

int32_t danp_ftp_add(int32_t a, int32_t b)
{
    int64_t sum = (int64_t)a + (int64_t)b;

    if (sum > INT32_MAX)
    {
        return INT32_MAX;
    }

    if (sum < INT32_MIN)
    {
        return INT32_MIN;
    }

    return (int32_t)sum;
}

danp_ftp_status_t danp_ftp_multiply(
    int32_t a,
    int32_t b,
    int32_t *result)
{
    /* Validate input parameter */
    if (result == NULL)
    {
        return DANP_FTP_ERROR_NULL;
    }

    /* Perform multiplication */
    int64_t product = (int64_t)a * (int64_t)b;
    if (product > INT32_MAX || product < INT32_MIN)
    {
        return DANP_FTP_ERROR_INVALID;
    }

    *result = (int32_t)product;

    return DANP_FTP_SUCCESS;
}

danp_ftp_status_t danp_ftp_foo(
    const char *input,
    char *output,
    size_t outputSize)
{
    /* Validate input parameters */
    if (input == NULL || output == NULL)
    {
        return DANP_FTP_ERROR_NULL;
    }

    if (outputSize == 0)
    {
        return DANP_FTP_ERROR_INVALID;
    }

    /* Get input length */
    size_t inputLen = strlen(input);

    const char prefix[] = "Processed: ";
    size_t prefixLen = sizeof(prefix) - 1;

    /* Check if output buffer is large enough */
    if (outputSize <= prefixLen || inputLen > (outputSize - prefixLen - 1))
    {
        return DANP_FTP_ERROR_INVALID;
    }

    /* Process the string by adding a prefix */
    int written = snprintf(output, outputSize, "%s%s", prefix, input);
    if (written < 0 || (size_t)written >= outputSize)
    {
        return DANP_FTP_ERROR_INVALID;
    }

    return DANP_FTP_SUCCESS;
}

bool danp_ftp_bar(int32_t value)
{
    /* Example validation: value must be in range [0, 100] */
    return (value >= 0 && value <= 100);
}

danp_ftp_result_t danp_ftp_factorial(int32_t n)
{
    danp_ftp_result_t result;

    /* Validate input */
    if (n < 0)
    {
        result.value = 0;
        result.status = DANP_FTP_ERROR_INVALID;
        return result;
    }

    /* Check for overflow protection */
    if (n > MAX_FACTORIAL_INPUT)
    {
        result.value = 0;
        result.status = DANP_FTP_ERROR_INVALID;
        return result;
    }

    /* Calculate factorial */
    int32_t factorial = 1;
    for (int32_t i = 2; i <= n; i++)
    {
        factorial *= i;
    }

    result.value = factorial;
    result.status = DANP_FTP_SUCCESS;

    return result;
}
