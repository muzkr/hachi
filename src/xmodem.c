
#include "xmodem.h"
#include "hachi_config.h"
#include "hardware/uart.h"
#include <assert.h>

#if 0 == PICO_DEFAULT_UART
#define UARTx uart0
#else
#define UARTx uart1
#endif

#define SOH 0x01
#define EOT 0x04
#define ACK 0x06
#define NAK 0x15
#define CAN 0x18

typedef enum
{
    RES_SOH = SOH,
    RES_EOT = EOT,
    RES_CAN = CAN,
    RES_REPEAT_PKT = 0x100,
    RES_PKT_TIMEOUT,
    RES_DATA_TIMEOUT,
    RES_BAD_DATA,
    RES_FATAL,
} result_t;

static void purge(uint32_t timeout);
static int recv_byte(uint32_t timeout);
static result_t recv_packet(uint8_t *buf, uint8_t pkt_num, uint32_t pkt_timeout, uint32_t data_timeout);
static inline void send_byte(uint8_t b) { uart_putc_raw(UARTx, b); }
static uint16_t calc_crc(const uint8_t *buf, uint32_t size);

xm_result_t xm_recv_begin(uint8_t *buf, uint32_t pkt_timeout, uint32_t data_timeout)
{
    purge(HACHI_CONFIG_XMODEM_PURGE_TIMEOUT);
    send_byte('C');

    int retry = 0;

    while (true)
    {
        result_t res = recv_packet(buf, 1, pkt_timeout, data_timeout);
        switch (res)
        {
        case RES_SOH:
            return XM_RES_OK;
        case RES_CAN:
            return XM_RES_CAN;
        case RES_EOT:
            // Well..
            send_byte(ACK);
            return XM_RES_EOT;

        case RES_BAD_DATA:
        case RES_DATA_TIMEOUT:
            if (retry++ < HACHI_CONFIG_XMODEM_RETRY)
            {
                purge(HACHI_CONFIG_XMODEM_PURGE_TIMEOUT);
                send_byte(NAK);
                break;
            }

            send_byte(CAN);
            return XM_RES_ABORT;

        case RES_PKT_TIMEOUT:
            return XM_RES_INITIAL_PACKET_TIMEOUT;

        case RES_REPEAT_PKT:
        case RES_FATAL:
        default:
            send_byte(CAN);
            return XM_RES_ABORT;
        } // switch

    } // while

    // Should not reach here
    assert(false);
}

xm_result_t xm_recv_packet(uint8_t *buf, uint8_t pkt_num, uint32_t pkt_timeout, uint32_t data_timeout)
{
    send_byte(ACK);

    int retry = 0;

    while (true)
    {
        result_t res = recv_packet(buf, pkt_num, pkt_timeout, data_timeout);
        switch (res)
        {
        case RES_SOH:
            return XM_RES_OK;
        case RES_EOT:
            send_byte(ACK);
            return XM_RES_EOT;
        case RES_CAN:
            return XM_RES_CAN;
        case RES_REPEAT_PKT:
            retry = 0;
            send_byte(ACK);
            break;

        case RES_PKT_TIMEOUT:
        case RES_DATA_TIMEOUT:
        case RES_BAD_DATA:
            if (retry++ < HACHI_CONFIG_XMODEM_RETRY)
            {
                purge(HACHI_CONFIG_XMODEM_PURGE_TIMEOUT);
                send_byte(NAK);
                break;
            }

            send_byte(CAN);
            return XM_RES_ABORT;

        case RES_FATAL:
        default:
            send_byte(CAN);
            return XM_RES_ABORT;
        } // switch

    } // while

    // Should not reach here
    assert(false);
}

void xm_recv_cancel()
{
    send_byte(CAN);
}

static inline void purge(uint32_t timeout)
{
    while (uart_is_readable_within_us(UARTx, 1000 * timeout))
    {
        uart_getc(UARTx);
    }
}

static inline int recv_byte(uint32_t timeout)
{
    if (uart_is_readable_within_us(UARTx, 1000 * timeout))
    {
        return 0xff & uart_getc(UARTx);
    }
    return -1;
}

static result_t recv_packet(uint8_t *buf, uint8_t pkt_num, uint32_t pkt_timeout, uint32_t data_timeout)
{

    // Packet type
    {
        int n = recv_byte(pkt_timeout);
        switch (n)
        {
        case -1:
            return RES_PKT_TIMEOUT;
        case SOH:
            // OK
            break;
        case EOT:
            return RES_EOT;
        case CAN:
            return RES_CAN;
        default:
            return RES_BAD_DATA;
        } // switch
    } //

    // Packet number
    uint8_t pkt_num1;
    {
        int n = recv_byte(data_timeout);
        if (-1 == n)
        {
            return RES_DATA_TIMEOUT;
        }
        else if (pkt_num == n || pkt_num - 1 == n)
        {
            // OK
            pkt_num1 = n;
        }
        else
        {
            return RES_FATAL;
        }

        // Packet number complete ---

        int n1 = recv_byte(data_timeout);
        if (-1 == n1)
        {
            return RES_DATA_TIMEOUT;
        }
        else if (0xff == n + n1)
        {
            // OK
        }
        else
        {
            return RES_BAD_DATA;
        }
    } //

    // Payload
    for (int i = 0; i < XM_PAYLOAD_SIZE; i++)
    {
        int n = recv_byte(data_timeout);
        if (-1 == n)
        {
            return RES_DATA_TIMEOUT;
        }

        buf[i] = n;
    } //

    // CRC
    uint16_t crc;
    {
        int n = recv_byte(data_timeout);
        if (-1 == n)
        {
            return RES_DATA_TIMEOUT;
        }

        crc = n << 8;

        n = recv_byte(data_timeout);
        if (-1 == n)
        {
            return RES_DATA_TIMEOUT;
        }

        crc |= n;
    } //

    if (calc_crc(buf, XM_PAYLOAD_SIZE) != crc)
    {
        return RES_BAD_DATA;
    }

    return pkt_num == pkt_num1 ? RES_SOH : RES_REPEAT_PKT;
}

static uint16_t calc_crc(const uint8_t *buf, uint32_t size)
{
    int crc = 0;

    for (int i = 0; i < size; i++)
    {
        crc = crc ^ (int)*buf++ << 8;

        for (int j = 0; j < 8; j++)
        {
            if (crc & 0x8000)
            {
                crc = crc << 1 ^ 0x1021;
            }
            else
            {
                crc = crc << 1;
            }
        } // for

    } // for

    return (uint16_t)crc;
}
