
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/flash.h"
#include "boot/uf2.h"
#include "hardware/structs/scb.h"

#include "hachi_config.h"
#include "led.h"
#include "xmodem.h"

#if 0 == PICO_DEFAULT_UART
#define UARTx uart0
#else
#define UARTx uart1
#endif

#define UART_BAUD 115200

#define BOOT2_SIZE 256
#define BL_SIZE (64 << 10) // 64 kB
#define FLASH_SIZE PICO_FLASH_SIZE_BYTES
static_assert(FLASH_SIZE == (2 << 20), "flash size should be 2 MB");
#define PROG_AREA_SIZE (FLASH_SIZE /* - BOOT2_SIZE */ - BL_SIZE)
#define PROG_AREA_BEGIN (XIP_BASE /* + BOOT2_SIZE */)
#define PROG_AREA_END (PROG_AREA_BEGIN + PROG_AREA_SIZE)

#define PACKETS_PER_BLOCK (sizeof(struct uf2_block) / XM_PAYLOAD_SIZE)
static_assert(0 == sizeof(struct uf2_block) % XM_PAYLOAD_SIZE, "unexpected block size");

#define DRY_RUN 0

#if DRY_RUN
#define FLASH_ERASE(a1, a2)
#define FLASH_PROG(a1, a2, a3)
#else
#define FLASH_ERASE(a1, a2) flash_range_erase((a1), (a2))
#define FLASH_PROG(a1, a2, a3) flash_range_program((a1), (a2), (a3))
#endif

typedef struct
{
    // All fields use word-sized type
    uint32_t prog_addr;
    uint32_t size;
} prog_info_t;

const prog_info_t _prog_info_record
    __attribute__((section(".hachi.prog_info")))
    __attribute__((aligned(4096))) = {0};

static_assert(sizeof(prog_info_t) <= FLASH_PAGE_SIZE, "prog info record larger than expected");

typedef struct
{
    uint32_t prog_addr;
    uint32_t num_blks;
    uint32_t num_blks_recv;
    uint32_t num_pkts_recv;
    uint8_t next_pkt_num;
} prog_state_t;

static uint8_t _block_buf[sizeof(struct uf2_block)];
static prog_state_t _prog_state;

static void uart_config();

static void prog_state_init(prog_state_t *p);
static bool check_generic_block(const struct uf2_block *b);
static bool check_1st_block(const struct uf2_block *b);
static bool check_block(const prog_state_t *s, const struct uf2_block *b);
static bool check_EOT(const prog_state_t *s);

static bool check_prog_info(const prog_info_t *p);
static void run_prog(const prog_info_t *p);

static inline int sector_index(uint32_t addr) { return (addr - XIP_BASE) / FLASH_SECTOR_SIZE; }
static inline int page_index(uint32_t addr) { return ((addr - XIP_BASE) % FLASH_SECTOR_SIZE) / FLASH_PAGE_SIZE; }

int main()
{

    led_config();
    // led_on(true) ;
    uart_config();

    while (true)
    {
        prog_state_t *s = &_prog_state;
        uint8_t *buf = _block_buf;

        prog_state_init(s);

        led_on(true);

        xm_result_t res;
        {
            bool prog_valid = check_prog_info(&_prog_info_record);

            uint32_t pkt_timeout = prog_valid ? HACHI_CONFIG_XMODEM_BOOT_TIMEOUT : HACHI_CONFIG_XMODEM_INITIAL_PACKET_TIMEOUT;
            res = xm_recv_begin(buf, pkt_timeout, HACHI_CONFIG_XMODEM_DATA_TIMEOUT);

            switch (res)
            {
            case XM_RES_INITIAL_PACKET_TIMEOUT:
            case XM_RES_CAN: // Explicitly refused
                if (prog_valid)
                {
                    led_on(false);
                    run_prog(&_prog_info_record);
                    continue;
                }
                break;
            default:
                break;
            } // switch

        } //

        struct uf2_block *b = (struct uf2_block *)buf;

        while (true)
        {
            if (XM_RES_OK == res)
            {
                s->num_pkts_recv++;
                s->next_pkt_num++;

                // Toggle every 2 packets
                if (1 == s->next_pkt_num % 2)
                {
                    led_toggle();
                }

                // Block incomplete
                if (s->num_pkts_recv < PACKETS_PER_BLOCK)
                {
                    res = xm_recv_packet(buf + XM_PAYLOAD_SIZE * s->num_pkts_recv,
                                         s->next_pkt_num,
                                         HACHI_CONFIG_XMODEM_PACKET_TIMEOUT,
                                         HACHI_CONFIG_XMODEM_DATA_TIMEOUT);
                    continue;
                }

                if (0 == s->num_blks_recv)
                {
                    // The 1st block ----

                    if (!check_1st_block(b))
                    {
                        xm_recv_cancel();
                        break;
                    }

                    int sec1 = sector_index(b->target_addr);
                    int sec2 // The last word (R.I.P.)
                        = sector_index(b->target_addr + FLASH_PAGE_SIZE * b->num_blocks - 4);

                    if (0 == sec1)
                    {
                        // Sector #0 -----

                        // We dont want to over-write boot stage2
                        uint8_t a1[BOOT2_SIZE];
                        memcpy(a1, (void *)XIP_BASE, BOOT2_SIZE);

                        FLASH_ERASE(FLASH_SECTOR_SIZE * sec1, FLASH_SECTOR_SIZE * (sec2 - sec1 + 1));
                        FLASH_PROG(0, a1, BOOT2_SIZE);

                        if (XIP_BASE != b->target_addr)
                        {
                            FLASH_PROG(b->target_addr - XIP_BASE, b->data, FLASH_PAGE_SIZE);
                        }
                    }
                    else
                    {
                        FLASH_ERASE(FLASH_SECTOR_SIZE * sec1, FLASH_SECTOR_SIZE * (sec2 - sec1 + 1));
                        FLASH_PROG(b->target_addr - XIP_BASE, b->data, FLASH_PAGE_SIZE);
                    }

                    // Erase prog info also!
                    FLASH_ERASE((uint32_t)&_prog_info_record - XIP_BASE, FLASH_SECTOR_SIZE);

                    s->prog_addr = b->target_addr;
                    s->num_blks = b->num_blocks;
                }
                else
                {
                    // Not the first block -------

                    if (!check_block(s, b))
                    {
                        xm_recv_cancel();
                        break;
                    }

                    FLASH_PROG(b->target_addr - XIP_BASE, b->data, FLASH_PAGE_SIZE);
                }

                s->num_blks_recv++;
                s->num_pkts_recv = 0;
                res = xm_recv_packet(buf, s->next_pkt_num,
                                     HACHI_CONFIG_XMODEM_PACKET_TIMEOUT,
                                     HACHI_CONFIG_XMODEM_DATA_TIMEOUT);
                continue;
            }
            else if (XM_RES_EOT == res)
            {
                // Empty program file
                if (0 == s->num_blks)
                {
                    break;
                }

                if (!check_EOT(s))
                {
                    break;
                }

                uint8_t a1[FLASH_PAGE_SIZE];
                {
                    prog_info_t *p1 = (prog_info_t *)a1;
                    p1->prog_addr = s->prog_addr;
                    p1->size = FLASH_PAGE_SIZE * s->num_blks;
                }

                FLASH_PROG((uint32_t)&_prog_info_record - XIP_BASE, a1, FLASH_PAGE_SIZE);

                break;
            }
            else // XM_RES_CAN
                 // XM_RES_ABORT
            {
                break;
            }

        } // while

        // When we reach here the programming process has finished, with either
        // success or not ..
        // 0. if we received an empty file (we received EOT instead of the initial
        //  data packet for example), neither the program info record nor the
        //  program area was touched
        // 1. or if it's a success we have properly saved the program code & updated
        //  the program info record
        // 2. or if we failed as early as at the first block, as in situation #0
        //  there is not a single flash sector ever been touched
        // 3. otherwise we have erased the program info record at the arrival of the
        //  first block
        // In either case of the above, we end up with a proper state, and there
        // is nothing futher to do

    } // while

    return 0;
}

static void uart_config()
{
    uart_init(UARTx, UART_BAUD);
    gpio_set_function(PICO_DEFAULT_UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(PICO_DEFAULT_UART_RX_PIN, GPIO_FUNC_UART);
}

static void prog_state_init(prog_state_t *p)
{
    memset(p, 0, sizeof(prog_state_t));
    p->next_pkt_num = 1;
}

static bool check_generic_block(const struct uf2_block *b)
{
    if (UF2_MAGIC_START0 != b->magic_start0 || UF2_MAGIC_START1 != b->magic_start1 //
        || UF2_MAGIC_END != b->magic_end)
    {
        return false;
    }
    if (0 != (UF2_FLAG_NOT_MAIN_FLASH & b->flags))
    {
        return false;
    }
    if (0 != b->target_addr % FLASH_PAGE_SIZE)
    {
        return false;
    }
    if (b->target_addr < PROG_AREA_BEGIN || b->target_addr >= PROG_AREA_END)
    {
        return false;
    }
    if (FLASH_PAGE_SIZE != b->payload_size)
    {
        return false;
    }
    if (0 == b->num_blocks)
    {
        return false;
    }
    if (b->block_no >= b->num_blocks)
    {
        return false;
    }

    if (0 != (UF2_FLAG_FAMILY_ID_PRESENT & b->flags) && RP2040_FAMILY_ID != b->file_size)
    {
        return false;
    }

    return true;
}

static bool check_1st_block(const struct uf2_block *b)
{
    if (!check_generic_block(b))
    {
        return false;
    }

    if (0 != b->block_no)
    {
        return false;
    }

    if (b->target_addr + FLASH_PAGE_SIZE * b->num_blocks > PROG_AREA_END)
    {
        return false;
    }

    return true;
}

static bool check_block(const prog_state_t *s, const struct uf2_block *b)
{
    if (!check_generic_block(b))
    {
        return false;
    }

    if (s->num_blks != b->num_blocks)
    {
        return false;
    }
    if (s->num_blks_recv != b->block_no)
    {
        return false;
    }
    if (s->prog_addr + FLASH_PAGE_SIZE * s->num_blks_recv != b->target_addr)
    {
        return false;
    }

    return true;
}

static bool check_EOT(const prog_state_t *s)
{
    if (s->num_blks != s->num_blks_recv)
    {
        return false;
    }
    if (0 != s->num_pkts_recv)
    {
        return false;
    }

    // Note: (0 == num_blks_recv) is OK

    return true;
}

static bool check_prog_info(const prog_info_t *p)
{
    if (p->prog_addr < PROG_AREA_BEGIN || p->prog_addr >= PROG_AREA_END)
    {
        return false;
    }
    if (0 == p->size || 0xffffffff == p->size)
    {
        return false;
    }

    // We'll not examine the vector table..

    return true;
}

static void run_prog(const prog_info_t *p)
{
    uint32_t vec_addr = p->prog_addr + BOOT2_SIZE;
    extern void run_prog_flash(uint32_t);
    run_prog_flash(vec_addr);
}
