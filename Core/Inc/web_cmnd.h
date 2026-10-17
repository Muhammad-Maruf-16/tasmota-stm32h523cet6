#ifndef WEB_CMND_H
#define WEB_CMND_H

#include <stdint.h>

/**
 * @brief  Handle request HTTP untuk /cmnd
 *         - GET  /cmnd  → halaman HTML form CAN Command
 *         - POST /cmnd  → parse id + msg, kirim via CAN bus
 *
 * @param  rx_buf   Buffer HTTP request dari W5500 RX
 * @param  rx_len   Panjang data di buffer
 */
void WebCmnd_Handle(uint8_t *rx_buf, uint16_t rx_len);

#endif /* WEB_CMND_H */
