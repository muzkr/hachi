
#ifndef _XMODEM_H
#define _XMODEM_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#define XM_PAYLOAD_SIZE 128

typedef enum
{
    XM_RES_OK = 0,
    XM_RES_EOT,
    XM_RES_CAN,
    XM_RES_ABORT,
    XM_RES_INITIAL_PACKET_TIMEOUT = 0x100,
} xm_result_t;

xm_result_t xm_recv_begin(uint8_t *buf, uint32_t pkt_timeout, uint32_t data_timeout);
xm_result_t xm_recv_packet(uint8_t *buf, uint8_t pkt_num, uint32_t pkt_timeout, uint32_t data_timeout);
void xm_recv_cancel();

#endif // _XMODEM_H
