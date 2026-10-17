#include "web_cmnd.h"
#include "can_bus.h"
#include "html_flash.h"
#include "main.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

// =============================================================================
// FORWARD DECLARATION
// =============================================================================
extern void W5500_SendTCP(const uint8_t *data, uint16_t len);

// HTML_CMND_PAGE dihapus — halaman di-serve dari SPI Flash via HtmlFlash_SendPage()

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
    char search[32];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(body, search);
    if (!p) { out[0] = '\0'; return; }
    p += strlen(search);
    const char *end = strchr(p, '&');
    size_t len = end ? (size_t)(end - p) : strlen(p);
    if (len >= out_len) len = out_len - 1;
    static char tmp[64];
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
// HELPER: Kirim HTML dari SPI Flash ke TCP
// =============================================================================
static void send_html_page(void)
{
    HtmlFlash_SendPage();
}

// =============================================================================
// HELPER: Hex string → byte array
// =============================================================================
static uint8_t hex_str_to_bytes(const char *str, uint8_t *out, uint8_t max_bytes)
{
    char clean[17] = {0};
    uint8_t ci = 0;
    for (uint8_t i = 0; str[i] && ci < 16; i++)
    {
        char c = str[i];
        if (c == ' ' || c == '\t') continue;
        clean[ci++] = c;
    }
    if (ci % 2 != 0) ci--;
    uint8_t dlc = 0;
    for (uint8_t i = 0; i < ci && dlc < max_bytes; i += 2)
    {
        #define HEX_NIBBLE(c) (((c)|0x20) > '9' ? ((c)|0x20) - 'a' + 10 : (c) - '0')
        out[dlc++] = (uint8_t)((HEX_NIBBLE(clean[i]) << 4) | HEX_NIBBLE(clean[i + 1]));
        #undef HEX_NIBBLE
    }
    return dlc;
}

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================
void WebCmnd_Handle(uint8_t *rx_buf, uint16_t rx_len)
{
    (void)rx_len;
    char *req = (char *)rx_buf;
    // =========================================================
    // GET /cmnd → kirim halaman HTML
    // =========================================================
    if (strstr(req, "GET /cmnd") != NULL)
    {
        send_html_page();
        return;
    }

    // =========================================================
    // POST /cmnd → parse id + msg, kirim CAN bare-metal
    // =========================================================
    if (strstr(req, "POST /cmnd") != NULL)
    {
        char *body = strstr(req, "\r\n\r\n");
        if (!body)
        {
            send_response(400, "Bad Request", "Body tidak ditemukan");
            return;
        }
        body += 4;
        char s_id[12]  = {0};
        char s_msg[20] = {0};
        parse_field(body, "id",  s_id,  sizeof(s_id));
        parse_field(body, "msg", s_msg, sizeof(s_msg));
        if (s_id[0] == '\0' || s_msg[0] == '\0')
        {
            send_response(400, "Bad Request", "Parameter id / msg tidak ada");
            return;
        }

        uint32_t can_id = strtoul(s_id, NULL, 16);
        uint8_t data[8] = {0};
        uint8_t dlc = hex_str_to_bytes(s_msg, data, 8);
        if (dlc == 0)
        {
            send_response(400, "Bad Request", "MSG tidak valid atau kosong");
            return;
        }

        /* Bagian CAN Bus dinonaktifkan sementara (belum ada CAN HW) */
        char resp[64];
        snprintf(resp, sizeof(resp), "CAN Disabled (ID=0x%lX DLC=%u)", (unsigned long)can_id, dlc);
        send_response(200, "OK", resp);
        return;
    }
}
