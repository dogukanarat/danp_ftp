/* danp_ftp_service.h - one line definition */

/* All Rights Reserved */

#ifndef INC_DANP_FTP_SERVICE_H
#define INC_DANP_FTP_SERVICE_H

/* Includes */

#include <stdint.h>
#include <stddef.h>
#include "danp/ftp/danp_ftp.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Configurations */


/* Definitions */


/* Types */

typedef enum danp_ftp_service_fs_mode_e
{
    DANP_FTP_FS_MODE_READ  = 0,
    DANP_FTP_FS_MODE_WRITE,
} danp_ftp_service_fs_mode_t;

typedef uintptr_t danp_ftp_file_handle_t;

typedef danp_ftp_status_t (*danp_ftp_service_fs_open_cb_t)(
    danp_ftp_file_handle_t *file_handle,         /* File handle output */
    const uint8_t *file_id,                      /* File name/id */
    size_t file_id_len,                          /* File name/id len */
    danp_ftp_service_fs_mode_t mode,             /* Open mode */
    void *user_data                              /* User data */
);

typedef danp_ftp_status_t (*danp_ftp_service_fs_close_cb_t)(
    danp_ftp_file_handle_t file_handle,          /* File handle */
    void *user_data                              /* User data */
);

typedef danp_ftp_status_t (*danp_ftp_service_fs_read_cb_t)(
    danp_ftp_file_handle_t file_handle,          /* File handle */
    size_t offset,                               /* Offset in file */
    uint8_t *buffer,                             /* Buffer to read data into */
    uint16_t length,                             /* Length of data to read */
    void *user_data                              /* User data */
);

typedef danp_ftp_status_t (*danp_ftp_service_fs_write_cb_t)(
    danp_ftp_file_handle_t file_handle,          /* File handle */
    size_t offset,                               /* Offset in file */
    const uint8_t *data,                         /* Data to write */
    uint16_t length,                             /* Length of data to write */
    void *user_data                              /* User data */
);

typedef struct danp_ftp_service_fs_api_s
{
    danp_ftp_service_fs_open_cb_t open;
    danp_ftp_service_fs_close_cb_t close;
    danp_ftp_service_fs_read_cb_t read;
    danp_ftp_service_fs_write_cb_t write;
} danp_ftp_service_fs_api_t;

typedef struct danp_ftp_service_config_s
{
    void *user_data;
    danp_ftp_service_fs_api_t fs;
} danp_ftp_service_config_t;

/* External Declarations */

extern int32_t danp_ftp_service_init(const danp_ftp_service_config_t *config);

#ifdef __cplusplus
}
#endif

#endif /* INC_DANP_FTP_SERVICE_H */
