#ifndef MQTT_CLIENT_H
#define MQTT_CLIENT_H

#include <stdint.h>
#include "mqtt_config.h"

// =============================================================================
// KONFIGURASI
// =============================================================================
#define MQTT_SOCKET_BLOCK    0x48   // Socket 2 REG  (2*4+1)<<3 = 0x48
#define MQTT_TX_BLOCK        0x50   // Socket 2 TX   (2*4+2)<<3 = 0x50
#define MQTT_RX_BLOCK        0x58   // Socket 2 RX   (2*4+3)<<3 = 0x58
#define MQTT_S2_BUF_SIZE     2      // 2KB

#define MQTT_KEEPALIVE_SEC   60     // MQTT keepalive interval
#define MQTT_CONN_TIMEOUT_MS 5000   // timeout koneksi TCP ke broker
#define MQTT_BUF_SIZE        512    // buffer TX/RX MQTT packet

// =============================================================================
// STATE MACHINE
// =============================================================================
typedef enum {
    MQTT_STATE_DISABLED   = 0,
    MQTT_STATE_IDLE       = 1,   // belum konek
    MQTT_STATE_TCP_CONN   = 2,   // sedang konek TCP
    MQTT_STATE_MQTT_CONN  = 3,   // kirim CONNECT packet
    MQTT_STATE_CONNECTED  = 4,   // terhubung, publish/subscribe jalan
    MQTT_STATE_RECONNECT  = 5,   // tunggu sebelum reconnect
} MqttState_t;

// =============================================================================
// VARIABEL GLOBAL (untuk debug Live Expression)
// =============================================================================
extern MqttState_t g_mqtt_state;
extern uint8_t     g_mqtt_conn_count;   // jumlah koneksi berhasil
extern uint32_t    g_mqtt_pub_count;    // jumlah publish berhasil

// =============================================================================
// DATA SENSOR — isi variabel ini dari luar untuk di-publish
// =============================================================================
typedef struct {
    float    temperature;   // °C
    float    voltage;       // V
    // tambah field lain di sini nanti
} MqttSensorData_t;

extern MqttSensorData_t g_mqtt_sensor;

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================

/**
 * @brief  Inisialisasi MQTT client — alokasi buffer Socket 2.
 *         Dipanggil sekali di USER CODE BEGIN 2.
 */
void MQTT_Init(void);

/**
 * @brief  State machine MQTT — dipanggil setiap loop di main().
 *         Menangani koneksi, publish tele, subscribe cmnd, keepalive.
 * @param  cfg  Config MQTT (host, port, topic, dll)
 */
void MQTT_Process(const MqttConfig_t *cfg);

/**
 * @brief  Cek apakah MQTT sedang terhubung.
 * @retval 1 = connected, 0 = tidak
 */
uint8_t MQTT_IsConnected(void);

/**
 * @brief  Publish manual ke topic tertentu (QoS 0).
 * @param  topic    Full topic string
 * @param  payload  Data payload
 * @param  len      Panjang payload
 */
void MQTT_Publish(const char *topic, const uint8_t *payload, uint16_t len);

#endif /* MQTT_CLIENT_H */
