#ifndef MQTT_CONFIG_H
#define MQTT_CONFIG_H

#include <stdint.h>

// =============================================================================
// ALAMAT SPI FLASH UNTUK CONFIG MQTT
// =============================================================================
#define FLASH_MQTT_META     0x00200000U   // 4KB sector untuk config

// =============================================================================
// STRUCT CONFIG MQTT — persis field Tasmota
// =============================================================================
typedef struct {
    char     host       [64];  // MQTT Broker IP / hostname
    uint16_t port;             // Default: 1883
    char     client_id  [32];  // MQTT Client ID
    char     user       [32];  // MQTT Username
    char     password   [32];  // MQTT Password
    char     topic      [32];  // %topic% — identifier unik device
    char     full_topic [64];  // Full Topic template, default: "%prefix%/%topic%/"
    uint16_t tele_period;      // Telemetry interval detik, default: 300
    uint8_t  enabled;          // 1 = aktif, 0 = nonaktif
    uint8_t  _pad[1];          // padding 4-byte aligned
    uint32_t magic;            // 0xABCD1234 = config valid
} MqttConfig_t;

#define MQTT_CONFIG_MAGIC        0xABCD1234U
#define MQTT_CONFIG_SIZE         sizeof(MqttConfig_t)

// =============================================================================
// DEFAULT CONFIG — sama seperti Tasmota default
// =============================================================================
#define MQTT_DEFAULT_HOST        "192.168.1.100"
#define MQTT_DEFAULT_PORT        1883
#define MQTT_DEFAULT_CLIENT_ID   "STM32_%06X"    // diisi MAC 3 byte terakhir
#define MQTT_DEFAULT_USER        ""
#define MQTT_DEFAULT_PASSWORD    ""
#define MQTT_DEFAULT_TOPIC       "stm32"
#define MQTT_DEFAULT_FULL_TOPIC  "%prefix%/%topic%/"
#define MQTT_DEFAULT_TELE_PERIOD 300
#define MQTT_DEFAULT_ENABLED     1

// =============================================================================
// HELPER — parse full topic menjadi prefix tertentu
// Contoh: full_topic = "%prefix%/%topic%/"
//         topic      = "stm32_01"
//         prefix     = "cmnd"
//         result     = "cmnd/stm32_01/"
// =============================================================================
void MqttConfig_BuildTopic(const MqttConfig_t *cfg,
                            const char *prefix,
                            char *out, uint16_t out_len);

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================
int  MqttConfig_Load    (MqttConfig_t *cfg);
int  MqttConfig_Save    (const MqttConfig_t *cfg);
void MqttConfig_SetDefault(MqttConfig_t *cfg);

#endif /* MQTT_CONFIG_H */
