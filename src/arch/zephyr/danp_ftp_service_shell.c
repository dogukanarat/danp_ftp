/* danp_ftp_shell.c - FTP library test shell commands for Zephyr */

/* All Rights Reserved */

/* Includes */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>

#include "danp/ftp/danp_ftp.h"
#include "danp/ftp/danp_ftp_service.h"
#include "danp/danp.h"

/* Definitions */

#define DANP_FTP_TEST_DEFAULT_REMOTE_NODE     (10)
#define DANP_FTP_TEST_DEFAULT_CHUNK_SIZE      (64)
#define DANP_FTP_TEST_DEFAULT_TIMEOUT_MS      (5000)
#define DANP_FTP_TEST_DEFAULT_MAX_RETRIES     (3)
#define DANP_FTP_TEST_MAX_FILE_SIZE           (4096)
#define DANP_FTP_TEST_PATTERN_SIZE            (1024)

/* Types */

typedef struct danp_ftp_test_stats_s
{
    uint32_t chunks_transferred;
    uint32_t total_bytes;
    uint32_t chunk_crcs[64];
    uint32_t total_crc;
    uint32_t expected_total_crc;
    bool verified;
} danp_ftp_test_stats_t;

typedef struct danp_ftp_test_context_s
{
    uint8_t tx_buffer[DANP_FTP_TEST_MAX_FILE_SIZE];
    uint8_t rx_buffer[DANP_FTP_TEST_MAX_FILE_SIZE];
    size_t tx_size;
    size_t rx_size;
    danp_ftp_test_stats_t tx_stats;
    danp_ftp_test_stats_t rx_stats;
    uint16_t remote_node;
    uint16_t chunk_size;
    uint32_t timeout_ms;
    uint8_t max_retries;
    const struct shell *shell;
} danp_ftp_test_context_t;

/* Forward Declarations */


/* Variables */

static danp_ftp_test_context_t test_ctx;
static danp_ftp_handle_t test_handle;
static bool handle_initialized = false;

/* CRC Calculation */

/**
 * @brief Calculate CRC32 for test data verification.
 * @param data Pointer to the data buffer.
 * @param length Length of the data.
 * @return Calculated CRC32 value.
 */
static uint32_t danp_ftp_test_calculate_crc(const uint8_t *data, size_t length)
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
 * @brief Generate a known test pattern.
 * @param buffer Output buffer.
 * @param size Size of the pattern.
 * @param seed Seed for pattern generation.
 */
static void danp_ftp_test_generate_pattern(uint8_t *buffer, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++)
    {
        buffer[i] = (uint8_t)((seed + i) ^ (i >> 3));
    }
}

/**
 * @brief Print test statistics.
 * @param sh Shell instance.
 * @param label Label for the stats.
 * @param stats Pointer to stats structure.
 */
static void danp_ftp_test_print_stats(
    const struct shell *sh,
    const char *label,
    const danp_ftp_test_stats_t *stats)
{
    shell_print(sh, "=== %s Statistics ===", label);
    shell_print(sh, "  Chunks transferred: %u", stats->chunks_transferred);
    shell_print(sh, "  Total bytes: %u", stats->total_bytes);
    shell_print(sh, "  Total CRC: 0x%08X", stats->total_crc);
    shell_print(sh, "  Expected CRC: 0x%08X", stats->expected_total_crc);
    shell_print(sh, "  Verified: %s", stats->verified ? "YES" : "NO");

    if (stats->chunks_transferred > 0 && stats->chunks_transferred <= 64)
    {
        shell_print(sh, "  Chunk CRCs:");
        for (uint32_t i = 0; i < stats->chunks_transferred; i++)
        {
            shell_print(sh, "    [%u]: 0x%08X", i, stats->chunk_crcs[i]);
        }
    }
}

/* FTP Callbacks */

/**
 * @brief Source callback for transmit test.
 */
static danp_ftp_status_t danp_ftp_test_source_cb(
    danp_ftp_handle_t *handle,
    size_t offset,
    uint8_t *data,
    uint16_t length,
    uint8_t *more,
    void *user_data)
{
    danp_ftp_test_context_t *ctx = (danp_ftp_test_context_t *)user_data;
    size_t remaining;
    size_t to_copy;
    uint32_t chunk_crc;

    (void)handle;

    for (;;)
    {
        if (!ctx || !data)
        {
            return DANP_FTP_STATUS_INVALID_PARAM;
        }

        if (offset >= ctx->tx_size)
        {
            if (more)
            {
                *more = 0;
            }
            return 0;
        }

        remaining = ctx->tx_size - offset;
        to_copy = (remaining < length) ? remaining : length;

        memcpy(data, &ctx->tx_buffer[offset], to_copy);

        /* Calculate chunk CRC */
        chunk_crc = danp_ftp_test_calculate_crc(data, to_copy);

        if (ctx->tx_stats.chunks_transferred < 64)
        {
            ctx->tx_stats.chunk_crcs[ctx->tx_stats.chunks_transferred] = chunk_crc;
        }
        ctx->tx_stats.chunks_transferred++;
        ctx->tx_stats.total_bytes += to_copy;

        if (ctx->shell)
        {
            shell_print(ctx->shell, "[TX] Chunk %u: offset=%zu len=%zu CRC=0x%08X",
                ctx->tx_stats.chunks_transferred - 1, offset, to_copy, chunk_crc);
        }

        if (more)
        {
            *more = (offset + to_copy < ctx->tx_size) ? 1 : 0;
        }

        return (danp_ftp_status_t)to_copy;
    }
}

/**
 * @brief Sink callback for receive test.
 */
static danp_ftp_status_t danp_ftp_test_sink_cb(
    danp_ftp_handle_t *handle,
    size_t offset,
    const uint8_t *data,
    uint16_t length,
    uint8_t more,
    void *user_data)
{
    danp_ftp_test_context_t *ctx = (danp_ftp_test_context_t *)user_data;
    uint32_t chunk_crc;

    (void)handle;
    (void)more;

    for (;;)
    {
        if (!ctx || !data)
        {
            return DANP_FTP_STATUS_INVALID_PARAM;
        }

        if (offset + length > DANP_FTP_TEST_MAX_FILE_SIZE)
        {
            if (ctx->shell)
            {
                shell_error(ctx->shell, "[RX] Buffer overflow at offset %zu", offset);
            }
            return DANP_FTP_STATUS_ERROR;
        }

        memcpy(&ctx->rx_buffer[offset], data, length);
        ctx->rx_size = offset + length;

        /* Calculate chunk CRC */
        chunk_crc = danp_ftp_test_calculate_crc(data, length);

        if (ctx->rx_stats.chunks_transferred < 64)
        {
            ctx->rx_stats.chunk_crcs[ctx->rx_stats.chunks_transferred] = chunk_crc;
        }
        ctx->rx_stats.chunks_transferred++;
        ctx->rx_stats.total_bytes += length;

        if (ctx->shell)
        {
            shell_print(ctx->shell, "[RX] Chunk %u: offset=%zu len=%u CRC=0x%08X",
                ctx->rx_stats.chunks_transferred - 1, offset, length, chunk_crc);
        }

        return (danp_ftp_status_t)length;
    }
}

/* Shell Commands */

/**
 * @brief Initialize FTP test with remote node.
 */
static int cmd_ftp_init(const struct shell *sh, size_t argc, char **argv)
{
    uint16_t remote_node = DANP_FTP_TEST_DEFAULT_REMOTE_NODE;
    danp_ftp_status_t status;

    if (argc > 1)
    {
        remote_node = (uint16_t)strtoul(argv[1], NULL, 0);
    }

    if (handle_initialized)
    {
        shell_warn(sh, "FTP handle already initialized, deinitializing first...");
        danp_ftp_deinit(&test_handle);
        handle_initialized = false;
    }

    memset(&test_ctx, 0, sizeof(danp_ftp_test_context_t));
    test_ctx.remote_node = remote_node;
    test_ctx.chunk_size = DANP_FTP_TEST_DEFAULT_CHUNK_SIZE;
    test_ctx.timeout_ms = DANP_FTP_TEST_DEFAULT_TIMEOUT_MS;
    test_ctx.max_retries = DANP_FTP_TEST_DEFAULT_MAX_RETRIES;
    test_ctx.shell = sh;

    shell_print(sh, "Initializing FTP connection to node %u...", remote_node);

    status = danp_ftp_init(&test_handle, remote_node);
    if (status != DANP_FTP_STATUS_OK)
    {
        shell_error(sh, "FTP init failed: %d", status);
        return -1;
    }

    handle_initialized = true;
    shell_print(sh, "FTP initialized successfully");

    return 0;
}

/**
 * @brief Deinitialize FTP test.
 */
static int cmd_ftp_deinit(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!handle_initialized)
    {
        shell_warn(sh, "FTP handle not initialized");
        return -1;
    }

    danp_ftp_deinit(&test_handle);
    handle_initialized = false;

    shell_print(sh, "FTP deinitialized");

    return 0;
}

/**
 * @brief Configure test parameters.
 */
static int cmd_ftp_config(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 3)
    {
        shell_print(sh, "Current configuration:");
        shell_print(sh, "  Remote node: %u", test_ctx.remote_node);
        shell_print(sh, "  Chunk size: %u", test_ctx.chunk_size);
        shell_print(sh, "  Timeout: %u ms", test_ctx.timeout_ms);
        shell_print(sh, "  Max retries: %u", test_ctx.max_retries);
        shell_print(sh, "");
        shell_print(sh, "Usage: ftp config <param> <value>");
        shell_print(sh, "  param: node, chunk, timeout, retries");
        return 0;
    }

    if (strcmp(argv[1], "node") == 0)
    {
        test_ctx.remote_node = (uint16_t)strtoul(argv[2], NULL, 0);
        shell_print(sh, "Remote node set to %u", test_ctx.remote_node);
    }
    else if (strcmp(argv[1], "chunk") == 0)
    {
        test_ctx.chunk_size = (uint16_t)strtoul(argv[2], NULL, 0);
        shell_print(sh, "Chunk size set to %u", test_ctx.chunk_size);
    }
    else if (strcmp(argv[1], "timeout") == 0)
    {
        test_ctx.timeout_ms = (uint32_t)strtoul(argv[2], NULL, 0);
        shell_print(sh, "Timeout set to %u ms", test_ctx.timeout_ms);
    }
    else if (strcmp(argv[1], "retries") == 0)
    {
        test_ctx.max_retries = (uint8_t)strtoul(argv[2], NULL, 0);
        shell_print(sh, "Max retries set to %u", test_ctx.max_retries);
    }
    else
    {
        shell_error(sh, "Unknown parameter: %s", argv[1]);
        return -1;
    }

    return 0;
}

/**
 * @brief Generate test pattern and calculate expected CRCs.
 */
static int cmd_ftp_generate(const struct shell *sh, size_t argc, char **argv)
{
    size_t size = DANP_FTP_TEST_PATTERN_SIZE;
    uint8_t seed = 0xA5;
    uint32_t total_crc;
    size_t offset = 0;
    uint32_t chunk_idx = 0;

    if (argc > 1)
    {
        size = (size_t)strtoul(argv[1], NULL, 0);
        if (size > DANP_FTP_TEST_MAX_FILE_SIZE)
        {
            size = DANP_FTP_TEST_MAX_FILE_SIZE;
            shell_warn(sh, "Size clamped to %u", DANP_FTP_TEST_MAX_FILE_SIZE);
        }
    }

    if (argc > 2)
    {
        seed = (uint8_t)strtoul(argv[2], NULL, 0);
    }

    shell_print(sh, "Generating test pattern: size=%zu seed=0x%02X", size, seed);

    danp_ftp_test_generate_pattern(test_ctx.tx_buffer, size, seed);
    test_ctx.tx_size = size;

    /* Calculate total CRC */
    total_crc = danp_ftp_test_calculate_crc(test_ctx.tx_buffer, size);

    /* Reset stats */
    memset(&test_ctx.tx_stats, 0, sizeof(danp_ftp_test_stats_t));
    test_ctx.tx_stats.expected_total_crc = total_crc;

    shell_print(sh, "Pattern generated:");
    shell_print(sh, "  Size: %zu bytes", size);
    shell_print(sh, "  Total CRC: 0x%08X", total_crc);

    /* Pre-calculate expected chunk CRCs */
    shell_print(sh, "Expected chunk CRCs (chunk_size=%u):", test_ctx.chunk_size);

    while (offset < size && chunk_idx < 64)
    {
        size_t chunk_len = (size - offset < test_ctx.chunk_size) ?
                           (size - offset) : test_ctx.chunk_size;
        uint32_t chunk_crc = danp_ftp_test_calculate_crc(
            &test_ctx.tx_buffer[offset], chunk_len);

        shell_print(sh, "  [%u] offset=%zu len=%zu CRC=0x%08X",
            chunk_idx, offset, chunk_len, chunk_crc);

        offset += chunk_len;
        chunk_idx++;
    }

    return 0;
}

/**
 * @brief Test transmit operation.
 */
static int cmd_ftp_tx(const struct shell *sh, size_t argc, char **argv)
{
    const char *file_id = "test_file";
    danp_ftp_transfer_config_t config;
    danp_ftp_status_t status;

    if (argc > 1)
    {
        file_id = argv[1];
    }

    if (!handle_initialized)
    {
        shell_error(sh, "FTP handle not initialized. Run 'ftp init' first.");
        return -1;
    }

    if (test_ctx.tx_size == 0)
    {
        shell_error(sh, "No test pattern generated. Run 'ftp generate' first.");
        return -1;
    }

    /* Reset TX stats */
    memset(&test_ctx.tx_stats, 0, sizeof(danp_ftp_test_stats_t));
    test_ctx.tx_stats.expected_total_crc = danp_ftp_test_calculate_crc(
        test_ctx.tx_buffer, test_ctx.tx_size);
    test_ctx.shell = sh;

    config.file_id = (const uint8_t *)file_id;
    config.file_id_len = strlen(file_id);
    config.chunk_size = test_ctx.chunk_size;
    config.timeout_ms = test_ctx.timeout_ms;
    config.max_retries = test_ctx.max_retries;

    shell_print(sh, "Starting FTP transmit test...");
    shell_print(sh, "  File ID: %s", file_id);
    shell_print(sh, "  Size: %zu bytes", test_ctx.tx_size);
    shell_print(sh, "  Chunk size: %u", config.chunk_size);
    shell_print(sh, "  Expected CRC: 0x%08X", test_ctx.tx_stats.expected_total_crc);
    shell_print(sh, "");

    status = danp_ftp_transmit(
        &test_handle,
        &config,
        danp_ftp_test_source_cb,
        &test_ctx);

    shell_print(sh, "");

    if (status < 0)
    {
        shell_error(sh, "FTP transmit failed: %d", status);
        return -1;
    }

    /* Calculate actual total CRC from sent data */
    test_ctx.tx_stats.total_crc = danp_ftp_test_calculate_crc(
        test_ctx.tx_buffer, test_ctx.tx_stats.total_bytes);

    test_ctx.tx_stats.verified =
        (test_ctx.tx_stats.total_crc == test_ctx.tx_stats.expected_total_crc) &&
        (test_ctx.tx_stats.total_bytes == test_ctx.tx_size);

    danp_ftp_test_print_stats(sh, "TX", &test_ctx.tx_stats);

    if (test_ctx.tx_stats.verified)
    {
        shell_print(sh, "\n[PASS] TX test completed successfully");
    }
    else
    {
        shell_error(sh, "\n[FAIL] TX verification failed");
        return -1;
    }

    return 0;
}

/**
 * @brief Test receive operation.
 */
static int cmd_ftp_rx(const struct shell *sh, size_t argc, char **argv)
{
    const char *file_id = "test_file";
    danp_ftp_transfer_config_t config;
    danp_ftp_status_t status;
    uint32_t expected_crc = 0;

    if (argc > 1)
    {
        file_id = argv[1];
    }

    if (argc > 2)
    {
        expected_crc = (uint32_t)strtoul(argv[2], NULL, 16);
    }

    if (!handle_initialized)
    {
        shell_error(sh, "FTP handle not initialized. Run 'ftp init' first.");
        return -1;
    }

    /* Reset RX context */
    memset(test_ctx.rx_buffer, 0, sizeof(test_ctx.rx_buffer));
    test_ctx.rx_size = 0;
    memset(&test_ctx.rx_stats, 0, sizeof(danp_ftp_test_stats_t));
    test_ctx.rx_stats.expected_total_crc = expected_crc;
    test_ctx.shell = sh;

    config.file_id = (const uint8_t *)file_id;
    config.file_id_len = strlen(file_id);
    config.chunk_size = test_ctx.chunk_size;
    config.timeout_ms = test_ctx.timeout_ms;
    config.max_retries = test_ctx.max_retries;

    shell_print(sh, "Starting FTP receive test...");
    shell_print(sh, "  File ID: %s", file_id);
    shell_print(sh, "  Chunk size: %u", config.chunk_size);
    if (expected_crc != 0)
    {
        shell_print(sh, "  Expected CRC: 0x%08X", expected_crc);
    }
    shell_print(sh, "");

    status = danp_ftp_receive(
        &test_handle,
        &config,
        danp_ftp_test_sink_cb,
        &test_ctx);

    shell_print(sh, "");

    if (status < 0)
    {
        shell_error(sh, "FTP receive failed: %d", status);
        return -1;
    }

    /* Calculate actual total CRC from received data */
    test_ctx.rx_stats.total_crc = danp_ftp_test_calculate_crc(
        test_ctx.rx_buffer, test_ctx.rx_size);

    if (expected_crc != 0)
    {
        test_ctx.rx_stats.verified =
            (test_ctx.rx_stats.total_crc == expected_crc);
    }
    else
    {
        test_ctx.rx_stats.verified = true;
        test_ctx.rx_stats.expected_total_crc = test_ctx.rx_stats.total_crc;
    }

    danp_ftp_test_print_stats(sh, "RX", &test_ctx.rx_stats);

    if (test_ctx.rx_stats.verified)
    {
        shell_print(sh, "\n[PASS] RX test completed successfully");
    }
    else
    {
        shell_error(sh, "\n[FAIL] RX verification failed");
        return -1;
    }

    return 0;
}

/**
 * @brief Loopback test - TX then RX and compare.
 */
static int cmd_ftp_loopback(const struct shell *sh, size_t argc, char **argv)
{
    const char *file_id = "loopback_test";
    size_t size = DANP_FTP_TEST_PATTERN_SIZE;
    uint8_t seed = 0x5A;
    danp_ftp_transfer_config_t config;
    danp_ftp_status_t status;
    uint32_t tx_crc, rx_crc;
    bool data_match;

    if (argc > 1)
    {
        size = (size_t)strtoul(argv[1], NULL, 0);
        if (size > DANP_FTP_TEST_MAX_FILE_SIZE)
        {
            size = DANP_FTP_TEST_MAX_FILE_SIZE;
        }
    }

    if (argc > 2)
    {
        seed = (uint8_t)strtoul(argv[2], NULL, 0);
    }

    if (!handle_initialized)
    {
        shell_error(sh, "FTP handle not initialized. Run 'ftp init' first.");
        return -1;
    }

    shell_print(sh, "=== FTP Loopback Test ===");
    shell_print(sh, "Size: %zu bytes, Seed: 0x%02X", size, seed);
    shell_print(sh, "");

    /* Generate test pattern */
    danp_ftp_test_generate_pattern(test_ctx.tx_buffer, size, seed);
    test_ctx.tx_size = size;
    tx_crc = danp_ftp_test_calculate_crc(test_ctx.tx_buffer, size);

    shell_print(sh, "Generated TX pattern CRC: 0x%08X", tx_crc);

    /* Reset stats */
    memset(&test_ctx.tx_stats, 0, sizeof(danp_ftp_test_stats_t));
    memset(&test_ctx.rx_stats, 0, sizeof(danp_ftp_test_stats_t));
    test_ctx.tx_stats.expected_total_crc = tx_crc;
    test_ctx.shell = sh;

    config.file_id = (const uint8_t *)file_id;
    config.file_id_len = strlen(file_id);
    config.chunk_size = test_ctx.chunk_size;
    config.timeout_ms = test_ctx.timeout_ms;
    config.max_retries = test_ctx.max_retries;

    /* Phase 1: Transmit */
    shell_print(sh, "\n--- Phase 1: Transmit ---");

    status = danp_ftp_transmit(
        &test_handle,
        &config,
        danp_ftp_test_source_cb,
        &test_ctx);

    if (status < 0)
    {
        shell_error(sh, "TX phase failed: %d", status);
        return -1;
    }

    test_ctx.tx_stats.total_crc = danp_ftp_test_calculate_crc(
        test_ctx.tx_buffer, test_ctx.tx_stats.total_bytes);

    shell_print(sh, "TX complete: %u bytes, CRC=0x%08X",
        test_ctx.tx_stats.total_bytes, test_ctx.tx_stats.total_crc);

    /* Reinitialize connection for RX */
    danp_ftp_deinit(&test_handle);
    k_msleep(100);

    status = danp_ftp_init(&test_handle, test_ctx.remote_node);
    if (status != DANP_FTP_STATUS_OK)
    {
        shell_error(sh, "Failed to reinitialize FTP: %d", status);
        handle_initialized = false;
        return -1;
    }

    /* Phase 2: Receive */
    shell_print(sh, "\n--- Phase 2: Receive ---");

    memset(test_ctx.rx_buffer, 0, sizeof(test_ctx.rx_buffer));
    test_ctx.rx_size = 0;
    test_ctx.rx_stats.expected_total_crc = tx_crc;

    status = danp_ftp_receive(
        &test_handle,
        &config,
        danp_ftp_test_sink_cb,
        &test_ctx);

    if (status < 0)
    {
        shell_error(sh, "RX phase failed: %d", status);
        return -1;
    }

    rx_crc = danp_ftp_test_calculate_crc(test_ctx.rx_buffer, test_ctx.rx_size);
    test_ctx.rx_stats.total_crc = rx_crc;

    shell_print(sh, "RX complete: %zu bytes, CRC=0x%08X",
        test_ctx.rx_size, rx_crc);

    /* Phase 3: Verification */
    shell_print(sh, "\n--- Phase 3: Verification ---");

    data_match = (test_ctx.tx_size == test_ctx.rx_size) &&
                 (memcmp(test_ctx.tx_buffer, test_ctx.rx_buffer, test_ctx.tx_size) == 0);

    shell_print(sh, "TX size: %zu, RX size: %zu", test_ctx.tx_size, test_ctx.rx_size);
    shell_print(sh, "TX CRC: 0x%08X, RX CRC: 0x%08X", tx_crc, rx_crc);
    shell_print(sh, "CRC match: %s", (tx_crc == rx_crc) ? "YES" : "NO");
    shell_print(sh, "Data match: %s", data_match ? "YES" : "NO");

    shell_print(sh, "\n=== Loopback Test Summary ===");
    danp_ftp_test_print_stats(sh, "TX", &test_ctx.tx_stats);
    shell_print(sh, "");
    danp_ftp_test_print_stats(sh, "RX", &test_ctx.rx_stats);

    if (data_match && (tx_crc == rx_crc))
    {
        shell_print(sh, "\n[PASS] Loopback test PASSED");
        return 0;
    }
    else
    {
        shell_error(sh, "\n[FAIL] Loopback test FAILED");

        /* Find first mismatch */
        if (!data_match && test_ctx.tx_size == test_ctx.rx_size)
        {
            for (size_t i = 0; i < test_ctx.tx_size; i++)
            {
                if (test_ctx.tx_buffer[i] != test_ctx.rx_buffer[i])
                {
                    shell_error(sh, "First mismatch at offset %zu: TX=0x%02X RX=0x%02X",
                        i, test_ctx.tx_buffer[i], test_ctx.rx_buffer[i]);
                    break;
                }
            }
        }
        return -1;
    }
}

/**
 * @brief Dump buffer contents.
 */
static int cmd_ftp_dump(const struct shell *sh, size_t argc, char **argv)
{
    const char *buffer_name = "tx";
    size_t offset = 0;
    size_t length = 64;
    uint8_t *buffer;
    size_t buffer_size;

    if (argc > 1)
    {
        buffer_name = argv[1];
    }

    if (argc > 2)
    {
        offset = (size_t)strtoul(argv[2], NULL, 0);
    }

    if (argc > 3)
    {
        length = (size_t)strtoul(argv[3], NULL, 0);
    }

    if (strcmp(buffer_name, "tx") == 0)
    {
        buffer = test_ctx.tx_buffer;
        buffer_size = test_ctx.tx_size;
    }
    else if (strcmp(buffer_name, "rx") == 0)
    {
        buffer = test_ctx.rx_buffer;
        buffer_size = test_ctx.rx_size;
    }
    else
    {
        shell_error(sh, "Unknown buffer: %s (use 'tx' or 'rx')", buffer_name);
        return -1;
    }

    if (buffer_size == 0)
    {
        shell_warn(sh, "Buffer '%s' is empty", buffer_name);
        return 0;
    }

    if (offset >= buffer_size)
    {
        shell_error(sh, "Offset %zu exceeds buffer size %zu", offset, buffer_size);
        return -1;
    }

    if (offset + length > buffer_size)
    {
        length = buffer_size - offset;
    }

    shell_print(sh, "Buffer '%s' [%zu - %zu] of %zu bytes:",
        buffer_name, offset, offset + length - 1, buffer_size);

    for (size_t i = 0; i < length; i += 16)
    {
        char hex_str[50] = {0};
        char ascii_str[17] = {0};
        size_t line_len = (length - i < 16) ? (length - i) : 16;

        for (size_t j = 0; j < line_len; j++)
        {
            uint8_t byte = buffer[offset + i + j];
            sprintf(&hex_str[j * 3], "%02X ", byte);
            ascii_str[j] = (byte >= 32 && byte < 127) ? byte : '.';
        }

        shell_print(sh, "  %04zX: %-48s |%s|", offset + i, hex_str, ascii_str);
    }

    return 0;
}

/**
 * @brief Show test status.
 */
static int cmd_ftp_status(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "=== FTP Test Status ===");
    shell_print(sh, "Handle initialized: %s", handle_initialized ? "YES" : "NO");
    shell_print(sh, "");

    shell_print(sh, "Configuration:");
    shell_print(sh, "  Remote node: %u", test_ctx.remote_node);
    shell_print(sh, "  Chunk size: %u", test_ctx.chunk_size);
    shell_print(sh, "  Timeout: %u ms", test_ctx.timeout_ms);
    shell_print(sh, "  Max retries: %u", test_ctx.max_retries);
    shell_print(sh, "");

    shell_print(sh, "TX Buffer:");
    shell_print(sh, "  Size: %zu bytes", test_ctx.tx_size);
    if (test_ctx.tx_size > 0)
    {
        shell_print(sh, "  CRC: 0x%08X",
            danp_ftp_test_calculate_crc(test_ctx.tx_buffer, test_ctx.tx_size));
    }
    shell_print(sh, "");

    shell_print(sh, "RX Buffer:");
    shell_print(sh, "  Size: %zu bytes", test_ctx.rx_size);
    if (test_ctx.rx_size > 0)
    {
        shell_print(sh, "  CRC: 0x%08X",
            danp_ftp_test_calculate_crc(test_ctx.rx_buffer, test_ctx.rx_size));
    }

    if (test_ctx.tx_stats.chunks_transferred > 0)
    {
        shell_print(sh, "");
        danp_ftp_test_print_stats(sh, "Last TX", &test_ctx.tx_stats);
    }

    if (test_ctx.rx_stats.chunks_transferred > 0)
    {
        shell_print(sh, "");
        danp_ftp_test_print_stats(sh, "Last RX", &test_ctx.rx_stats);
    }

    return 0;
}

/**
 * @brief Calculate CRC of arbitrary data.
 */
static int cmd_ftp_crc(const struct shell *sh, size_t argc, char **argv)
{
    if (argc < 2)
    {
        shell_print(sh, "Usage: ftp crc <hex_data>");
        shell_print(sh, "Example: ftp crc 01020304");
        return 0;
    }

    const char *hex_str = argv[1];
    size_t hex_len = strlen(hex_str);
    size_t data_len = hex_len / 2;
    uint8_t data[256];
    uint32_t crc;

    if (hex_len % 2 != 0)
    {
        shell_error(sh, "Hex string must have even length");
        return -1;
    }

    if (data_len > sizeof(data))
    {
        shell_error(sh, "Data too long (max %zu bytes)", sizeof(data));
        return -1;
    }

    for (size_t i = 0; i < data_len; i++)
    {
        char byte_str[3] = {hex_str[i * 2], hex_str[i * 2 + 1], 0};
        data[i] = (uint8_t)strtoul(byte_str, NULL, 16);
    }

    crc = danp_ftp_test_calculate_crc(data, data_len);

    shell_print(sh, "Data length: %zu bytes", data_len);
    shell_print(sh, "CRC32: 0x%08X", crc);

    return 0;
}

/* Shell Command Registration */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_ftp_cmds,
    SHELL_CMD_ARG(init, NULL,
        "Initialize FTP connection\n"
        "Usage: ftp init [remote_node]\n"
        "  remote_node: Target node ID (default: 10)",
        cmd_ftp_init, 1, 1),
    SHELL_CMD(deinit, NULL,
        "Deinitialize FTP connection",
        cmd_ftp_deinit),
    SHELL_CMD_ARG(config, NULL,
        "Configure test parameters\n"
        "Usage: ftp config [param] [value]\n"
        "  param: node, chunk, timeout, retries",
        cmd_ftp_config, 1, 2),
    SHELL_CMD_ARG(generate, NULL,
        "Generate test pattern\n"
        "Usage: ftp generate [size] [seed]\n"
        "  size: Pattern size in bytes (default: 1024)\n"
        "  seed: Pattern seed (default: 0xA5)",
        cmd_ftp_generate, 1, 2),
    SHELL_CMD_ARG(tx, NULL,
        "Transmit test data\n"
        "Usage: ftp tx [file_id]\n"
        "  file_id: File identifier (default: test_file)",
        cmd_ftp_tx, 1, 1),
    SHELL_CMD_ARG(rx, NULL,
        "Receive test data\n"
        "Usage: ftp rx [file_id] [expected_crc]\n"
        "  file_id: File identifier (default: test_file)\n"
        "  expected_crc: Expected CRC in hex (optional)",
        cmd_ftp_rx, 1, 2),
    SHELL_CMD_ARG(loopback, NULL,
        "Run full loopback test (TX then RX)\n"
        "Usage: ftp loopback [size] [seed]",
        cmd_ftp_loopback, 1, 2),
    SHELL_CMD_ARG(dump, NULL,
        "Dump buffer contents\n"
        "Usage: ftp dump [tx|rx] [offset] [length]",
        cmd_ftp_dump, 1, 3),
    SHELL_CMD(status, NULL,
        "Show test status and statistics",
        cmd_ftp_status),
    SHELL_CMD_ARG(crc, NULL,
        "Calculate CRC32 of hex data\n"
        "Usage: ftp crc <hex_data>",
        cmd_ftp_crc, 2, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(ftp, &sub_ftp_cmds, "FTP test commands", NULL);
