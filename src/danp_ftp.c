/* danp_ftp.c - FTP protocol implementation over DANP */

/* All Rights Reserved */

/* Includes */

#include "danp/ftp/danp_ftp.h"
#include "danp/danp.h"
#include "danp_debug.h"
#include <string.h>

/* Imports */


/* Definitions */

#define DANP_FTP_PORT                         (CONFIG_DANP_FTP_SERVICE_PORT)
#define DANP_FTP_MAX_PAYLOAD_SIZE             (DANP_MAX_PACKET_SIZE - sizeof(danp_ftp_header_t))
#define DANP_FTP_DEFAULT_CHUNK_SIZE           (64)
#define DANP_FTP_DEFAULT_TIMEOUT_MS           (5000)
#define DANP_FTP_DEFAULT_MAX_RETRIES          (3)

#define DANP_FTP_CMD_REQUEST_READ             (0x01)
#define DANP_FTP_CMD_REQUEST_WRITE            (0x02)
#define DANP_FTP_CMD_ABORT                    (0x03)

#define DANP_FTP_RESP_OK                      (0x00)
#define DANP_FTP_RESP_ERROR                   (0x01)
#define DANP_FTP_RESP_FILE_NOT_FOUND          (0x02)
#define DANP_FTP_RESP_BUSY                    (0x03)

#define DANP_FTP_FLAG_NONE                    (0x00)
#define DANP_FTP_FLAG_LAST_CHUNK              (0x01)
#define DANP_FTP_FLAG_FIRST_CHUNK             (0x02)

/* Types */

typedef struct danp_ftp_message_s
{
    danp_ftp_header_t header;
    uint8_t payload[DANP_FTP_MAX_PAYLOAD_SIZE];
} PACKED danp_ftp_message_t;

/* Forward Declarations */


/* Variables */


/* Functions */

/**
 * @brief Calculate CRC32 for data integrity verification.
 * @param data Pointer to the data buffer.
 * @param length Length of the data.
 * @return Calculated CRC32 value.
 */
static uint32_t danp_ftp_calculate_crc(const uint8_t *data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < length; i++)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; j++)
        {
            if (crc & 1)
            {
                crc = (crc >> 1) ^ DANP_FTP_CRC32_POLYNOMIAL;
            }
            else
            {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

/**
 * @brief Send an FTP protocol message.
 * @param handle Pointer to the FTP handle.
 * @param type Packet type.
 * @param flags Packet flags.
 * @param payload Pointer to the payload data.
 * @param payload_length Length of the payload.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_send_message(
    danp_ftp_handle_t *handle,
    danp_ftp_packet_type_t type,
    uint8_t flags,
    const uint8_t *payload,
    uint16_t payload_length)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_message_t message;
    int32_t send_result;

    for (;;)
    {
        if (!handle || !handle->socket)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        if (payload_length > DANP_FTP_MAX_PAYLOAD_SIZE)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP payload too large: %u", payload_length);
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        memset(&message, 0, sizeof(danp_ftp_message_t));

        message.header.type = (uint8_t)type;
        message.header.flags = flags;
        message.header.sequence_number = handle->sequence_number;
        message.header.payload_length = payload_length;

        if (payload && payload_length > 0)
        {
            memcpy(message.payload, payload, payload_length);
        }

        message.header.crc = danp_ftp_calculate_crc(
            message.payload,
            payload_length);

        send_result = danp_send(
            handle->socket,
            &message,
            sizeof(danp_ftp_header_t) + payload_length);

        if (send_result < 0)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP send failed: %d", send_result);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        danp_log_message(
            DANP_LOG_DEBUG,
            "FTP TX: type=%u flags=0x%02X seq=%u len=%u",
            type,
            flags,
            handle->sequence_number,
            payload_length);

        break;
    }

    return status;
}

/**
 * @brief Receive an FTP protocol message.
 * @param handle Pointer to the FTP handle.
 * @param message Pointer to store the received message.
 * @param timeout_ms Timeout in milliseconds.
 * @return Status code or bytes received.
 */
static danp_ftp_status_t danp_ftp_receive_message(
    danp_ftp_handle_t *handle,
    danp_ftp_message_t *message,
    uint32_t timeout_ms)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    int32_t recv_result;
    uint32_t calculated_crc;

    for (;;)
    {
        if (!handle || !handle->socket || !message)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        memset(message, 0, sizeof(danp_ftp_message_t));

        recv_result = danp_recv(
            handle->socket,
            message,
            sizeof(danp_ftp_message_t),
            timeout_ms);

        if (recv_result < (int32_t)sizeof(danp_ftp_header_t))
        {
            if (recv_result == 0)
            {
                danp_log_message(DANP_LOG_WARN, "FTP receive timeout");
            }
            else
            {
                danp_log_message(DANP_LOG_ERROR, "FTP receive failed: %d", recv_result);
            }
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        calculated_crc = danp_ftp_calculate_crc(
            message->payload,
            message->header.payload_length);

        if (calculated_crc != message->header.crc)
        {
            danp_log_message(
                DANP_LOG_WARN,
                "FTP CRC mismatch: expected=0x%08X got=0x%08X",
                message->header.crc,
                calculated_crc);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        danp_log_message(
            DANP_LOG_DEBUG,
            "FTP RX: type=%u flags=0x%02X seq=%u len=%u",
            message->header.type,
            message->header.flags,
            message->header.sequence_number,
            message->header.payload_length);

        status = (danp_ftp_status_t)message->header.payload_length;

        break;
    }

    return status;
}

/**
 * @brief Wait for an ACK message with expected sequence number.
 * @param handle Pointer to the FTP handle.
 * @param expected_seq Expected sequence number.
 * @param timeout_ms Timeout in milliseconds.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_wait_for_ack(
    danp_ftp_handle_t *handle,
    uint16_t expected_seq,
    uint32_t timeout_ms)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_message_t message;

    for (;;)
    {
        status = danp_ftp_receive_message(handle, &message, timeout_ms);
        if (status < 0)
        {
            break;
        }

        if (message.header.type == DANP_FTP_PACKET_TYPE_ACK)
        {
            if (message.header.sequence_number == expected_seq)
            {
                status = DANP_FTP_STATUS_OK;
                break;
            }
            else
            {
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP ACK seq mismatch: expected=%u got=%u",
                    expected_seq,
                    message.header.sequence_number);
                status = DANP_FTP_STATUS_TRANSFER_FAILED;
                break;
            }
        }
        else if (message.header.type == DANP_FTP_PACKET_TYPE_NACK)
        {
            danp_log_message(DANP_LOG_WARN, "FTP received NACK");
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }
        else
        {
            danp_log_message(
                DANP_LOG_WARN,
                "FTP unexpected packet type: %u",
                message.header.type);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        break;
    }

    return status;
}

/**
 * @brief Initializes the FTP handle for communication with a destination node.
 * @param handle Pointer to the FTP handle to initialize.
 * @param dst_node Destination node ID.
 * @return Status code indicating the result of the initialization.
 */
danp_ftp_status_t danp_ftp_init(
    danp_ftp_handle_t *handle,
    uint16_t dst_node)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_socket_t *sock = NULL;
    int32_t connect_result;

    for (;;)
    {
        if (!handle)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        memset(handle, 0, sizeof(danp_ftp_handle_t));

        sock = danp_socket(DANP_TYPE_STREAM);
        if (!sock)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP failed to create socket");
            status = DANP_FTP_STATUS_ERROR;
            break;
        }

        connect_result = danp_connect(sock, dst_node, DANP_FTP_PORT);
        if (connect_result < 0)
        {
            danp_log_message(
                DANP_LOG_ERROR,
                "FTP failed to connect to node %u: %d",
                dst_node,
                connect_result);
            danp_close(sock);
            status = DANP_FTP_STATUS_CONNECTION_FAILED;
            break;
        }

        handle->socket = sock;
        handle->dst_node = dst_node;
        handle->sequence_number = 0;
        handle->state = DANP_FTP_STATE_IDLE;
        handle->total_bytes_transferred = 0;
        handle->is_initialized = true;

        danp_log_message(
            DANP_LOG_INFO,
            "FTP initialized for node %u",
            dst_node);

        break;
    }

    return status;
}

/**
 * @brief Deinitializes the FTP handle and releases associated resources.
 * @param handle Pointer to the FTP handle to deinitialize.
 */
void danp_ftp_deinit(danp_ftp_handle_t *handle)
{
    for (;;)
    {
        if (!handle)
        {
            break;
        }

        if (!handle->is_initialized)
        {
            break;
        }

        if (handle->socket)
        {
            danp_close(handle->socket);
            handle->socket = NULL;
        }

        handle->is_initialized = false;
        handle->state = DANP_FTP_STATE_IDLE;

        danp_log_message(DANP_LOG_INFO, "FTP handle deinitialized");

        break;
    }
}

/**
 * @brief Transmits data using the FTP protocol.
 * @param handle Pointer to the initialized FTP handle.
 * @param transfer_config Pointer to the transfer configuration structure.
 * @param callback Source callback function to provide data.
 * @param user_data User-defined data passed to the callback.
 * @return Status code indicating the result of the transmission.
 */
danp_ftp_status_t danp_ftp_transmit(
    danp_ftp_handle_t *handle,
    const danp_ftp_transfer_config_t *transfer_config,
    danp_ftp_source_cb_t callback,
    void *user_data)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_message_t response;
    uint8_t command_payload[128];
    size_t command_len;
    uint8_t chunk_buffer[DANP_FTP_MAX_PAYLOAD_SIZE];
    uint16_t chunk_size;
    uint32_t timeout_ms;
    uint8_t max_retries;
    size_t offset = 0;
    uint8_t more = 1;
    uint8_t retries;
    uint8_t flags;

    for (;;)
    {
        if (!handle || !transfer_config || !callback)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        if (!handle->is_initialized)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP handle not initialized");
            status = DANP_FTP_STATUS_ERROR;
            break;
        }

        if (!transfer_config->file_id || transfer_config->file_id_len == 0)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP invalid file ID");
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        chunk_size = transfer_config->chunk_size;
        if (chunk_size == 0)
        {
            chunk_size = DANP_FTP_DEFAULT_CHUNK_SIZE;
        }
        if (chunk_size > DANP_FTP_MAX_PAYLOAD_SIZE)
        {
            chunk_size = DANP_FTP_MAX_PAYLOAD_SIZE;
        }

        timeout_ms = transfer_config->timeout_ms;
        if (timeout_ms == 0)
        {
            timeout_ms = DANP_FTP_DEFAULT_TIMEOUT_MS;
        }

        max_retries = transfer_config->max_retries;
        if (max_retries == 0)
        {
            max_retries = DANP_FTP_DEFAULT_MAX_RETRIES;
        }

        /* Build command payload: [cmd][file_id_len][file_id] */
        command_payload[0] = DANP_FTP_CMD_REQUEST_WRITE;
        command_payload[1] = (uint8_t)transfer_config->file_id_len;
        memcpy(&command_payload[2], transfer_config->file_id, transfer_config->file_id_len);
        command_len = 2 + transfer_config->file_id_len;

        handle->sequence_number = 0;
        handle->state = DANP_FTP_STATE_CONNECTING;

        /* Send write request command */
        status = danp_ftp_send_message(
            handle,
            DANP_FTP_PACKET_TYPE_COMMAND,
            DANP_FTP_FLAG_NONE,
            command_payload,
            (uint16_t)command_len);

        if (status < 0)
        {
            break;
        }

        /* Wait for response */
        status = danp_ftp_receive_message(handle, &response, timeout_ms);
        if (status < 0)
        {
            break;
        }

        if (response.header.type != DANP_FTP_PACKET_TYPE_RESPONSE)
        {
            danp_log_message(
                DANP_LOG_ERROR,
                "FTP unexpected response type: %u",
                response.header.type);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        if (response.payload[0] != DANP_FTP_RESP_OK)
        {
            danp_log_message(
                DANP_LOG_ERROR,
                "FTP write request rejected: %u",
                response.payload[0]);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        handle->state = DANP_FTP_STATE_TRANSFERRING;
        handle->total_bytes_transferred = 0;
        handle->sequence_number++;

        danp_log_message(DANP_LOG_INFO, "FTP transmit started");

        /* Transfer data chunks */
        while (more)
        {
            danp_ftp_status_t read_result = callback(
                handle,
                offset,
                chunk_buffer,
                chunk_size,
                &more,
                user_data);

            if (read_result < 0)
            {
                danp_log_message(
                    DANP_LOG_ERROR,
                    "FTP source callback failed: %d",
                    read_result);
                status = read_result;
                break;
            }

            if (read_result == 0)
            {
                more = 0;
                continue;
            }

            flags = DANP_FTP_FLAG_NONE;
            if (offset == 0)
            {
                flags |= DANP_FTP_FLAG_FIRST_CHUNK;
            }
            if (!more)
            {
                flags |= DANP_FTP_FLAG_LAST_CHUNK;
            }

            retries = 0;
            while (retries < max_retries)
            {
                status = danp_ftp_send_message(
                    handle,
                    DANP_FTP_PACKET_TYPE_DATA,
                    flags,
                    chunk_buffer,
                    (uint16_t)read_result);

                if (status < 0)
                {
                    retries++;
                    continue;
                }

                status = danp_ftp_wait_for_ack(
                    handle,
                    handle->sequence_number,
                    timeout_ms);

                if (status == DANP_FTP_STATUS_OK)
                {
                    break;
                }

                retries++;
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP retry %u/%u for seq %u",
                    retries,
                    max_retries,
                    handle->sequence_number);
            }

            if (retries >= max_retries)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP max retries exceeded");
                status = DANP_FTP_STATUS_TRANSFER_FAILED;
                break;
            }

            offset += read_result;
            handle->total_bytes_transferred = offset;
            handle->sequence_number++;
        }

        if (status < 0)
        {
            handle->state = DANP_FTP_STATE_ERROR;
            break;
        }

        handle->state = DANP_FTP_STATE_COMPLETE;

        danp_log_message(
            DANP_LOG_INFO,
            "FTP transmit complete: %zu bytes",
            handle->total_bytes_transferred);

        status = (danp_ftp_status_t)handle->total_bytes_transferred;

        break;
    }

    return status;
}

/**
 * @brief Receives data using the FTP protocol.
 * @param handle Pointer to the initialized FTP handle.
 * @param transfer_config Pointer to the transfer configuration structure.
 * @param callback Sink callback function to process received data.
 * @param user_data User-defined data passed to the callback.
 * @return Status code indicating the result of the reception.
 */
danp_ftp_status_t danp_ftp_receive(
    danp_ftp_handle_t *handle,
    const danp_ftp_transfer_config_t *transfer_config,
    danp_ftp_sink_cb_t callback,
    void *user_data)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_message_t response;
    danp_ftp_message_t data_msg;
    uint8_t command_payload[128];
    size_t command_len;
    uint32_t timeout_ms;
    size_t offset = 0;
    uint8_t more = 1;

    for (;;)
    {
        if (!handle || !transfer_config || !callback)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        if (!handle->is_initialized)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP handle not initialized");
            status = DANP_FTP_STATUS_ERROR;
            break;
        }

        if (!transfer_config->file_id || transfer_config->file_id_len == 0)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP invalid file ID");
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        timeout_ms = transfer_config->timeout_ms;
        if (timeout_ms == 0)
        {
            timeout_ms = DANP_FTP_DEFAULT_TIMEOUT_MS;
        }

        /* Build command payload: [cmd][file_id_len][file_id] */
        command_payload[0] = DANP_FTP_CMD_REQUEST_READ;
        command_payload[1] = (uint8_t)transfer_config->file_id_len;
        memcpy(&command_payload[2], transfer_config->file_id, transfer_config->file_id_len);
        command_len = 2 + transfer_config->file_id_len;

        handle->sequence_number = 0;
        handle->state = DANP_FTP_STATE_CONNECTING;

        /* Send read request command */
        status = danp_ftp_send_message(
            handle,
            DANP_FTP_PACKET_TYPE_COMMAND,
            DANP_FTP_FLAG_NONE,
            command_payload,
            (uint16_t)command_len);

        if (status < 0)
        {
            break;
        }

        /* Wait for response */
        status = danp_ftp_receive_message(handle, &response, timeout_ms);
        if (status < 0)
        {
            break;
        }

        if (response.header.type != DANP_FTP_PACKET_TYPE_RESPONSE)
        {
            danp_log_message(
                DANP_LOG_ERROR,
                "FTP unexpected response type: %u",
                response.header.type);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        if (response.payload[0] == DANP_FTP_RESP_FILE_NOT_FOUND)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP file not found");
            status = DANP_FTP_STATUS_FILE_NOT_FOUND;
            break;
        }

        if (response.payload[0] != DANP_FTP_RESP_OK)
        {
            danp_log_message(
                DANP_LOG_ERROR,
                "FTP read request rejected: %u",
                response.payload[0]);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        handle->state = DANP_FTP_STATE_TRANSFERRING;
        handle->total_bytes_transferred = 0;
        handle->sequence_number++;

        danp_log_message(DANP_LOG_INFO, "FTP receive started");

        /* Receive data chunks */
        while (more)
        {
            status = danp_ftp_receive_message(handle, &data_msg, timeout_ms);
            if (status < 0)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP receive data failed");
                break;
            }

            if (data_msg.header.type != DANP_FTP_PACKET_TYPE_DATA)
            {
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP unexpected packet type: %u",
                    data_msg.header.type);

                /* Send NACK */
                danp_ftp_send_message(
                    handle,
                    DANP_FTP_PACKET_TYPE_NACK,
                    DANP_FTP_FLAG_NONE,
                    NULL,
                    0);
                continue;
            }

            if (data_msg.header.sequence_number != handle->sequence_number)
            {
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP seq mismatch: expected=%u got=%u",
                    handle->sequence_number,
                    data_msg.header.sequence_number);

                /* Send NACK */
                danp_ftp_send_message(
                    handle,
                    DANP_FTP_PACKET_TYPE_NACK,
                    DANP_FTP_FLAG_NONE,
                    NULL,
                    0);
                continue;
            }

            more = (data_msg.header.flags & DANP_FTP_FLAG_LAST_CHUNK) ? 0 : 1;

            /* Process received data */
            danp_ftp_status_t sink_result = callback(
                handle,
                offset,
                data_msg.payload,
                data_msg.header.payload_length,
                more,
                user_data);

            if (sink_result < 0)
            {
                danp_log_message(
                    DANP_LOG_ERROR,
                    "FTP sink callback failed: %d",
                    sink_result);
                status = sink_result;
                break;
            }

            /* Send ACK */
            status = danp_ftp_send_message(
                handle,
                DANP_FTP_PACKET_TYPE_ACK,
                DANP_FTP_FLAG_NONE,
                NULL,
                0);

            if (status < 0)
            {
                break;
            }

            offset += data_msg.header.payload_length;
            handle->total_bytes_transferred = offset;
            handle->sequence_number++;
        }

        if (status < 0)
        {
            handle->state = DANP_FTP_STATE_ERROR;
            break;
        }

        handle->state = DANP_FTP_STATE_COMPLETE;

        danp_log_message(
            DANP_LOG_INFO,
            "FTP receive complete: %zu bytes",
            handle->total_bytes_transferred);

        status = (danp_ftp_status_t)handle->total_bytes_transferred;

        break;
    }

    return status;
}
