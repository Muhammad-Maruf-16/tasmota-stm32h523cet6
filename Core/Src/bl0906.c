/**
 ******************************************************************************
 * @file    bl0906.c
 * @brief   Driver implementation for Shanghai Belling BL0906
 *          Energy Metering IC using STM32 UART5 Low-Layer (LL) driver.
 ******************************************************************************
 */

#include "bl0906.h"
#include "main.h"
#include <string.h>

extern volatile uint32_t g_ms_tick;

/* Query Interval & Timeouts */
#define BL0906_POLL_INTERVAL_MS      1000    /* Polling seluruh channel setiap 1 detik */
#define BL0906_INTER_REG_DELAY_MS    15      /* Jeda antar request register (15 ms) */
#define BL0906_RESP_TIMEOUT_MS       120     /* Waktu tunggu respon 4 byte dari BL0906 (120 ms) */

/* Query Register Types */
typedef enum {
    BL_TYPE_VOLTAGE = 0,
    BL_TYPE_FREQ,
    BL_TYPE_TEMP,
    BL_TYPE_CURRENT,
    BL_TYPE_POWER,
    BL_TYPE_ENERGY,
    BL_TYPE_SUM_POWER,
    BL_TYPE_SUM_ENERGY
} BL0906_RegType_t;

typedef struct {
    uint8_t          reg;
    BL0906_RegType_t type;
    uint8_t          channel;   /* 0..5 untuk channel 1..6, atau 0 untuk global */
} BL0906_QueryItem_t;

/* Daftar register yang di-query berurutan pada setiap siklus */
static const BL0906_QueryItem_t s_query_table[] = {
    { BL0906_REG_V_RMS,       BL_TYPE_VOLTAGE,    0 },
    { BL0906_REG_FREQUENCY,   BL_TYPE_FREQ,       0 },
    { BL0906_REG_TEMPERATURE, BL_TYPE_TEMP,       0 },

    /* Channel 1 */
    { BL0906_REG_I_1_RMS,     BL_TYPE_CURRENT,    0 },
    { BL0906_REG_WATT_1,      BL_TYPE_POWER,      0 },
    { BL0906_REG_CF_1_CNT,    BL_TYPE_ENERGY,     0 },

    /* Channel 2 */
    { BL0906_REG_I_2_RMS,     BL_TYPE_CURRENT,    1 },
    { BL0906_REG_WATT_2,      BL_TYPE_POWER,      1 },
    { BL0906_REG_CF_2_CNT,    BL_TYPE_ENERGY,     1 },

    /* Channel 3 */
    { BL0906_REG_I_3_RMS,     BL_TYPE_CURRENT,    2 },
    { BL0906_REG_WATT_3,      BL_TYPE_POWER,      2 },
    { BL0906_REG_CF_3_CNT,    BL_TYPE_ENERGY,     2 },

    /* Channel 4 */
    { BL0906_REG_I_4_RMS,     BL_TYPE_CURRENT,    3 },
    { BL0906_REG_WATT_4,      BL_TYPE_POWER,      3 },
    { BL0906_REG_CF_4_CNT,    BL_TYPE_ENERGY,     3 },

    /* Channel 5 */
    { BL0906_REG_I_5_RMS,     BL_TYPE_CURRENT,    4 },
    { BL0906_REG_WATT_5,      BL_TYPE_POWER,      4 },
    { BL0906_REG_CF_5_CNT,    BL_TYPE_ENERGY,     4 },

    /* Channel 6 */
    { BL0906_REG_I_6_RMS,     BL_TYPE_CURRENT,    5 },
    { BL0906_REG_WATT_6,      BL_TYPE_POWER,      5 },
    { BL0906_REG_CF_6_CNT,    BL_TYPE_ENERGY,     5 },

    /* Total / Summary */
    { BL0906_REG_WATT_SUM,    BL_TYPE_SUM_POWER,  0 },
    { BL0906_REG_CF_SUM_CNT,  BL_TYPE_SUM_ENERGY, 0 }
};

#define BL0906_TOTAL_QUERIES (sizeof(s_query_table) / sizeof(s_query_table[0]))

/* Global Measured Data */
BL0906_Data_t g_bl0906_data;

/* Calibration Data */
static BL0906_Calib_t s_calib;

/* RX Ring Buffer */
static volatile uint8_t  s_rx_buf[BL0906_RX_BUF_SIZE];
static volatile uint16_t s_rx_head = 0;
static volatile uint16_t s_rx_tail = 0;

/* State Machine Variables */
typedef enum {
    SM_IDLE = 0,
    SM_SEND_REQ,
    SM_WAIT_RESP,
    SM_INTER_DELAY
} BL0906_SmState_t;

static BL0906_SmState_t s_sm_state      = SM_IDLE;
static uint8_t          s_query_idx     = 0;
static uint32_t         s_last_cycle_ms = 0;
static uint32_t         s_step_timer_ms = 0;

/* Hardware Helper: Kirim 1 byte via UART5 LL */
static void BL0906_SendByte(uint8_t b)
{
    uint32_t timeout = 200000;
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5))
    {
        if (--timeout == 0) return;
    }
    LL_USART_TransmitData8(UART5, b);
}

/* Helper: Kirim array via UART5 LL */
static void BL0906_SendArray(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        BL0906_SendByte(data[i]);
    }
}

/* Helper: Hitung jumlah byte yang ada di ring buffer */
static inline uint16_t BL0906_RxAvailable(void)
{
    return (uint16_t)((s_rx_head - s_rx_tail + BL0906_RX_BUF_SIZE) % BL0906_RX_BUF_SIZE);
}

/* Helper: Baca 1 byte dari ring buffer */
static inline uint8_t BL0906_RxReadByte(void)
{
    uint8_t b = 0;
    if (s_rx_head != s_rx_tail)
    {
        b = s_rx_buf[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1) % BL0906_RX_BUF_SIZE);
    }
    return b;
}

/* Helper: Buang sisa byte di buffer */
static inline void BL0906_RxFlush(void)
{
    s_rx_tail = s_rx_head;
}

/* Decode 24-bit Signed integer (Little Endian: L, M, H) */
static inline int32_t decode_s24(uint8_t l, uint8_t m, uint8_t h)
{
    int32_t val = (int32_t)((uint32_t)h << 16 | (uint32_t)m << 8 | (uint32_t)l);
    if (val & 0x00800000)
    {
        val |= (int32_t)0xFF000000; /* Sign extension */
    }
    return val;
}

/* Decode 24-bit Unsigned integer (Little Endian: L, M, H) */
static inline uint32_t decode_u24(uint8_t l, uint8_t m, uint8_t h)
{
    return ((uint32_t)h << 16 | (uint32_t)m << 8 | (uint32_t)l);
}

/* Public Callback: Dipanggil dari UART5_IRQHandler */
void BL0906_RxByteCallback(uint8_t byte)
{
    g_bl0906_data.rx_debug_buf[g_bl0906_data.raw_rx_bytes % 16] = byte;
    g_bl0906_data.raw_rx_bytes++;
    g_bl0906_data.last_rx_byte = byte;

    uint16_t next = (uint16_t)((s_rx_head + 1) % BL0906_RX_BUF_SIZE);
    if (next != s_rx_tail)
    {
        s_rx_buf[s_rx_head] = byte;
        s_rx_head = next;
    }
}

/* Inisialisasi Driver */
void BL0906_Init(void)
{
    memset(&g_bl0906_data, 0, sizeof(g_bl0906_data));

    /* Load faktor kalibrasi default */
    s_calib.u_ref     = BL0906_DEF_UREF;
    s_calib.p_sum_ref = BL0906_DEF_WATT_SUM;
    s_calib.e_sum_ref = BL0906_DEF_CF_SUM;

    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        s_calib.i_ref[i] = BL0906_DEF_IREF;
        s_calib.p_ref[i] = BL0906_DEF_PREF;
        s_calib.e_ref[i] = BL0906_DEF_EREF;
    }

    BL0906_RxFlush();
    s_sm_state = SM_IDLE;
    s_last_cycle_ms = g_ms_tick;

    /* Kirim perintah unlock register operasi BL0906 */
    static const uint8_t unlock_cmd[6] = { 0xCA, 0x9E, 0x55, 0x55, 0x00, 0xB7 };
    BL0906_SendArray(unlock_cmd, sizeof(unlock_cmd));
}

/* Set Kalibrasi Kustom */
void BL0906_SetCalib(const BL0906_Calib_t *calib)
{
    if (calib != NULL)
    {
        memcpy(&s_calib, calib, sizeof(BL0906_Calib_t));
    }
}

/* Ambil Pointer Data */
const BL0906_Data_t* BL0906_GetData(void)
{
    return &g_bl0906_data;
}

/* Reset Akumulasi Energi */
void BL0906_ResetEnergy(void)
{
    /* Format Reset: Command 0xCA, Reg 0x9F, Data 0x5A, 0x5A, 0x5A, Checksum 0x52 */
    static const uint8_t reset_cmd[6] = { BL0906_CMD_WRITE, BL0906_REG_SOFT_RESET, 0x5A, 0x5A, 0x5A, 0x52 };
    BL0906_SendArray(reset_cmd, sizeof(reset_cmd));
}

/* Parser Hasil Register */
static void BL0906_ParseRegister(const BL0906_QueryItem_t *item, uint8_t l, uint8_t m, uint8_t h)
{
    uint32_t raw_u = decode_u24(l, m, h);
    int32_t  raw_s = decode_s24(l, m, h);

    switch (item->type)
    {
        case BL_TYPE_VOLTAGE:
            g_bl0906_data.voltage = (float)raw_u * s_calib.u_ref;
            break;

        case BL_TYPE_FREQ:
            if (raw_u > 0)
            {
                g_bl0906_data.frequency = 10000000.0f / (float)raw_u;
            }
            break;

        case BL_TYPE_TEMP:
            /* Formula suhu internal Belling: ((raw - 64) * 12.5 / 59) - 40 */
            g_bl0906_data.temperature = (((float)raw_s - 64.0f) * 12.5f / 59.0f) - 40.0f;
            break;

        case BL_TYPE_CURRENT:
            if (item->channel < BL0906_NUM_CHANNELS)
            {
                g_bl0906_data.current[item->channel] = (float)raw_u * s_calib.i_ref[item->channel];
            }
            break;

        case BL_TYPE_POWER:
            if (item->channel < BL0906_NUM_CHANNELS)
            {
                g_bl0906_data.power[item->channel] = (float)raw_s * s_calib.p_ref[item->channel];
            }
            break;

        case BL_TYPE_ENERGY:
            if (item->channel < BL0906_NUM_CHANNELS)
            {
                g_bl0906_data.energy[item->channel] = (float)raw_u * s_calib.e_ref[item->channel];
            }
            break;

        case BL_TYPE_SUM_POWER:
            g_bl0906_data.total_power = (float)raw_s * s_calib.p_sum_ref;
            break;

        case BL_TYPE_SUM_ENERGY:
            g_bl0906_data.total_energy = (float)raw_u * s_calib.e_sum_ref;
            break;

        default:
            break;
    }
}

/* State Machine Utama non-blocking */
void BL0906_Process(void)
{
    uint32_t now = g_ms_tick;

    /* Update status live diagnostics */
    g_bl0906_data.sm_state     = (uint8_t)s_sm_state;
    g_bl0906_data.rx_buf_avail = BL0906_RxAvailable();

    switch (s_sm_state)
    {
        case SM_IDLE:
            /* Cek apakah sudah waktunya polling cycle baru */
            if ((now - s_last_cycle_ms) >= BL0906_POLL_INTERVAL_MS)
            {
                s_last_cycle_ms = now;
                s_query_idx = 0;
                s_sm_state = SM_SEND_REQ;
            }
            break;

        case SM_SEND_REQ:
            if (s_query_idx < BL0906_TOTAL_QUERIES)
            {
                const BL0906_QueryItem_t *item = &s_query_table[s_query_idx];
                g_bl0906_data.current_query_reg = item->reg;

                /* JANGAN flush buffer di sini agar byte respon in-transit tidak hilang! */

                /* Kirim format baca: 0x35 <reg_addr> */
                BL0906_SendByte(BL0906_CMD_READ);
                BL0906_SendByte(item->reg);

                s_step_timer_ms = now;
                s_sm_state = SM_WAIT_RESP;
            }
            else
            {
                /* Selesai 1 siklus penuh semua register */
                s_sm_state = SM_IDLE;
            }
            break;

        case SM_WAIT_RESP:
        {
            const BL0906_QueryItem_t *item = &s_query_table[s_query_idx];
            bool frame_found = false;

            /* Cari frame 4 byte yang valid (L, M, H, Checksum) menggunakan sliding window */
            while (BL0906_RxAvailable() >= 4)
            {
                uint8_t b_l  = s_rx_buf[s_rx_tail];
                uint8_t b_m  = s_rx_buf[(s_rx_tail + 1) % BL0906_RX_BUF_SIZE];
                uint8_t b_h  = s_rx_buf[(s_rx_tail + 2) % BL0906_RX_BUF_SIZE];
                uint8_t b_cs = s_rx_buf[(s_rx_tail + 3) % BL0906_RX_BUF_SIZE];

                /* Checksum formula BL0906: (Addr + L + M + H) ^ 0xFF */
                uint8_t expected_cs = (uint8_t)((item->reg + b_l + b_m + b_h) ^ 0xFF);
                g_bl0906_data.last_expected_cs = expected_cs;
                g_bl0906_data.last_calc_cs     = b_cs;

                if (b_cs == expected_cs)
                {
                    /* Frame valid ditemukan! Konsumsi 4 byte */
                    s_rx_tail = (uint16_t)((s_rx_tail + 4) % BL0906_RX_BUF_SIZE);
                    BL0906_ParseRegister(item, b_l, b_m, b_h);
                    g_bl0906_data.is_online = true;
                    g_bl0906_data.last_success_ms = now;
                    g_bl0906_data.update_count++;
                    frame_found = true;
                    break;
                }
                else
                {
                    /* Checksum mismatch: buang 1 byte pertama agar window bergeser */
                    s_rx_tail = (uint16_t)((s_rx_tail + 1) % BL0906_RX_BUF_SIZE);
                    g_bl0906_data.checksum_err_count++;
                }
            }

            if (frame_found)
            {
                s_step_timer_ms = now;
                s_sm_state = SM_INTER_DELAY;
            }
            else if ((now - s_step_timer_ms) >= BL0906_RESP_TIMEOUT_MS)
            {
                /* Timeout: IC tidak membalas dalam batas waktu */
                g_bl0906_data.timeout_err_count++;

                /* Jika sering timeout berturut-turut (> 3 detik), tandai offline */
                if ((now - g_bl0906_data.last_success_ms) > 3000)
                {
                    g_bl0906_data.is_online = false;
                }

                s_step_timer_ms = now;
                s_sm_state = SM_INTER_DELAY;
            }
            break;
        }

        case SM_INTER_DELAY:
            /* Beri jeda kecil agar jalur serial stabil */
            if ((now - s_step_timer_ms) >= BL0906_INTER_REG_DELAY_MS)
            {
                s_query_idx++;
                s_sm_state = SM_SEND_REQ;
            }
            break;

        default:
            s_sm_state = SM_IDLE;
            break;
    }
}
