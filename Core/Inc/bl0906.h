/**
 ******************************************************************************
 * @file    bl0906.h
 * @brief   Header file for Shanghai Belling BL0906 6-Channel Energy Metering IC
 *          Non-blocking UART driver for STM32H5 (LL Drivers)
 ******************************************************************************
 */

#ifndef __BL0906_H
#define __BL0906_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32h5xx.h"
#include <stdint.h>
#include <stdbool.h>

/* Number of metering channels in BL0906 */
#define BL0906_NUM_CHANNELS         6

/* Ring buffer size for UART RX */
#define BL0906_RX_BUF_SIZE          128

/* BL0906 UART Commands */
#define BL0906_CMD_READ             0x35
#define BL0906_CMD_WRITE            0xCA

/* BL0906 Register Addresses */
/* Current Channels 1 ~ 6 (RMS) */
#define BL0906_REG_I_1_RMS          0x0D
#define BL0906_REG_I_2_RMS          0x0E
#define BL0906_REG_I_3_RMS          0x0F
#define BL0906_REG_I_4_RMS          0x10
#define BL0906_REG_I_5_RMS          0x13
#define BL0906_REG_I_6_RMS          0x14

/* Voltage (RMS) */
#define BL0906_REG_V_RMS            0x16

/* Active Power Channels 1 ~ 6 (Watt) */
#define BL0906_REG_WATT_1           0x23
#define BL0906_REG_WATT_2           0x24
#define BL0906_REG_WATT_3           0x25
#define BL0906_REG_WATT_4           0x26
#define BL0906_REG_WATT_5           0x29
#define BL0906_REG_WATT_6           0x2A

/* Total Active Power (Watt) */
#define BL0906_REG_WATT_SUM         0x2C

/* Active Energy Pulse Count Channels 1 ~ 6 */
#define BL0906_REG_CF_1_CNT         0x30
#define BL0906_REG_CF_2_CNT         0x31
#define BL0906_REG_CF_3_CNT         0x32
#define BL0906_REG_CF_4_CNT         0x33
#define BL0906_REG_CF_5_CNT         0x36
#define BL0906_REG_CF_6_CNT         0x37

/* Total Active Energy Pulse Count */
#define BL0906_REG_CF_SUM_CNT       0x39

/* Line Frequency & Temperature */
#define BL0906_REG_FREQUENCY        0x4E
#define BL0906_REG_TEMPERATURE      0x5E

/* Control & Protection Registers */
#define BL0906_REG_USR_WRPROT       0x9E
#define BL0906_REG_SOFT_RESET       0x9F

/* Default Calibration Reference Constants (Athom EM6 / Belling standard reference) */
#define BL0906_DEF_UREF             (0.00083346f)
#define BL0906_DEF_IREF             (0.0000305f)
#define BL0906_DEF_PREF             (0.00058388f)
#define BL0906_DEF_EREF             (0.0003125f)
#define BL0906_DEF_WATT_SUM         (0.00058388f)
#define BL0906_DEF_CF_SUM           (0.0003125f)

/**
 * @brief Calibration factors structure
 */
typedef struct {
    float u_ref;                         /* Multiplier for Voltage (V) */
    float i_ref[BL0906_NUM_CHANNELS];    /* Multiplier for Current (A) */
    float p_ref[BL0906_NUM_CHANNELS];    /* Multiplier for Active Power (W) */
    float e_ref[BL0906_NUM_CHANNELS];    /* Multiplier for Energy (kWh) */
    float p_sum_ref;                     /* Multiplier for Total Power (W) */
    float e_sum_ref;                     /* Multiplier for Total Energy (kWh) */
} BL0906_Calib_t;

/**
 * @brief Data structure holding all measured parameters
 */
typedef struct {
    float voltage;                       /* Tegangan RMS (V) */
    float frequency;                     /* Frekuensi AC (Hz) */
    float temperature;                   /* Suhu internal IC (°C) */
    float current[BL0906_NUM_CHANNELS];  /* Arus Channel 1..6 (A) */
    uint32_t arus_raw[BL0906_NUM_CHANNELS]; /* Raw RMS value Channel 1..6 */
    float power[BL0906_NUM_CHANNELS];    /* Daya Aktif Channel 1..6 (W) */
    float energy[BL0906_NUM_CHANNELS];   /* Energi Channel 1..6 (kWh) */
    float total_power;                   /* Total Daya Aktif (W) */
    float total_energy;                  /* Total Energi (kWh) */

    /* Status & diagnostic counters */
    uint32_t raw_rx_bytes;               /* Total byte fisik yang berhasil diterima ISR */
    uint8_t  last_rx_byte;               /* Karakter byte terakhir yang diterima */
    uint8_t  rx_debug_buf[16];           /* 16 byte terakhir yang diterima (buffer sirkular) */
    uint8_t  current_query_reg;          /* Register yang sedang/terakhir di-query */
    uint8_t  sm_state;                   /* State machine state (0=IDLE, 1=REQ, 2=WAIT, 3=DELAY) */
    uint16_t rx_buf_avail;               /* Jumlah byte di ring buffer saat ini */
    uint8_t  last_expected_cs;           /* Expected checksum terakhir */
    uint8_t  last_calc_cs;               /* Checksum diterima terakhir */
    uint32_t overrun_err_count;          /* Overrun error counter */
    uint32_t framing_err_count;          /* Error framing (indikasi salah baudrate jika > 0) */
    uint32_t last_success_ms;            /* Waktu update sukses terakhir (ms tick) */
    uint32_t update_count;               /* Total pembacaan sukses */
    uint32_t checksum_err_count;         /* Total error checksum */
    uint32_t timeout_err_count;          /* Total timeout */
    bool is_online;                      /* true jika IC berhasil merespons */
} BL0906_Data_t;

/* Global data instance */
extern BL0906_Data_t g_bl0906_data;

/* Public Functions */

/**
 * @brief  Inisialisasi driver BL0906
 */
void BL0906_Init(void);

/**
 * @brief  Callback penerimaan byte UART dari ISR (panggil di UART5_IRQHandler)
 * @param  byte Karakter yang dibaca dari LL_USART_ReceiveData8(UART5)
 */
void BL0906_RxByteCallback(uint8_t byte);

/**
 * @brief  State machine polling non-blocking (panggil secara berkala di while(1) main)
 */
void BL0906_Process(void);

/**
 * @brief  Ambil pointer ke struct hasil pembacaan BL0906
 */
const BL0906_Data_t* BL0906_GetData(void);

/**
 * @brief  Set faktor kalibrasi kustom
 */
void BL0906_SetCalib(const BL0906_Calib_t *calib);

/**
 * @brief  Reset akumulasi energi di BL0906
 */
void BL0906_ResetEnergy(void);

#ifdef __cplusplus
}
#endif

#endif /* __BL0906_H */
