/* danp_ftp.h - one line definition */

/* All Rights Reserved */

#ifndef INC_DANP_FTP_H
#define INC_DANP_FTP_H

/* Includes */

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include "danp/danp.h"

#ifdef __cplusplus
extern "C" {
#endif


/* Configurations */


/* Definitions */

#define DANP_FTP_STATUS_OK                    (0)
#define DANP_FTP_STATUS_ERROR                 (-1)
#define DANP_FTP_STATUS_INVALID_PARAM         (-2)
#define DANP_FTP_STATUS_CONNECTION_FAILED     (-3)
#define DANP_FTP_STATUS_TRANSFER_FAILED       (-4)
#define DANP_FTP_STATUS_FILE_NOT_FOUND        (-5)

#define DANP_FTP_CRC32_POLYNOMIAL             (0xEDB88320U)

/* Types */

typedef ssize_t danp_ftp_status_t;

typedef struct danp_ftp_handle_s danp_ftp_handle_t;

typedef enum danp_ftp_packet_type_e
{
    DANP_FTP_PACKET_TYPE_COMMAND = 0,
    DANP_FTP_PACKET_TYPE_RESPONSE,
    DANP_FTP_PACKET_TYPE_ACK,
    DANP_FTP_PACKET_TYPE_NACK,
    DANP_FTP_PACKET_TYPE_DATA,
} danp_ftp_packet_type_t;

typedef enum danp_ftp_state_e
{
    DANP_FTP_STATE_IDLE = 0,
    DANP_FTP_STATE_CONNECTING,
    DANP_FTP_STATE_TRANSFERRING,
    DANP_FTP_STATE_WAITING_ACK,
    DANP_FTP_STATE_COMPLETE,
    DANP_FTP_STATE_ERROR
} danp_ftp_state_t;

typedef struct danp_ftp_header_s
{
    uint8_t type; /* danp_ftp_packet_type_t */
    uint8_t flags;
    uint16_t sequence_number;
    uint16_t payload_length;
    uint32_t crc;
} danp_ftp_header_t;

typedef struct danp_ftp_transfer_config_s
{
    const uint8_t *file_id;                        /* File name/id */
    size_t file_id_len;                            /* File name/id len */
    uint16_t chunk_size;                           /* Chunk size in bytes */
    uint32_t timeout_ms;                           /* Timeout in milliseconds */
    uint8_t max_retries;                           /* Maximum number of retries */
} danp_ftp_transfer_config_t;

/**
 * @brief Reads a chunk of data from the FTP source.
 *
 * @param data   Pointer to the buffer where the read data will be stored.
 * @param length Capacity of the data buffer.
 * @param more   If not NULL, set to 1 if more data remains after this chunk, else 0.
 *
 * @return
 *   - <0: Error code (see DANP_FTP_STATUS_* for error codes)
 *   -  0: End of file (no more data)
 *   - >0: Number of bytes produced into `data`
 *
 * @note
 *   - `length` specifies the maximum number of bytes to read into `data`.
 *   - If `more` is provided, it indicates whether additional data remains after this chunk.
 */
typedef danp_ftp_status_t (*danp_ftp_source_cb_t)(
    danp_ftp_handle_t *handle,
    size_t offset,
    uint8_t *data,
    uint16_t length,
    uint8_t *more,
    void *user_data
);

/**
 * @brief Processes a chunk of FTP data.
 *
 * @param data   Pointer to the data buffer.
 * @param length Length of the data buffer.
 * @param more  Set to 1 if more data will follow, else 0.
 * @return
 *   - <0: Error code (see DANP_FTP_STATUS_*).
 *   - >0: Number of bytes consumed from `data` (recommended: must equal `length`).
 *
 * @note
 *   - `is_last` should be set to true for the final chunk of the transfer.
 */
typedef danp_ftp_status_t (*danp_ftp_sink_cb_t)(
    danp_ftp_handle_t *handle,
    size_t offset,
    const uint8_t *data,
    uint16_t length,
    uint8_t more,
    void *user_data
);

typedef struct danp_ftp_handle_s
{
    danp_socket_t *socket;
    uint16_t dst_node;
    uint16_t sequence_number;
    danp_ftp_state_t state;
    size_t total_bytes_transferred;
    bool is_initialized;
} danp_ftp_handle_t;

/* External Declarations */

/**
 * @brief Initializes the FTP handle for communication with a destination node.
 *
 * This function sets up the FTP handle for subsequent FTP operations with the specified destination node.
 *
 * @param[out] handle      Pointer to the FTP handle to initialize.
 * @param[in]  dst_node    Destination node ID.
 *
 * @return Status code indicating the result of the initialization.
 */
extern danp_ftp_status_t danp_ftp_init(
    danp_ftp_handle_t *handle,                     /* FTP handle */
    uint16_t dst_node                              /* Destination node ID */
);


/**
 * @brief Deinitializes the FTP handle and releases associated resources.
 *
 * This function cleans up the FTP handle, closing any open connections and freeing allocated resources.
 *
 * @param[in] handle Pointer to the FTP handle to deinitialize.
 *
 * @return None.
 */
extern void danp_ftp_deinit(
    danp_ftp_handle_t *handle                      /* FTP handle */
);

/**
 * @brief Transmits data using the FTP protocol.
 *
 * This function initiates a data transfer using the FTP handle and the provided transfer configuration.
 * The source callback is used to provide data to be transmitted.
 *
 * @param[in]  handle           Pointer to the initialized FTP handle.
 * @param[in]  transfer_config  Pointer to the transfer configuration structure.
 * @param[in]  callback         Source callback function to provide data.
 * @param[in]  user_data        User-defined data passed to the callback.
 *
 * @return Status code indicating the result of the transmission.
 */
extern danp_ftp_status_t danp_ftp_transmit(
    danp_ftp_handle_t *handle,                           /* FTP handle */
    const danp_ftp_transfer_config_t *transfer_config,   /* Transfer configuration */
    danp_ftp_source_cb_t callback,                       /* Source callback */
    void *user_data
);

/**
 * @brief Receives data using the FTP protocol.
 *
 * This function initiates a data reception using the FTP handle and the provided transfer configuration.
 * The sink callback is used to process received data.
 *
 * @param[in]  handle           Pointer to the initialized FTP handle.
 * @param[in]  transfer_config  Pointer to the transfer configuration structure.
 * @param[in]  callback         Sink callback function to process received data.
 * @param[in]  user_data        User-defined data passed to the callback.
 *
 * @return Status code indicating the result of the reception.
 */
extern danp_ftp_status_t danp_ftp_receive(
    danp_ftp_handle_t *handle,                           /* FTP handle */
    const danp_ftp_transfer_config_t *transfer_config,   /* Transfer configuration */
    danp_ftp_sink_cb_t callback,                         /* Sink callback */
    void *user_data
);

#ifdef __cplusplus
}
#endif

#endif /* INC_DANP_FTP_H */
