/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : ota.h
  * @brief          : Header for ota.c file (Web Server & OTA Firmware Updater)
  *                   - Socket 0 (Port 80): Web Dashboard, Config, Cmnd, HTML Flash,
  *                     and OTA Staging binary upload.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __OTA_H
#define __OTA_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ==============================================================================
 * PARTISI FLASH EKSTERNAL (OctoSPI Physical Offset)
 * ============================================================================== */
#define FLASH_APP_META        0x00000000U
#define FLASH_APP_BIN         0x00001000U
#define FLASH_STAGING_META    0x00100000U  /* 1 MB offset (Sektor 256) */
#define FLASH_STAGING_BIN     0x00101000U  /* 1 MB + 4 KB (Sektor 257) */

#define MAX_FIRMWARE_SIZE     (1020UL * 1024UL)

/* ==============================================================================
 * FUNGSI PUBLIK OTA / WEB SERVER
 * ============================================================================== */
/**
  * @brief  Inisialisasi modul OTA / Web Server (Socket 0, Port 80)
  */
void OTA_Init(void);

/**
  * @brief  Proses event HTTP request & OTA upload (Socket 0)
  *         Dipanggil di main while (1) loop saat koneksi DHCP aktif.
  */
void OTA_Process(void);

/**
  * @brief  Alias kompatibilitas mundur dengan nama lama
  */
void Process_Web_OTA(void);

#ifdef __cplusplus
}
#endif

#endif /* __OTA_H */
