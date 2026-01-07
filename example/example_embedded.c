/* example_embedded.c - Embedded-safe example for danp_ftp */

/* All Rights Reserved */

/*
 * Minimal example that avoids POSIX APIs for embedded/OSAL environments.
 * This is intended to compile on bare-metal or RTOS targets.
 */

/* Includes */

#include "danp_ftp/danp_ftp.h"
#include <stddef.h>
#include <stdint.h>

/* Definitions */

#define OUTPUT_BUFFER_SIZE 32

/* Types */

/* Forward Declarations */

/* Functions */

int main(void)
{
    int32_t multiplyResult = 0;
    char output[OUTPUT_BUFFER_SIZE];

    (void)danp_ftp_get_version();

    (void)danp_ftp_add(10, 20);
    (void)danp_ftp_multiply(3, 7, &multiplyResult);
    (void)danp_ftp_foo("embedded", output, sizeof(output));
    (void)danp_ftp_bar(42);
    (void)danp_ftp_factorial(5);

    (void)multiplyResult;
    (void)output;

    return 0;
}
