/**
  ******************************************************************************
  * @file    bl0906.c
  * @brief   Driver implementation for Shanghai Belling BL0906
  *          6-Channel Energy Metering IC using STM32 Low-Layer (LL) UART5.
  *          Based on official ESPHome component logic & Belling App Note.
  ******************************************************************************
  */

#include "bl0906.h"
#include "main.h"
#include <string.h>

/* Query Interval & Delays */
#define BL0906_POLL_INTERVAL_MS      1000    /* Polling setiap 1 detik */
#define BL0906_INTER_REG_DELAY_MS    10      /* Jeda stabilisasi antar register (ms) */
#define BL0906_BYTE_TIMEOUT_MS       100     /* Timeout respons per register (ms) */

/* Global Measured Data */
BL0906_Data_t g_bl0906_data;

/* Timer interval polling */
static uint32_t s_last_cycle_ms = 0;

/* Tabel alamat register Current 1 ~ 6 */
static const uint8_t s_reg_current[BL0906_NUM_CHANNELS] = {
    BL0906_REG_I_1_RMS, /* CH1: 0x0D */
    BL0906_REG_I_2_RMS, /* CH2: 0x0E */
    BL0906_REG_I_3_RMS, /* CH3: 0x0F */
    BL0906_REG_I_4_RMS, /* CH4: 0x10 */
    BL0906_REG_I_5_RMS, /* CH5: 0x13 */
    BL0906_REG_I_6_RMS  /* CH6: 0x14 */
};

/* Tabel alamat register Active Power 1 ~ 6 */
static const uint8_t s_reg_power[BL0906_NUM_CHANNELS] = {
    BL0906_REG_WATT_1,  /* CH1: 0x23 */
    BL0906_REG_WATT_2,  /* CH2: 0x24 */
    BL0906_REG_WATT_3,  /* CH3: 0x25 */
    BL0906_REG_WATT_4,  /* CH4: 0x26 */
    BL0906_REG_WATT_5,  /* CH5: 0x29 */
    BL0906_REG_WATT_6   /* CH6: 0x2A */
};

/* Tabel alamat register Energy Pulse Count 1 ~ 6 */
static const uint8_t s_reg_energy[BL0906_NUM_CHANNELS] = {
    BL0906_REG_CF_1_CNT, /* CH1: 0x30 */
    BL0906_REG_CF_2_CNT, /* CH2: 0x31 */
    BL0906_REG_CF_3_CNT, /* CH3: 0x32 */
    BL0906_REG_CF_4_CNT, /* CH4: 0x33 */
    BL0906_REG_CF_5_CNT, /* CH5: 0x36 */
    BL0906_REG_CF_6_CNT  /* CH6: 0x37 */
};

/**
  * @brief  Helper sign-extension 24-bit ke 32-bit bertanda (signed)
  */
static inline int32_t to_signed24(uint32_t val)
{
    if (val & 0x00800000UL)
    {
        return (int32_t)(val | 0xFF000000UL);
    }
    return (int32_t)val;
}

/**
  * @brief  Fungsi checksum resmi BL0906 (ESPHome standard):
  *         Checksum = ((Address + Data_L + Data_M + Data_H) & 0xFF) ^ 0xFF
  */
static inline uint8_t BL0906_CalcChecksum(uint8_t addr, uint8_t l, uint8_t m, uint8_t h)
{
    return (uint8_t)((addr + l + m + h) ^ 0xFF);
}

/**
  * @brief  Bersihkan data sampah dan flag error di hardware UART5 LL
  */
static void BL0906_FlushRx(void)
{
    LL_USART_ClearFlag_ORE(UART5);
    LL_USART_ClearFlag_FE(UART5);
    LL_USART_ClearFlag_NE(UART5);
    LL_USART_ClearFlag_PE(UART5);

    while (LL_USART_IsActiveFlag_RXNE_RXFNE(UART5))
    {
        (void)LL_USART_ReceiveData8(UART5);
    }
}

/**
  * @brief  Kirim 1 byte via UART5 LL
  */
static bool BL0906_SendByte(uint8_t byte)
{
    uint32_t timeout = 200000;
    while (!LL_USART_IsActiveFlag_TXE_TXFNF(UART5))
    {
        if (--timeout == 0) return false;
    }
    LL_USART_TransmitData8(UART5, byte);
    return true;
}

/**
  * @brief  Kirim array byte via UART5 LL dan tunggu bit terakhir keluar (TC = 1)
  */
static bool BL0906_SendArray(const uint8_t *data, uint16_t len)
{
    LL_USART_ClearFlag_TC(UART5);

    for (uint16_t i = 0; i < len; i++)
    {
        if (!BL0906_SendByte(data[i])) return false;
    }

    uint32_t timeout = 200000;
    while (!LL_USART_IsActiveFlag_TC(UART5))
    {
        if (--timeout == 0) return false;
    }
    return true;
}

/**
  * @brief  Menerima array byte via UART5 LL dengan timeout berbasis g_ms_tick
  */
static bool BL0906_ReceiveArray(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    uint16_t count = 0;
    uint32_t start_tick = g_ms_tick;

    while (count < len)
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
        if (LL_USART_IsActiveFlag_RXNE_RXFNE(UART5))
        {
            uint8_t b = LL_USART_ReceiveData8(UART5);
            buf[count++] = b;

            g_bl0906_data.rx_debug_buf[g_bl0906_data.raw_rx_bytes % 16] = b;
            g_bl0906_data.raw_rx_bytes++;
            g_bl0906_data.last_rx_byte = b;
        }

        if ((g_ms_tick - start_tick) >= timeout_ms)
        {
            g_bl0906_data.timeout_err_count++;
            return false;
        }
    }
    return true;
}

/**
  * @brief  Membaca 1 buah register 24-bit dari BL0906 dengan validasi checksum
  */
static bool BL0906_ReadReg(uint8_t addr, uint32_t *raw_out)
{
    if (raw_out == NULL) return false;

    g_bl0906_data.current_query_reg = addr;

    /* 1. Bersihkan sisa data lama di buffer UART5 */
    BL0906_FlushRx();

    /* 2. Kirim perintah Read: 0x35 <addr> */
    uint8_t tx_cmd[2] = { BL0906_CMD_READ, addr };
    if (!BL0906_SendArray(tx_cmd, 2))
    {
        return false;
    }

    /* 3. BL0906 membalas 4 byte: [Data_L, Data_M, Data_H, Checksum] */
    uint8_t rx_buf[4] = {0};
    if (!BL0906_ReceiveArray(rx_buf, 4, BL0906_BYTE_TIMEOUT_MS))
    {
        return false;
    }

    /* 4. Validasi Checksum */
    uint8_t expected_cs = BL0906_CalcChecksum(addr, rx_buf[0], rx_buf[1], rx_buf[2]);
    g_bl0906_data.last_calc_cs = rx_buf[3];
    g_bl0906_data.last_expected_cs = expected_cs;

    if (expected_cs != rx_buf[3])
    {
        g_bl0906_data.checksum_err_count++;
        return false; /* Data korup / checksum tidak cocok */
    }

    /* 5. Susun nilai 24-bit (Data_L + Data_M + Data_H) */
    *raw_out = ((uint32_t)rx_buf[2] << 16) | ((uint32_t)rx_buf[1] << 8) | (uint32_t)rx_buf[0];
    g_bl0906_data.update_count++;

    /* Jeda singkat antar register agar hardware BL0906 stabil (sesuai ESPHome standard) */
    uint32_t t_wait = g_ms_tick;
    while ((g_ms_tick - t_wait) < BL0906_INTER_REG_DELAY_MS);

    return true;
}

/**
  * @brief  Inisialisasi Driver BL0906
  */
void BL0906_Init(void)
{
    memset(&g_bl0906_data, 0, sizeof(g_bl0906_data));

    /* Buka proteksi penulisan register BL0906 (User Write Operation Enable resmi ESPHome) */
    static const uint8_t wr_enable_cmd[6] = { BL0906_CMD_WRITE, BL0906_REG_USR_WRPROT, 0x55, 0x55, 0x00, 0xB7 };
    BL0906_FlushRx();
    BL0906_SendArray(wr_enable_cmd, sizeof(wr_enable_cmd));

    uint32_t t_wait = g_ms_tick;
    while ((g_ms_tick - t_wait) < 20);

    s_last_cycle_ms = g_ms_tick;
}

/**
  * @brief  Ambil pointer ke struct hasil pembacaan BL0906
  */
const BL0906_Data_t* BL0906_GetData(void)
{
    return &g_bl0906_data;
}

/**
  * @brief  Set kalibrasi kustom (opsional)
  */
void BL0906_SetCalib(const BL0906_Calib_t *calib)
{
    (void)calib;
}

/**
  * @brief  Reset Akumulasi Energi
  */
void BL0906_ResetEnergy(void)
{
    static const uint8_t reset_cmd[6] = { BL0906_CMD_WRITE, BL0906_REG_SOFT_RESET, 0x5A, 0x5A, 0x5A, 0x52 };
    BL0906_SendArray(reset_cmd, sizeof(reset_cmd));

    uint32_t t_wait = g_ms_tick;
    while ((g_ms_tick - t_wait) < 50);

    /* Buka kembali proteksi penulisan */
    static const uint8_t wr_enable_cmd[6] = { BL0906_CMD_WRITE, BL0906_REG_USR_WRPROT, 0x55, 0x55, 0x00, 0xB7 };
    BL0906_SendArray(wr_enable_cmd, sizeof(wr_enable_cmd));
}

/**
  * @brief  Callback penerimaan byte UART dari ISR (kompatibilitas jika ISR diaktifkan)
  */
void BL0906_RxByteCallback(uint8_t byte)
{
    g_bl0906_data.rx_debug_buf[g_bl0906_data.raw_rx_bytes % 16] = byte;
    g_bl0906_data.raw_rx_bytes++;
    g_bl0906_data.last_rx_byte = byte;
}

/**
  * @brief  Polling Process Utama BL0906 (Dipanggil berkala di while(1) main)
  */
void BL0906_Process(void)
{
    uint32_t now = g_ms_tick;

    /* Eksekusi tiap 1 detik */
    if ((now - s_last_cycle_ms) < BL0906_POLL_INTERVAL_MS)
    {
        return;
    }
    s_last_cycle_ms = now;

    bool any_success = false;
    uint32_t raw_val = 0;

    /* 1. Baca Tegangan RMS (0x16) */
    if (BL0906_ReadReg(BL0906_REG_V_RMS, &raw_val))
    {
        g_bl0906_data.tegangan_raw = raw_val;
        g_bl0906_data.voltage = (float)raw_val * BL0906_DEF_UREF;
        any_success = true;
    }

    /* 2. Baca Frekuensi PLN (0x4E) */
    if (BL0906_ReadReg(BL0906_REG_FREQUENCY, &raw_val))
    {
        if (raw_val > 0)
        {
            g_bl0906_data.frequency = 10000000.0f / (float)raw_val;
        }
        else
        {
            g_bl0906_data.frequency = 0.0f;
        }
        any_success = true;
    }

    /* 3. Baca Suhu Internal Chip (0x5E) */
    if (BL0906_ReadReg(BL0906_REG_TEMPERATURE, &raw_val))
    {
        int32_t s24 = to_signed24(raw_val);
        g_bl0906_data.temperature = (((float)s24 - 64.0f) * 12.5f / 59.0f) - 40.0f;
        any_success = true;
    }

    /* 4. Baca Arus Channel 1 sampai 6 (0x0D..0x14) */
    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        if (BL0906_ReadReg(s_reg_current[i], &raw_val))
        {
            g_bl0906_data.arus_raw[i] = raw_val;
            float arus = (float)raw_val * BL0906_DEF_IREF;

            /* Filter noise di bawah 5 mA */
            if (arus < 0.005f)
            {
                arus = 0.0f;
            }
            g_bl0906_data.current[i] = arus;
            any_success = true;
        }
    }

    /* 5. Baca Daya Aktif Channel 1 sampai 6 (0x23..0x2A) */
    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        if (BL0906_ReadReg(s_reg_power[i], &raw_val))
        {
            int32_t s24 = to_signed24(raw_val);
            g_bl0906_data.power[i] = (float)s24 * BL0906_DEF_PREF;
            any_success = true;
        }
    }

    /* 6. Baca Total Daya Aktif 6 Channel (0x2C) */
    if (BL0906_ReadReg(BL0906_REG_WATT_SUM, &raw_val))
    {
        int32_t s24 = to_signed24(raw_val);
        g_bl0906_data.total_power = (float)s24 * BL0906_DEF_WATT_SUM;
        any_success = true;
    }

    /* 7. Baca Energi Channel 1 sampai 6 (0x30..0x37) */
    for (int i = 0; i < BL0906_NUM_CHANNELS; i++)
    {
        if (BL0906_ReadReg(s_reg_energy[i], &raw_val))
        {
            g_bl0906_data.energy[i] = (float)raw_val * BL0906_DEF_EREF;
            any_success = true;
        }
    }

    /* 8. Baca Total Energi (0x39) */
    if (BL0906_ReadReg(BL0906_REG_CF_SUM_CNT, &raw_val))
    {
        g_bl0906_data.total_energy = (float)raw_val * BL0906_DEF_CF_SUM;
        any_success = true;
    }

    /* Status Koneksi Online/Offline */
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
