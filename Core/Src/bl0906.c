/**
 ******************************************************************************
 * @file    bl0906.c
 * @brief   Driver implementation for Shanghai Belling BL0906
 *          Energy Metering IC using STM32 UART5 Low-Layer (LL) driver.
 *          Direct Synchronous Polling Mode (matching proven ESP32 logic).
 ******************************************************************************
 */

#include "bl0906.h"
#include "main.h"
#include <string.h>

extern volatile uint32_t g_ms_tick;

/* Query Interval & Delays */
#define BL0906_POLL_INTERVAL_MS      1000    /* Polling setiap 1 detik */
#define BL0906_UNLOCK_DELAY_MS       50      /* delay(50) setelah unlock */
#define BL0906_INTER_REG_DELAY_MS    30      /* delay(30) antar register */
#define BL0906_BYTE_TIMEOUT_MS       100     /* timeout 100 ms per byte response */

/* Kalibrasi Presisi Channel 1 - 6 dari ESP32 */
static const float s_calib_ch[BL0906_NUM_CHANNELS] = {
    0.2002f, /* Kalibrasi CH1 */
    0.2058f, /* Kalibrasi CH2 */
    0.4242f, /* Kalibrasi CH3 */
    0.2091f, /* Kalibrasi CH4 */
    0.2052f, /* Kalibrasi CH5 */
    0.2108f  /* Kalibrasi CH6 */
};

/* Register Addresses for Current 1..6 */
static const uint8_t s_current_regs[BL0906_NUM_CHANNELS] = {
    BL0906_REG_I_1_RMS, /* 0x0D */
    BL0906_REG_I_2_RMS, /* 0x0E */
    BL0906_REG_I_3_RMS, /* 0x0F */
    BL0906_REG_I_4_RMS, /* 0x10 */
    BL0906_REG_I_5_RMS, /* 0x13 */
    BL0906_REG_I_6_RMS  /* 0x14 */
};

/* Global Measured Data */
BL0906_Data_t g_bl0906_data;

/* Calibration Data */
static BL0906_Calib_t s_calib;

/* Polling Timing Timer */
static uint32_t s_last_cycle_ms = 0;

/* Helper: Kirim 1 byte via UART5 LL */
static void BL0906_SendByte(uint8_t b)
{
    uint32_t timeout = 200000;
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5))
    {
        if (--timeout == 0) return;
    }
    LL_USART_TransmitData8(UART5, b);
}

/* Helper: Kirim array dan tunggu seluruh bit selesai terkirim keluar dari pin (TC) */
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

/* Helper: Baca 1 byte via Polling Hardware Register (sama seperti Serial2.read()) */
static int BL0906_ReadBytePolling(uint8_t *b, uint32_t timeout_ms)
{
    uint32_t start = g_ms_tick;
    while (!LL_USART_IsActiveFlag_RXNE_RXFNE(UART5))
    {
        if (LL_USART_IsActiveFlag_ORE(UART5))
        {
            LL_USART_ClearFlag_ORE(UART5);
            g_bl0906_data.overrun_err_count++;
        }
        if (LL_USART_IsActiveFlag_FE(UART5))
        {
            LL_USART_ClearFlag_FE(UART5);
            g_bl0906_data.framing_err_count++;
        }
        if ((g_ms_tick - start) > timeout_ms)
        {
            return -1; /* Timeout */
        }
    }
    *b = LL_USART_ReceiveData8(UART5);
    g_bl0906_data.rx_debug_buf[g_bl0906_data.raw_rx_bytes % 16] = *b;
    g_bl0906_data.raw_rx_bytes++;
    g_bl0906_data.last_rx_byte = *b;
    return 0;
}

/* Callback jika ISR UART5 aktif (opsional/kompatibilitas) */
void BL0906_RxByteCallback(uint8_t byte)
{
    g_bl0906_data.rx_debug_buf[g_bl0906_data.raw_rx_bytes % 16] = byte;
    g_bl0906_data.raw_rx_bytes++;
    g_bl0906_data.last_rx_byte = byte;
}

/* Helper: Membuka kunci proteksi register BL0906 */
static void BL0906_Unlock(void)
{
    static const uint8_t unlock_cmd[6] = { 0xCA, 0x9E, 0x00, 0x55, 0x55, 0x49 };
    BL0906_SendArray(unlock_cmd, sizeof(unlock_cmd));
}

/* Helper: Baca 1 register dengan format BL0906 (0x35 <addr> -> 4 byte response) */
static bool BL0906_ReadReg(uint8_t addr, uint32_t *out_val)
{
    /* 1. Bersihkan sisa data lama di antrean: while(Serial2.available()) Serial2.read() */
    while (LL_USART_IsActiveFlag_RXNE_RXFNE(UART5))
    {
        (void)LL_USART_ReceiveData8(UART5);
    }
    if (LL_USART_IsActiveFlag_ORE(UART5)) LL_USART_ClearFlag_ORE(UART5);
    if (LL_USART_IsActiveFlag_FE(UART5))  LL_USART_ClearFlag_FE(UART5);

    /* 2. Kirim perintah baca: 0x35 <addr> */
    uint8_t cmd[2] = { BL0906_CMD_READ, addr };
    BL0906_SendArray(cmd, 2);

    /* 3. Baca 4 byte balasan: LSB, MID, MSB, Checksum */
    uint8_t rx[4] = {0, 0, 0, 0};
    for (int i = 0; i < 4; i++)
    {
        if (BL0906_ReadBytePolling(&rx[i], BL0906_BYTE_TIMEOUT_MS) != 0)
        {
            return false; /* Timeout */
        }
    }

    g_bl0906_data.last_calc_cs = rx[3];

    /* 4. PERBAIKAN URUTAN BYTE: rx[0] adalah LSB, rx[2] adalah MSB */
    *out_val = ((uint32_t)rx[2] << 16) | ((uint32_t)rx[1] << 8) | rx[0];
    return true;
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

/* Polling Process Utama (Dipanggil di while(1) main) */
void BL0906_Process(void)
{
    uint32_t now = g_ms_tick;

    /* Eksekusi setiap 1 detik (persis delay(1000) di loop ESP32) */
    if ((now - s_last_cycle_ms) < BL0906_POLL_INTERVAL_MS)
    {
        return;
    }
    s_last_cycle_ms = now;

    /* 1. Pastikan IC selalu terbuka sebelum dibaca (bl0906_unlock()) */
    BL0906_Unlock();

    /* delay(50) */
    uint32_t t_wait = g_ms_tick;
    while ((g_ms_tick - t_wait) < BL0906_UNLOCK_DELAY_MS);

    bool any_success = false;

    /* 2. Baca seluruh nilai channel arus 1 sampai 6 (bl0906_read_all()) */
    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        uint8_t reg = s_current_regs[i];
        g_bl0906_data.current_query_reg = reg;
        uint32_t raw_val = 0;

        if (BL0906_ReadReg(reg, &raw_val))
        {
            g_bl0906_data.arus_raw[i] = raw_val;

            /* Rumus dasar BL0906 */
            float arus_dasar = (float)raw_val * 1.097f / 12875.0f;

            /* Terapkan faktor kalibrasi ke masing-masing channel */
            float arus = arus_dasar * s_calib_ch[i];

            /* Filter Noise: Jika arus sangat kecil (< 0.03 A), tampilkan 0 */
            if (arus < 0.03f)
            {
                arus = 0.0f;
            }
            g_bl0906_data.current[i] = arus;
            g_bl0906_data.update_count++;
            any_success = true;
        }
        else
        {
            g_bl0906_data.timeout_err_count++;
        }

        /* Jeda waktu 30ms agar IC BL0906 tidak kewalahan (delay(30)) */
        t_wait = g_ms_tick;
        while ((g_ms_tick - t_wait) < BL0906_INTER_REG_DELAY_MS);
    }

    /* 3. Baca Tegangan RMS (0x16) */
    g_bl0906_data.current_query_reg = BL0906_REG_V_RMS;
    uint32_t raw_v = 0;
    if (BL0906_ReadReg(BL0906_REG_V_RMS, &raw_v))
    {
        g_bl0906_data.voltage = (float)raw_v * BL0906_DEF_UREF;
        any_success = true;
    }

    /* Hitung total arus & estimasi total daya */
    float tot_i = 0.0f;
    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        tot_i += g_bl0906_data.current[i];
    }
    if (g_bl0906_data.voltage > 100.0f)
    {
        g_bl0906_data.total_power = tot_i * g_bl0906_data.voltage;
    }

    if (any_success)
    {
        g_bl0906_data.is_online = true;
        g_bl0906_data.last_success_ms = now;
    }
    else if ((now - g_bl0906_data.last_success_ms) > 3000)
    {
        g_bl0906_data.is_online = false;
    }
}
