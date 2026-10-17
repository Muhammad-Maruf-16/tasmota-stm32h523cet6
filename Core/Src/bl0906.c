/**
 ******************************************************************************
 * @file    bl0906.c
 * @brief   Driver implementation for Shanghai Belling BL0906
 *          Energy Metering IC using STM32 UART5 Low-Layer (LL) driver.
 *          Directly adapted from proven ESP32 working implementation.
 ******************************************************************************
 */

#include "bl0906.h"
#include "main.h"
#include <string.h>

extern volatile uint32_t g_ms_tick;

/* Query Interval & Delays (sesuai implementasi ESP32 yang terbukti bekerja) */
#define BL0906_POLL_INTERVAL_MS      1000    /* Polling setiap 1 detik */
#define BL0906_UNLOCK_DELAY_MS       50      /* delay(50) setelah unlock */
#define BL0906_INTER_REG_DELAY_MS    30      /* delay(30) antar register */
#define BL0906_RESP_TIMEOUT_MS       100     /* timeout 100 ms per register */

/* Kalibrasi Presisi Channel 1 - 6 dari ESP32 */
static const float s_calib_ch[BL0906_NUM_CHANNELS] = {
    0.2002f, /* Kalibrasi CH1 */
    0.2058f, /* Kalibrasi CH2 */
    0.4242f, /* Kalibrasi CH3 */
    0.2091f, /* Kalibrasi CH4 */
    0.2052f, /* Kalibrasi CH5 */
    0.2108f  /* Kalibrasi CH6 */
};

/* Query Register Item */
typedef struct {
    uint8_t reg;
    uint8_t channel; /* 0..5 untuk CH1..6, atau 0xFF untuk voltage */
} BL0906_QueryItem_t;

/* Daftar register yang dibaca (CH1..CH6 Arus RMS + Tegangan RMS) */
static const BL0906_QueryItem_t s_query_table[] = {
    { BL0906_REG_I_1_RMS, 0 },  /* 0x0D - CH1 */
    { BL0906_REG_I_2_RMS, 1 },  /* 0x0E - CH2 */
    { BL0906_REG_I_3_RMS, 2 },  /* 0x0F - CH3 */
    { BL0906_REG_I_4_RMS, 3 },  /* 0x10 - CH4 */
    { BL0906_REG_I_5_RMS, 4 },  /* 0x13 - CH5 */
    { BL0906_REG_I_6_RMS, 5 },  /* 0x14 - CH6 */
    { BL0906_REG_V_RMS,   0xFF }/* 0x16 - Voltage */
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
    SM_UNLOCK,
    SM_UNLOCK_WAIT,
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

/* Helper: Kirim array via UART5 LL dan tunggu transmisi selesai (TC) */
static void BL0906_SendArray(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        BL0906_SendByte(data[i]);
    }
    uint32_t timeout = 200000;
    while (!LL_USART_IsActiveFlag_TC(UART5))
    {
        if (--timeout == 0) break;
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

/* Helper: Buang sisa data lama di antrean memori serial (sama seperti while(Serial2.available()) Serial2.read()) */
static inline void BL0906_RxFlush(void)
{
    s_rx_tail = s_rx_head;
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
        s_calib.i_ref[i] = (1.097f / 12875.0f) * s_calib_ch[i];
        s_calib.p_ref[i] = BL0906_DEF_PREF;
        s_calib.e_ref[i] = BL0906_DEF_EREF;
    }

    BL0906_RxFlush();
    s_sm_state = SM_IDLE;
    s_last_cycle_ms = g_ms_tick;
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
    static const uint8_t reset_cmd[6] = { BL0906_CMD_WRITE, BL0906_REG_SOFT_RESET, 0x5A, 0x5A, 0x5A, 0x52 };
    BL0906_SendArray(reset_cmd, sizeof(reset_cmd));
}

/* State Machine Utama non-blocking (adaptasi 1:1 dari logika working ESP32) */
void BL0906_Process(void)
{
    uint32_t now = g_ms_tick;

    /* Update status live diagnostics */
    g_bl0906_data.sm_state     = (uint8_t)s_sm_state;
    g_bl0906_data.rx_buf_avail = BL0906_RxAvailable();

    switch (s_sm_state)
    {
        case SM_IDLE:
            /* Setiap 1 detik (sama dengan delay(1000) di loop ESP32) */
            if ((now - s_last_cycle_ms) >= BL0906_POLL_INTERVAL_MS)
            {
                s_last_cycle_ms = now;
                s_sm_state = SM_UNLOCK;
            }
            break;

        case SM_UNLOCK:
        {
            /* Buka akses register BL0906 persis seperti bl0906_unlock() di ESP32 */
            static const uint8_t unlock_cmd[6] = { 0xCA, 0x9E, 0x00, 0x55, 0x55, 0x49 };
            BL0906_SendArray(unlock_cmd, sizeof(unlock_cmd));

            s_step_timer_ms = now;
            s_sm_state = SM_UNLOCK_WAIT;
            break;
        }

        case SM_UNLOCK_WAIT:
            /* Jeda 50ms setelah unlock persis seperti delay(50) di ESP32 */
            if ((now - s_step_timer_ms) >= BL0906_UNLOCK_DELAY_MS)
            {
                s_query_idx = 0;
                s_sm_state = SM_SEND_REQ;
            }
            break;

        case SM_SEND_REQ:
            if (s_query_idx < BL0906_TOTAL_QUERIES)
            {
                /* Bersihkan sisa data lama di antrean memori serial: while(Serial2.available()) Serial2.read() */
                BL0906_RxFlush();

                const BL0906_QueryItem_t *item = &s_query_table[s_query_idx];
                g_bl0906_data.current_query_reg = item->reg;

                /* Kirim format baca: 0x35 <reg_addr> */
                uint8_t cmd[2] = { BL0906_CMD_READ, item->reg };
                BL0906_SendArray(cmd, sizeof(cmd));

                s_step_timer_ms = now;
                s_sm_state = SM_WAIT_RESP;
            }
            else
            {
                /* Selesai 1 siklus pembacaan seluruh channel */
                s_sm_state = SM_IDLE;
            }
            break;

        case SM_WAIT_RESP:
        {
            const BL0906_QueryItem_t *item = &s_query_table[s_query_idx];

            /* Tunggu hingga 4 byte terkumpul di antrean serial */
            if (BL0906_RxAvailable() >= 4)
            {
                uint8_t b_l  = BL0906_RxReadByte(); /* rx_buf[0] = LSB */
                uint8_t b_m  = BL0906_RxReadByte(); /* rx_buf[1] = MID */
                uint8_t b_h  = BL0906_RxReadByte(); /* rx_buf[2] = MSB */
                uint8_t b_cs = BL0906_RxReadByte(); /* rx_buf[3] = Checksum */
                g_bl0906_data.last_calc_cs = b_cs;

                /* PERBAIKAN URUTAN BYTE: rx_buf[0] adalah LSB, rx_buf[2] adalah MSB (sama seperti ESP32) */
                uint32_t val = ((uint32_t)b_h << 16) | ((uint32_t)b_m << 8) | (uint32_t)b_l;

                if (item->channel < BL0906_NUM_CHANNELS)
                {
                    uint8_t ch = item->channel;
                    g_bl0906_data.arus_raw[ch] = val;

                    /* Rumus dasar BL0906 */
                    float arus_dasar = (float)val * 1.097f / 12875.0f;

                    /* Terapkan faktor kalibrasi ke masing-masing channel */
                    float arus = arus_dasar * s_calib_ch[ch];

                    /* Filter Noise: Jika arus sangat kecil (< 0.03 A), tampilkan 0 */
                    if (arus < 0.03f)
                    {
                        arus = 0.0f;
                    }
                    g_bl0906_data.current[ch] = arus;
                }
                else if (item->reg == BL0906_REG_V_RMS)
                {
                    g_bl0906_data.voltage = (float)val * BL0906_DEF_UREF;
                }

                /* Hitung estimasi total arus & daya aktif */
                float tot_i = 0.0f;
                for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
                {
                    tot_i += g_bl0906_data.current[i];
                }
                if (g_bl0906_data.voltage > 100.0f)
                {
                    g_bl0906_data.total_power = tot_i * g_bl0906_data.voltage;
                }

                g_bl0906_data.is_online = true;
                g_bl0906_data.last_success_ms = now;
                g_bl0906_data.update_count++;

                /* Lanjut ke jeda antar register */
                s_step_timer_ms = now;
                s_sm_state = SM_INTER_DELAY;
            }
            else if ((now - s_step_timer_ms) >= BL0906_RESP_TIMEOUT_MS)
            {
                /* Timeout 100ms sama persis dengan batas waktu ESP32 */
                g_bl0906_data.timeout_err_count++;

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
            /* Jeda 30ms agar IC BL0906 tidak kewalahan (persis delay(30) di ESP32) */
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
