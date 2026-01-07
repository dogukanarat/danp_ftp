/* danp_ftp_service.c - FTP service implementation over DANP */

/* All Rights Reserved */

/* Includes */

#include "osal/osal_thread.h"
#include "osal/osal_memory.h"
#include "danp/ftp/danp_ftp_service.h"
#include "danp/ftp/danp_ftp.h"
#include "danp/danp.h"
#include "danp_debug.h"
#include <string.h>

/* Imports */


/* Definitions */

#define DANP_FTP_SERVICE_PORT                 (CONFIG_DANP_FTP_SERVICE_PORT)
#define DANP_FTP_SERVICE_STACK_SIZE           (1024 * 4)
#define DANP_FTP_SERVICE_BACKLOG              (5)
#define DANP_FTP_SERVICE_TIMEOUT_MS           (30000)
#define DANP_FTP_SERVICE_MAX_CLIENTS          (4)

#define DANP_FTP_MAX_PAYLOAD_SIZE             (DANP_MAX_PACKET_SIZE - sizeof(danp_ftp_header_t))

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

typedef struct danp_ftp_service_context_s
{
    danp_ftp_service_config_t config;
    danp_socket_t *listen_socket;
    osal_thread_handle_t service_thread;
    bool is_running;
    bool is_initialized;
} danp_ftp_service_context_t;

typedef struct danp_ftp_client_context_s
{
    danp_socket_t *socket;
    danp_ftp_service_context_t *service;
    uint16_t sequence_number;
    danp_ftp_file_handle_t file_handle;
    bool file_open;
} danp_ftp_client_context_t;

/* Forward Declarations */

static void danp_ftp_service_thread(void *arg);
static void danp_ftp_client_handler_thread(void *arg);
static uint32_t danp_ftp_service_calculate_crc(const uint8_t *data, size_t length);
static danp_ftp_status_t danp_ftp_service_send_message(
    danp_ftp_client_context_t *ctx,
    danp_ftp_packet_type_t type,
    uint8_t flags,
    const uint8_t *payload,
    uint16_t payload_length);
static danp_ftp_status_t danp_ftp_service_receive_message(
    danp_ftp_client_context_t *ctx,
    danp_ftp_message_t *message,
    uint32_t timeout_ms);
static danp_ftp_status_t danp_ftp_service_handle_read_request(
    danp_ftp_client_context_t *ctx,
    const uint8_t *file_id,
    size_t file_id_len);
static danp_ftp_status_t danp_ftp_service_handle_write_request(
    danp_ftp_client_context_t *ctx,
    const uint8_t *file_id,
    size_t file_id_len);

/* Variables */

static danp_ftp_service_context_t ftp_service_ctx;

/* Functions */

/**
 * @brief Calculate CRC32 for data integrity verification.
 * @param data Pointer to the data buffer.
 * @param length Length of the data.
 * @return Calculated CRC32 value.
 */
static uint32_t danp_ftp_service_calculate_crc(const uint8_t *data, size_t length)
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
 * @brief Send an FTP protocol message from service.
 * @param ctx Pointer to the client context.
 * @param type Packet type.
 * @param flags Packet flags.
 * @param payload Pointer to the payload data.
 * @param payload_length Length of the payload.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_service_send_message(
    danp_ftp_client_context_t *ctx,
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
        if (!ctx || !ctx->socket)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        if (payload_length > DANP_FTP_MAX_PAYLOAD_SIZE)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service payload too large: %u", payload_length);
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        memset(&message, 0, sizeof(danp_ftp_message_t));

        message.header.type = (uint8_t)type;
        message.header.flags = flags;
        message.header.sequence_number = ctx->sequence_number;
        message.header.payload_length = payload_length;

        if (payload && payload_length > 0)
        {
            memcpy(message.payload, payload, payload_length);
        }

        message.header.crc = danp_ftp_service_calculate_crc(
            message.payload,
            payload_length);

        send_result = danp_send(
            ctx->socket,
            &message,
            sizeof(danp_ftp_header_t) + payload_length);

        if (send_result < 0)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service send failed: %d", send_result);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        danp_log_message(
            DANP_LOG_DEBUG,
            "FTP SVC TX: type=%u flags=0x%02X seq=%u len=%u",
            type,
            flags,
            ctx->sequence_number,
            payload_length);

        break;
    }

    return status;
}

/**
 * @brief Receive an FTP protocol message.
 * @param ctx Pointer to the client context.
 * @param message Pointer to store the received message.
 * @param timeout_ms Timeout in milliseconds.
 * @return Status code or bytes received.
 */
static danp_ftp_status_t danp_ftp_service_receive_message(
    danp_ftp_client_context_t *ctx,
    danp_ftp_message_t *message,
    uint32_t timeout_ms)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    int32_t recv_result;
    uint32_t calculated_crc;

    for (;;)
    {
        if (!ctx || !ctx->socket || !message)
        {
            status = DANP_FTP_STATUS_INVALID_PARAM;
            break;
        }

        memset(message, 0, sizeof(danp_ftp_message_t));

        recv_result = danp_recv(
            ctx->socket,
            message,
            sizeof(danp_ftp_message_t),
            timeout_ms);

        if (recv_result < (int32_t)sizeof(danp_ftp_header_t))
        {
            if (recv_result == 0)
            {
                danp_log_message(DANP_LOG_WARN, "FTP service receive timeout");
            }
            else
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service receive failed: %d", recv_result);
            }
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        calculated_crc = danp_ftp_service_calculate_crc(
            message->payload,
            message->header.payload_length);

        if (calculated_crc != message->header.crc)
        {
            danp_log_message(
                DANP_LOG_WARN,
                "FTP service CRC mismatch: expected=0x%08X got=0x%08X",
                message->header.crc,
                calculated_crc);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        danp_log_message(
            DANP_LOG_DEBUG,
            "FTP SVC RX: type=%u flags=0x%02X seq=%u len=%u",
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
 * @brief Wait for ACK from client.
 * @param ctx Pointer to the client context.
 * @param expected_seq Expected sequence number.
 * @param timeout_ms Timeout in milliseconds.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_service_wait_for_ack(
    danp_ftp_client_context_t *ctx,
    uint16_t expected_seq,
    uint32_t timeout_ms)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_message_t message;

    for (;;)
    {
        status = danp_ftp_service_receive_message(ctx, &message, timeout_ms);
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
                    "FTP service ACK seq mismatch: expected=%u got=%u",
                    expected_seq,
                    message.header.sequence_number);
                status = DANP_FTP_STATUS_TRANSFER_FAILED;
                break;
            }
        }
        else if (message.header.type == DANP_FTP_PACKET_TYPE_NACK)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service received NACK");
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }
        else
        {
            danp_log_message(
                DANP_LOG_WARN,
                "FTP service unexpected packet type: %u",
                message.header.type);
            status = DANP_FTP_STATUS_TRANSFER_FAILED;
            break;
        }

        break;
    }

    return status;
}

/**
 * @brief Handle a file read request from client.
 * @param ctx Pointer to the client context.
 * @param file_id File identifier.
 * @param file_id_len Length of file identifier.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_service_handle_read_request(
    danp_ftp_client_context_t *ctx,
    const uint8_t *file_id,
    size_t file_id_len)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_service_context_t *svc = ctx->service;
    danp_ftp_file_handle_t file_handle = 0;
    uint8_t response_payload[1];
    uint8_t data_buffer[DANP_FTP_MAX_PAYLOAD_SIZE];
    size_t offset = 0;
    uint8_t flags;
    bool more = true;

    for (;;)
    {
        danp_log_message(
            DANP_LOG_INFO,
            "FTP service handling read request for file (len=%zu)",
            file_id_len);

        /* Open file for reading */
        status = svc->config.fs.open(
            &file_handle,
            file_id,
            file_id_len,
            DANP_FTP_FS_MODE_READ,
            svc->config.user_data);

        if (status < 0)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service file open failed: %d", status);

            if (status == DANP_FTP_STATUS_FILE_NOT_FOUND)
            {
                response_payload[0] = DANP_FTP_RESP_FILE_NOT_FOUND;
            }
            else
            {
                response_payload[0] = DANP_FTP_RESP_ERROR;
            }

            danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_RESPONSE,
                DANP_FTP_FLAG_NONE,
                response_payload,
                1);
            break;
        }

        ctx->file_handle = file_handle;
        ctx->file_open = true;

        /* Send OK response */
        response_payload[0] = DANP_FTP_RESP_OK;
        status = danp_ftp_service_send_message(
            ctx,
            DANP_FTP_PACKET_TYPE_RESPONSE,
            DANP_FTP_FLAG_NONE,
            response_payload,
            1);

        if (status < 0)
        {
            break;
        }

        ctx->sequence_number++;

        /* Send file data in chunks */
        while (more)
        {
            danp_ftp_status_t read_result = svc->config.fs.read(
                file_handle,
                offset,
                data_buffer,
                DANP_FTP_MAX_PAYLOAD_SIZE,
                svc->config.user_data);

            if (read_result < 0)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service file read failed: %d", read_result);
                status = read_result;
                break;
            }

            if (read_result == 0)
            {
                more = false;
                continue;
            }

            /* Check if this is the last chunk */
            danp_ftp_status_t peek_result = svc->config.fs.read(
                file_handle,
                offset + read_result,
                data_buffer + read_result,
                1,
                svc->config.user_data);

            if (peek_result <= 0)
            {
                more = false;
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

            status = danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_DATA,
                flags,
                data_buffer,
                (uint16_t)read_result);

            if (status < 0)
            {
                break;
            }

            /* Wait for ACK */
            status = danp_ftp_service_wait_for_ack(
                ctx,
                ctx->sequence_number,
                DANP_FTP_SERVICE_TIMEOUT_MS);

            if (status < 0)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service ACK timeout");
                break;
            }

            offset += read_result;
            ctx->sequence_number++;
        }

        /* Close file */
        svc->config.fs.close(file_handle, svc->config.user_data);
        ctx->file_open = false;

        if (status >= 0)
        {
            danp_log_message(
                DANP_LOG_INFO,
                "FTP service read complete: %zu bytes",
                offset);
            status = (danp_ftp_status_t)offset;
        }

        break;
    }

    return status;
}

/**
 * @brief Handle a file write request from client.
 * @param ctx Pointer to the client context.
 * @param file_id File identifier.
 * @param file_id_len Length of file identifier.
 * @return Status code.
 */
static danp_ftp_status_t danp_ftp_service_handle_write_request(
    danp_ftp_client_context_t *ctx,
    const uint8_t *file_id,
    size_t file_id_len)
{
    danp_ftp_status_t status = DANP_FTP_STATUS_OK;
    danp_ftp_service_context_t *svc = ctx->service;
    danp_ftp_file_handle_t file_handle = 0;
    danp_ftp_message_t data_msg;
    uint8_t response_payload[1];
    size_t offset = 0;
    bool more = true;

    for (;;)
    {
        danp_log_message(
            DANP_LOG_INFO,
            "FTP service handling write request for file (len=%zu)",
            file_id_len);

        /* Open file for writing */
        status = svc->config.fs.open(
            &file_handle,
            file_id,
            file_id_len,
            DANP_FTP_FS_MODE_WRITE,
            svc->config.user_data);

        if (status < 0)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service file open failed: %d", status);
            response_payload[0] = DANP_FTP_RESP_ERROR;

            danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_RESPONSE,
                DANP_FTP_FLAG_NONE,
                response_payload,
                1);
            break;
        }

        ctx->file_handle = file_handle;
        ctx->file_open = true;

        /* Send OK response */
        response_payload[0] = DANP_FTP_RESP_OK;
        status = danp_ftp_service_send_message(
            ctx,
            DANP_FTP_PACKET_TYPE_RESPONSE,
            DANP_FTP_FLAG_NONE,
            response_payload,
            1);

        if (status < 0)
        {
            break;
        }

        ctx->sequence_number++;

        /* Receive file data chunks */
        while (more)
        {
            status = danp_ftp_service_receive_message(
                ctx,
                &data_msg,
                DANP_FTP_SERVICE_TIMEOUT_MS);

            if (status < 0)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service receive data failed");
                break;
            }

            if (data_msg.header.type != DANP_FTP_PACKET_TYPE_DATA)
            {
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP service unexpected packet type: %u",
                    data_msg.header.type);

                /* Send NACK */
                danp_ftp_service_send_message(
                    ctx,
                    DANP_FTP_PACKET_TYPE_NACK,
                    DANP_FTP_FLAG_NONE,
                    NULL,
                    0);
                continue;
            }

            if (data_msg.header.sequence_number != ctx->sequence_number)
            {
                danp_log_message(
                    DANP_LOG_WARN,
                    "FTP service seq mismatch: expected=%u got=%u",
                    ctx->sequence_number,
                    data_msg.header.sequence_number);

                /* Send NACK */
                danp_ftp_service_send_message(
                    ctx,
                    DANP_FTP_PACKET_TYPE_NACK,
                    DANP_FTP_FLAG_NONE,
                    NULL,
                    0);
                continue;
            }

            /* Write data to file */
            danp_ftp_status_t write_result = svc->config.fs.write(
                file_handle,
                offset,
                data_msg.payload,
                data_msg.header.payload_length,
                svc->config.user_data);

            if (write_result < 0)
            {
                danp_log_message(
                    DANP_LOG_ERROR,
                    "FTP service file write failed: %d",
                    write_result);
                status = write_result;

                /* Send NACK */
                danp_ftp_service_send_message(
                    ctx,
                    DANP_FTP_PACKET_TYPE_NACK,
                    DANP_FTP_FLAG_NONE,
                    NULL,
                    0);
                break;
            }

            /* Check if this is the last chunk */
            if (data_msg.header.flags & DANP_FTP_FLAG_LAST_CHUNK)
            {
                more = false;
            }

            /* Send ACK */
            status = danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_ACK,
                DANP_FTP_FLAG_NONE,
                NULL,
                0);

            if (status < 0)
            {
                break;
            }

            offset += data_msg.header.payload_length;
            ctx->sequence_number++;
        }

        /* Close file */
        svc->config.fs.close(file_handle, svc->config.user_data);
        ctx->file_open = false;

        if (status >= 0)
        {
            danp_log_message(
                DANP_LOG_INFO,
                "FTP service write complete: %zu bytes",
                offset);
            status = (danp_ftp_status_t)offset;
        }

        break;
    }

    return status;
}

/**
 * @brief Client handler thread function.
 * @param arg Pointer to client context.
 */
static void danp_ftp_client_handler_thread(void *arg)
{
    danp_ftp_client_context_t *ctx = (danp_ftp_client_context_t *)arg;
    danp_ftp_message_t message;
    danp_ftp_status_t status;
    uint8_t command;
    uint8_t file_id_len;
    const uint8_t *file_id;
    uint8_t response_payload[1];

    for (;;)
    {
        if (!ctx || !ctx->socket)
        {
            break;
        }

        danp_log_message(
            DANP_LOG_INFO,
            "FTP service client handler started for node %u",
            ctx->socket->remote_node);

        /* Wait for command */
        status = danp_ftp_service_receive_message(
            ctx,
            &message,
            DANP_FTP_SERVICE_TIMEOUT_MS);

        if (status < 0)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service command receive failed");
            break;
        }

        if (message.header.type != DANP_FTP_PACKET_TYPE_COMMAND)
        {
            danp_log_message(
                DANP_LOG_WARN,
                "FTP service expected command, got type: %u",
                message.header.type);
            break;
        }

        if (message.header.payload_length < 2)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service command payload too short");
            response_payload[0] = DANP_FTP_RESP_ERROR;
            danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_RESPONSE,
                DANP_FTP_FLAG_NONE,
                response_payload,
                1);
            break;
        }

        command = message.payload[0];
        file_id_len = message.payload[1];
        file_id = &message.payload[2];

        if (file_id_len + 2 > message.header.payload_length)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service invalid file_id_len");
            response_payload[0] = DANP_FTP_RESP_ERROR;
            danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_RESPONSE,
                DANP_FTP_FLAG_NONE,
                response_payload,
                1);
            break;
        }

        switch (command)
        {
        case DANP_FTP_CMD_REQUEST_READ:
            danp_ftp_service_handle_read_request(ctx, file_id, file_id_len);
            break;

        case DANP_FTP_CMD_REQUEST_WRITE:
            danp_ftp_service_handle_write_request(ctx, file_id, file_id_len);
            break;

        case DANP_FTP_CMD_ABORT:
            danp_log_message(DANP_LOG_INFO, "FTP service received abort command");
            break;

        default:
            danp_log_message(DANP_LOG_WARN, "FTP service unknown command: %u", command);
            response_payload[0] = DANP_FTP_RESP_ERROR;
            danp_ftp_service_send_message(
                ctx,
                DANP_FTP_PACKET_TYPE_RESPONSE,
                DANP_FTP_FLAG_NONE,
                response_payload,
                1);
            break;
        }

        break;
    }

    /* Cleanup */
    if (ctx)
    {
        if (ctx->file_open && ctx->service)
        {
            ctx->service->config.fs.close(
                ctx->file_handle,
                ctx->service->config.user_data);
        }

        if (ctx->socket)
        {
            danp_close(ctx->socket);
        }

        danp_log_message(DANP_LOG_INFO, "FTP service client handler terminated");

        /* Free client context */
        memset(ctx, 0, sizeof(danp_ftp_client_context_t));
    }
}

/**
 * @brief Main service thread function.
 * @param arg Pointer to service context.
 */
static void danp_ftp_service_thread(void *arg)
{
    danp_ftp_service_context_t *svc = (danp_ftp_service_context_t *)arg;
    danp_socket_t *client_socket = NULL;
    danp_ftp_client_context_t *client_ctx = NULL;
    osal_thread_handle_t client_thread = NULL;
    osal_thread_attr_t client_thread_attr = {
        .name = "ftpClient",
        .stack_size = DANP_FTP_SERVICE_STACK_SIZE,
        .stack_mem = NULL,
        .priority = OSAL_THREAD_PRIORITY_NORMAL,
        .cb_mem = NULL,
        .cb_size = 0,
    };

    for (;;)
    {
        if (!svc)
        {
            break;
        }

        danp_log_message(DANP_LOG_INFO, "FTP service thread started");

        while (svc->is_running)
        {
            client_socket = danp_accept(svc->listen_socket, 1000);
            if (!client_socket)
            {
                continue;
            }

            danp_log_message(
                DANP_LOG_INFO,
                "FTP service accepted connection from node %u",
                client_socket->remote_node);

            /* Allocate client context */
            client_ctx = (danp_ftp_client_context_t *)osal_memory_alloc(
                sizeof(danp_ftp_client_context_t));

            if (!client_ctx)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service failed to allocate client context");
                danp_close(client_socket);
                continue;
            }

            memset(client_ctx, 0, sizeof(danp_ftp_client_context_t));
            client_ctx->socket = client_socket;
            client_ctx->service = svc;
            client_ctx->sequence_number = 0;
            client_ctx->file_open = false;

            /* Create client handler thread */
            client_thread = osal_thread_create(
                danp_ftp_client_handler_thread,
                client_ctx,
                &client_thread_attr);

            if (!client_thread)
            {
                danp_log_message(DANP_LOG_ERROR, "FTP service failed to create client thread");
                danp_close(client_socket);
                osal_memory_free(client_ctx);
                continue;
            }
        }

        break;
    }

    danp_log_message(DANP_LOG_INFO, "FTP service thread terminated");
}

/**
 * @brief Initialize the FTP service.
 * @param config Pointer to the service configuration.
 * @return 0 on success, negative on error.
 */
int32_t danp_ftp_service_init(const danp_ftp_service_config_t *config)
{
    int32_t ret = 0;
    danp_socket_t *sock = NULL;
    int32_t bind_result;
    osal_thread_handle_t thread_handle = NULL;
    osal_thread_attr_t thread_attr = {
        .name = "ftpService",
        .stack_size = DANP_FTP_SERVICE_STACK_SIZE,
        .stack_mem = NULL,
        .priority = OSAL_THREAD_PRIORITY_NORMAL,
        .cb_mem = NULL,
        .cb_size = 0,
    };

    for (;;)
    {
        if (!config)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service config is NULL");
            ret = -1;
            break;
        }

        if (!config->fs.open || !config->fs.close ||
            !config->fs.read || !config->fs.write)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service filesystem callbacks incomplete");
            ret = -1;
            break;
        }

        if (ftp_service_ctx.is_initialized)
        {
            danp_log_message(DANP_LOG_WARN, "FTP service already initialized");
            ret = -1;
            break;
        }

        memset(&ftp_service_ctx, 0, sizeof(danp_ftp_service_context_t));
        memcpy(&ftp_service_ctx.config, config, sizeof(danp_ftp_service_config_t));

        /* Create listening socket */
        sock = danp_socket(DANP_TYPE_STREAM);
        if (!sock)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service failed to create socket");
            ret = -1;
            break;
        }

        bind_result = danp_bind(sock, DANP_FTP_SERVICE_PORT);
        if (bind_result < 0)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service failed to bind to port %u", DANP_FTP_SERVICE_PORT);
            danp_close(sock);
            ret = -1;
            break;
        }

        danp_listen(sock, DANP_FTP_SERVICE_BACKLOG);

        ftp_service_ctx.listen_socket = sock;
        ftp_service_ctx.is_running = true;
        ftp_service_ctx.is_initialized = true;

        /* Create service thread */
        thread_handle = osal_thread_create(
            danp_ftp_service_thread,
            &ftp_service_ctx,
            &thread_attr);

        if (!thread_handle)
        {
            danp_log_message(DANP_LOG_ERROR, "FTP service failed to create service thread");
            danp_close(sock);
            ftp_service_ctx.is_initialized = false;
            ret = -1;
            break;
        }

        ftp_service_ctx.service_thread = thread_handle;

        danp_log_message(
            DANP_LOG_INFO,
            "FTP service initialized on port %u",
            DANP_FTP_SERVICE_PORT);

        break;
    }

    return ret;
}
