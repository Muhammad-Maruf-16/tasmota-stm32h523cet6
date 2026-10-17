#include "web_config.h"
#include "mqtt_config.h"
#include "html_flash.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// FORWARD DECLARATION
// =============================================================================
extern void W5500_SendTCP(const uint8_t *data, uint16_t len);
uint8_t g_config_updated = 0;

// HTML_CONFIG_PAGE dihapus — halaman di-serve dari SPI Flash via HtmlFlash_SendPage()

// =============================================================================
// HELPER: URL decode
// =============================================================================
static void url_decode(char *dst, const char *src, size_t max_len)
{
    size_t i = 0;
    while (*src && i < max_len - 1)
    {
        if (*src == '%' && src[1] && src[2])
        {
            char hex[3] = { src[1], src[2], 0 };
            dst[i++] = (char)strtol(hex, NULL, 16);
            src += 3;
        }
        else if (*src == '+')
        {
            dst[i++] = ' ';
            src++;
        }
        else
        {
            dst[i++] = *src++;
        }
    }
    dst[i] = '\0';
}

// =============================================================================
// HELPER: Ambil field dari body URL-encoded
// =============================================================================
static void parse_field(const char *body, const char *key,
                         char *out, size_t out_len)
{
    char search[72];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_len) len = out_len - 1;
    static char tmp[200];
    if (len >= sizeof(tmp)) len = sizeof(tmp) - 1;
    memcpy(tmp, p, len);
    tmp[len] = '\0';
    url_decode(out, tmp, out_len);
}

// =============================================================================
// HELPER: Kirim HTTP response teks
// =============================================================================
static void send_response(uint16_t code, const char *code_str, const char *body)
{
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        code, code_str, (int)strlen(body));
    W5500_SendTCP((uint8_t *)hdr,  (uint16_t)strlen(hdr));
    W5500_SendTCP((uint8_t *)body, (uint16_t)strlen(body));
}

// =============================================================================
// HELPER: Kirim JSON config + topic preview
// =============================================================================
static void send_config_json(void)
{
    MqttConfig_t cfg;
    MqttConfig_Load(&cfg);
    // Build preview topic
    char t_cmnd[96], t_stat[96], t_tele[96];
    MqttConfig_BuildTopic(&cfg, "cmnd", t_cmnd, sizeof(t_cmnd));
    MqttConfig_BuildTopic(&cfg, "stat", t_stat, sizeof(t_stat));
    MqttConfig_BuildTopic(&cfg, "tele", t_tele, sizeof(t_tele));
    char json[512];
    snprintf(json, sizeof(json),
        "{"
        "\"host\":\"%s\","
        "\"port\":%d,"
        "\"client_id\":\"%s\","
        "\"user\":\"%s\","
        "\"password\":\"%s\","
        "\"topic\":\"%s\","
        "\"full_topic\":\"%s\","
        "\"tele_period\":%d,"
        "\"enabled\":%d,"
        "\"t_cmnd\":\"%s\","
        "\"t_stat\":\"%s\","
        "\"t_tele\":\"%s\""
        "}",
        cfg.host, cfg.port, cfg.client_id,
        cfg.user, cfg.password,
        cfg.topic, cfg.full_topic,
        cfg.tele_period, cfg.enabled,
        t_cmnd, t_stat, t_tele);
    char hdr[128];
    snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n\r\n",
        (int)strlen(json));
    W5500_SendTCP((uint8_t *)hdr,  (uint16_t)strlen(hdr));
    W5500_SendTCP((uint8_t *)json, (uint16_t)strlen(json));
}

// =============================================================================
// HELPER: Kirim HTML dari SPI Flash ke TCP
// =============================================================================
static void send_html_page(void)
{
    HtmlFlash_SendPage();
}

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================

void WebConfig_Init(void)
{
    // Tidak perlu aksi khusus — config dibaca on-demand
    (void)0;
}

void WebConfig_Handle(uint8_t *rx_buf, uint16_t rx_len)
{
    (void)rx_len;
    char *req = (char *)rx_buf;
    // =========================================================
    // GET /config/data → JSON (dipanggil JavaScript)
    // =========================================================
    if (strstr(req, "GET /config/data") != NULL)
    {
        send_config_json();
        return;
    }

    // =========================================================
    // GET /config → kirim halaman HTML
    // =========================================================
    if (strstr(req, "GET /config") != NULL)
    {
        send_html_page();
        return;
    }

    // =========================================================
    // POST /config → parse, simpan ke SPI Flash
    // =========================================================
    if (strstr(req, "POST /config") != NULL)
    {
        char *body = strstr(req, "\r\n\r\n");
        if (!body)
        {
            send_response(400, "Bad Request", "Body tidak ditemukan");
            return;
        }
        body += 4;
        MqttConfig_t cfg;
        memset(&cfg, 0, sizeof(cfg));
        parse_field(body, "host",        cfg.host,       sizeof(cfg.host));
        parse_field(body, "client_id",   cfg.client_id,  sizeof(cfg.client_id));
        parse_field(body, "user",        cfg.user,       sizeof(cfg.user));
        parse_field(body, "password",    cfg.password,   sizeof(cfg.password));
        parse_field(body, "topic",       cfg.topic,      sizeof(cfg.topic));
        parse_field(body, "full_topic",  cfg.full_topic, sizeof(cfg.full_topic));
        char tmp[8] = {0};
        parse_field(body, "port", tmp, sizeof(tmp));
        cfg.port = (uint16_t)atoi(tmp);
        if (cfg.port == 0) cfg.port = 1883;
        parse_field(body, "tele_period", tmp, sizeof(tmp));
        cfg.tele_period = (uint16_t)atoi(tmp);
        if (cfg.tele_period < 10) cfg.tele_period = 300;
        parse_field(body, "enabled", tmp, sizeof(tmp));
        cfg.enabled = (tmp[0] == '1') ? 1 : 0;
        // Full topic default jika kosong
        if (cfg.full_topic[0] == '\0')
        {
            strncpy(cfg.full_topic, MQTT_DEFAULT_FULL_TOPIC,
                    sizeof(cfg.full_topic) - 1);
        }

        cfg.magic = MQTT_CONFIG_MAGIC;
        int err = MqttConfig_Save(&cfg);
        if (err == 0)
        {
            g_config_updated = 1;
            send_response(200, "OK", "Config tersimpan!");
        }
        else
        {
            send_response(500, "Error", "Gagal simpan ke flash");
        }
        return;
    }
}
