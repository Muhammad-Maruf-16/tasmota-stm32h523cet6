#ifndef WEB_CONFIG_H
#define WEB_CONFIG_H

#include <stdint.h>

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================
extern uint8_t g_config_updated;
/**
 * @brief  Inisialisasi web config — load config MQTT ke memory saat boot.
 *         Dipanggil sekali di USER CODE BEGIN 2.
 */
void WebConfig_Init(void);

/**
 * @brief  Handle request HTTP untuk /config dan /config/data.
 *         - GET  /config/data → JSON config saat ini
 *         - GET  /config      → halaman HTML form MQTT
 *         - POST /config      → parse form, simpan ke SPI Flash
 *
 * @param  rx_buf   Buffer yang sudah dibaca dari W5500 RX
 * @param  rx_len   Panjang data di buffer
 */
void WebConfig_Handle(uint8_t *rx_buf, uint16_t rx_len);

#endif /* WEB_CONFIG_H */
