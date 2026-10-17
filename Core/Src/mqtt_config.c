#include "mqtt_config.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// FORWARD DECLARATION — fungsi SPI Flash dari main.c
// =============================================================================
extern void ExtFlash_EraseSector(uint32_t addr);
extern void ExtFlash_WritePage  (uint32_t addr, const uint8_t *data, uint16_t len);
extern void Flash_ReadBytes     (uint32_t addr, uint8_t *buf, uint32_t len);

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================

void MqttConfig_SetDefault(MqttConfig_t *cfg)
{
    memset(cfg, 0, sizeof(MqttConfig_t));
    strncpy(cfg->host,       MQTT_DEFAULT_HOST,       sizeof(cfg->host)       - 1);
    strncpy(cfg->client_id,  "STM32_W5500",           sizeof(cfg->client_id)  - 1);
    strncpy(cfg->user,       MQTT_DEFAULT_USER,       sizeof(cfg->user)       - 1);
    strncpy(cfg->password,   MQTT_DEFAULT_PASSWORD,   sizeof(cfg->password)   - 1);
    strncpy(cfg->topic,      MQTT_DEFAULT_TOPIC,      sizeof(cfg->topic)      - 1);
    strncpy(cfg->full_topic, MQTT_DEFAULT_FULL_TOPIC, sizeof(cfg->full_topic) - 1);
    cfg->port        = MQTT_DEFAULT_PORT;
    cfg->tele_period = MQTT_DEFAULT_TELE_PERIOD;
    cfg->enabled     = MQTT_DEFAULT_ENABLED;
    cfg->magic       = MQTT_CONFIG_MAGIC;
}

int MqttConfig_Load(MqttConfig_t *cfg)
{
    MqttConfig_t tmp;
    memset(&tmp, 0, sizeof(MqttConfig_t));
    Flash_ReadBytes(FLASH_MQTT_META, (uint8_t *)&tmp, sizeof(MqttConfig_t));
    if (tmp.magic == MQTT_CONFIG_MAGIC)
    {
        memcpy(cfg, &tmp, sizeof(MqttConfig_t));
        return 0;
    }

    MqttConfig_SetDefault(cfg);
    return 1;
}

int MqttConfig_Save(const MqttConfig_t *cfg)
{
    ExtFlash_EraseSector(FLASH_MQTT_META);
    const uint8_t *ptr       = (const uint8_t *)cfg;
    uint32_t       remaining = sizeof(MqttConfig_t);
    uint32_t       addr      = FLASH_MQTT_META;
    while (remaining > 0)
    {
        uint16_t page_offset = (uint16_t)(addr & 0xFF);
        uint16_t space       = 256 - page_offset;
        uint16_t chunk       = (remaining > space) ? space : (uint16_t)remaining;
        ExtFlash_WritePage(addr, ptr, chunk);
        ptr       += chunk;
        addr      += chunk;
        remaining -= chunk;
    }

    // Verifikasi magic
    uint32_t magic_check = 0;
    Flash_ReadBytes(FLASH_MQTT_META + __builtin_offsetof(MqttConfig_t, magic),
                    (uint8_t *)&magic_check, 4);
    return (magic_check == MQTT_CONFIG_MAGIC) ? 0 : -1;
}

void MqttConfig_BuildTopic(const MqttConfig_t *cfg,
                            const char *prefix,
                            char *out, uint16_t out_len)
{
    // Ganti %prefix% → prefix, %topic% → cfg->topic
    // Contoh: "%prefix%/%topic%/" + prefix="cmnd" + topic="stm32"
    //         → "cmnd/stm32/"
    const char *src = cfg->full_topic;
    uint16_t    i   = 0;
    while (*src && i < out_len - 1)
    {
        if (strncmp(src, "%prefix%", 8) == 0)
        {
            uint16_t plen = (uint16_t)strlen(prefix);
            if (i + plen >= out_len - 1) break;
            memcpy(&out[i], prefix, plen);
            i   += plen;
            src += 8;
        }
        else if (strncmp(src, "%topic%", 7) == 0)
        {
            uint16_t tlen = (uint16_t)strlen(cfg->topic);
            if (i + tlen >= out_len - 1) break;
            memcpy(&out[i], cfg->topic, tlen);
            i   += tlen;
            src += 7;
        }
        else
        {
            out[i++] = *src++;
        }
    }
    out[i] = '\0';
}
