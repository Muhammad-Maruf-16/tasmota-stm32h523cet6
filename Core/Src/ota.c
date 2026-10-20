/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : ota.c
  * @brief          : Modul Web Server, Config, Dashboard & OTA Firmware Updater
  *                   - Socket 0 (Port 80)
  *                   - Handler: /config, /cmnd, /favicon.ico, /, /html-upload, /ota
  ******************************************************************************
  */
/* USER CODE END Header */

#include "ota.h"
#include "main.h"
#include "octospi_ll.h"
#include "web_config.h"
#include "web_cmnd.h"
#include "html_flash.h"
#include "html_landing.h"
#include "bl0906.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern volatile uint32_t g_ms_tick;

/* ==============================================================================
 * W5500 ETHERNET DEFINES (SOCKET 0)
 * ============================================================================== */
#define W5500_COMMON_REG_OP   0x00
#define W5500_S0_REG_OP       0x08
#define W5500_S0_TX_OP        0x10
#define W5500_S0_RX_OP        0x18

#define Sn_MR                 0x0000
#define Sn_CR                 0x0001
#define Sn_IR                 0x0002
#define Sn_SR                 0x0003
#define Sn_PORT               0x0004
#define Sn_TX_FSR             0x0020
#define Sn_TX_RD              0x0022
#define Sn_TX_WR              0x0024
#define Sn_RX_RSR             0x0026
#define Sn_RX_RD              0x0028

#define SOCK_CLOSED           0x00
#define SOCK_INIT             0x13
#define SOCK_LISTEN           0x14
#define SOCK_ESTABLISHED      0x17
#define SOCK_CLOSE_WAIT       0x1C
#define SOCK_FIN_WAIT         0x18

#define CMD_OPEN              0x01
#define CMD_LISTEN            0x02
#define CMD_DISCON            0x08
#define CMD_CLOSE             0x10
#define CMD_SEND              0x20
#define CMD_RECV              0x40

#define W5500_S0_BUF_MASK     0x1FFF
#define CHUNK_BUFFER_SIZE     (256)

/* ==============================================================================
 * FORWARD DECLARATIONS (Fungsi Driver W5500 & Buffer dari main.c)
 * ============================================================================== */
extern void     W5500_WriteReg(uint16_t addr, uint8_t block, uint8_t data);
extern uint8_t  W5500_ReadReg(uint16_t addr, uint8_t block);
extern void     W5500_WriteBuf(uint16_t addr, uint8_t block, const uint8_t *buf, uint16_t len);
extern void     W5500_ReadBuf(uint16_t addr, uint8_t block, uint8_t *buf, uint16_t len);
extern void     W5500_SocketOpenListen(uint16_t port);
extern uint16_t W5500_GetTxFreeSize(void);
extern void     W5500_SendTCP(const uint8_t *data, uint16_t len);
extern uint8_t  ram_buffer[CHUNK_BUFFER_SIZE];

/* ==============================================================================
 * FUNGSI INISIALISASI & WRAPPER
 * ============================================================================== */
void OTA_Init(void)
{
    W5500_SocketOpenListen(80);
}

void OTA_Process(void)
{
    Process_Web_OTA();
}

static void fmt_flt(char *buf, size_t sz, float val, int dec)
{
    long factor = 1;
    for (int i = 0; i < dec; i++) factor *= 10;
    int sign = (val < 0.0f) ? -1 : 1;
    float abs_val = (val < 0.0f) ? -val : val;
    long int_part = (long)abs_val;
    long dec_part = (long)((abs_val - (float)int_part) * (float)factor + 0.5f);
    if (dec_part >= factor) { int_part += 1; dec_part = 0; }
    if (sign < 0) snprintf(buf, sz, "-%ld.%0*ld", int_part, dec, dec_part);
    else snprintf(buf, sz, "%ld.%0*ld", int_part, dec, dec_part);
}

/* ==============================================================================
 * WEB DASHBOARD, CONFIG & OTA HANDLER (Socket 0)
 * ============================================================================== */
void Process_Web_OTA(void)
{
    uint8_t sr = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
    if (sr == SOCK_CLOSED)
    {
        W5500_SocketOpenListen(80);
        return;
    }

    if (sr == SOCK_CLOSE_WAIT || sr == SOCK_FIN_WAIT ||
        sr == 0x1A /* CLOSING */ || sr == 0x1B /* TIME_WAIT */ || sr == 0x1D /* LAST_ACK */)
    {
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_CLOSE);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        W5500_SocketOpenListen(80);
        return;
    }

    if (sr != SOCK_ESTABLISHED) return;
    uint16_t rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR, W5500_S0_REG_OP) << 8) |
                      W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
    if (rx_len == 0) return;
    uint16_t rx_rd = ((uint16_t)W5500_ReadReg(Sn_RX_RD, W5500_S0_REG_OP) << 8) |
                     W5500_ReadReg(Sn_RX_RD + 1, W5500_S0_REG_OP);
    uint16_t fetch = (rx_len > (CHUNK_BUFFER_SIZE - 1)) ? (CHUNK_BUFFER_SIZE - 1) : rx_len;
    uint16_t s_off = rx_rd & W5500_S0_BUF_MASK;
    if ((s_off + fetch) > (W5500_S0_BUF_MASK + 1))
    {
        uint16_t part1 = (W5500_S0_BUF_MASK + 1) - s_off;
        W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, part1);
        W5500_ReadBuf(0x0000, W5500_S0_RX_OP, ram_buffer + part1, fetch - part1);
    }
    else
    {
        W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, fetch);
    }
    ram_buffer[fetch] = '\0';

    /* 0. /sensor — JSON Real-time Data BL0906 */
    if (strstr((char *)ram_buffer, "GET /sensor") != NULL)
    {
        char v_str[16], f_str[16], tp_str[16], te_str[16], t_str[16];
        fmt_flt(v_str,  sizeof(v_str),  g_bl0906_data.voltage,     1);
        fmt_flt(f_str,  sizeof(f_str),  g_bl0906_data.frequency,   1);
        fmt_flt(tp_str, sizeof(tp_str), g_bl0906_data.total_power, 1);
        fmt_flt(te_str, sizeof(te_str), g_bl0906_data.total_energy, 4);
        fmt_flt(t_str,  sizeof(t_str),  g_bl0906_data.temperature, 1);

        char c_str[6][12], p_str[6][12];
        for (int i = 0; i < 6; i++) {
            fmt_flt(c_str[i], sizeof(c_str[i]), g_bl0906_data.current[i], 3);
            fmt_flt(p_str[i], sizeof(p_str[i]), g_bl0906_data.power[i],   1);
        }

        uint32_t upt_sec = g_ms_tick / 1000;
        static char s_json[512];
        snprintf(s_json, sizeof(s_json),
            "{\"u\":%s,\"f\":%s,\"p\":%s,\"e\":%s,\"t\":%s,"
            "\"c\":[{\"i\":%s,\"w\":%s},{\"i\":%s,\"w\":%s},{\"i\":%s,\"w\":%s},"
            "{\"i\":%s,\"w\":%s},{\"i\":%s,\"w\":%s},{\"i\":%s,\"w\":%s}],"
            "\"upt\":%lu,\"on\":%d}",
            v_str, f_str, tp_str, te_str, t_str,
            c_str[0], p_str[0], c_str[1], p_str[1], c_str[2], p_str[2],
            c_str[3], p_str[3], c_str[4], p_str[4], c_str[5], p_str[5],
            (unsigned long)upt_sec, g_bl0906_data.is_online ? 1 : 0);

        char s_hdr[160];
        snprintf(s_hdr, sizeof(s_hdr),
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: application/json\r\n"
            "Content-Length: %d\r\n"
            "Access-Control-Allow-Origin: *\r\n"
            "Connection: close\r\n\r\n",
            (int)strlen(s_json));

        W5500_SendTCP((uint8_t *)s_hdr, (uint16_t)strlen(s_hdr));
        W5500_SendTCP((uint8_t *)s_json, (uint16_t)strlen(s_json));

        rx_rd += rx_len;
        W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        uint32_t t_flush = g_ms_tick;
        while (W5500_GetTxFreeSize() < 8192 && (g_ms_tick - t_flush) < 200)
        {
            uint8_t sr_cur = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
            if (sr_cur != SOCK_ESTABLISHED && sr_cur != SOCK_CLOSE_WAIT) break;
        }
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 1. /config — Form Konfigurasi MQTT */
    if (strstr((char *)ram_buffer, "/config") != NULL)
    {
        if (strstr((char *)ram_buffer, "POST") != NULL)
        {
            uint8_t tail[4] = { 0 };
            char *pBody = NULL;
            while (pBody == NULL)
            {
                static uint8_t overlap[4 + CHUNK_BUFFER_SIZE];
                memcpy(overlap, tail, 4);
                memcpy(overlap + 4, ram_buffer, fetch);
                overlap[4 + fetch] = '\0';
                char *found = strstr((char *)overlap, "\r\n\r\n");
                if (found)
                {
                    int pos_in_overlap = (found - (char *)overlap);
                    int pos_in_buf = pos_in_overlap - 4;
                    int body_start = pos_in_buf + 4;
                    if (body_start < 0) body_start = 0;
                    if (body_start > fetch) body_start = fetch;
                    pBody = (char *)ram_buffer + body_start;
                    rx_rd += fetch;
                    W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                    W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                    W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                    while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
                }
                else
                {
                    memcpy(tail, ram_buffer + fetch - 4, 4);
                    rx_rd += fetch;
                    W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                    W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                    W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                    while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
                    rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR, W5500_S0_REG_OP) << 8) |
                             W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
                    if (rx_len == 0) continue;
                    fetch = (rx_len > (CHUNK_BUFFER_SIZE - 1)) ? (CHUNK_BUFFER_SIZE - 1) : rx_len;
                    s_off = rx_rd & W5500_S0_BUF_MASK;
                    W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, fetch);
                    ram_buffer[fetch] = '\0';
                }
            }

            uint16_t header_len = (uint8_t *)pBody - ram_buffer;
            uint16_t body_in_chunk = fetch - header_len;
            if (body_in_chunk > 0)
            {
                memmove(ram_buffer, pBody, body_in_chunk);
            }

            rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR, W5500_S0_REG_OP) << 8) |
                     W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
            if (rx_len > 0)
            {
                uint16_t grab = rx_len;
                if (body_in_chunk + grab > CHUNK_BUFFER_SIZE - 1)
                {
                    grab = (CHUNK_BUFFER_SIZE - 1) - body_in_chunk;
                }
                s_off = rx_rd & W5500_S0_BUF_MASK;
                W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer + body_in_chunk, grab);
                body_in_chunk += grab;
                if (body_in_chunk >= CHUNK_BUFFER_SIZE) body_in_chunk = CHUNK_BUFFER_SIZE - 1;
                ram_buffer[body_in_chunk] = '\0';
                rx_rd += rx_len;
                W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
            }

            const char prefix_tag[] = "POST /config\r\n\r\n";
            uint8_t tag_len = strlen(prefix_tag);
            if (body_in_chunk + tag_len < CHUNK_BUFFER_SIZE)
            {
                memmove(ram_buffer + tag_len, ram_buffer, body_in_chunk + 1);
                memcpy(ram_buffer, prefix_tag, tag_len);
                body_in_chunk += tag_len;
            }

            WebConfig_Handle(ram_buffer, body_in_chunk);
            uint32_t t_flush = g_ms_tick;
            while (W5500_GetTxFreeSize() < 8192 && (g_ms_tick - t_flush) < 200)
            {
                uint8_t sr_cur = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
                if (sr_cur != SOCK_ESTABLISHED && sr_cur != SOCK_CLOSE_WAIT) break;
            }
            W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
            while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
            return;
        }

        WebConfig_Handle(ram_buffer, fetch);
        rx_rd += rx_len;
        W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        uint32_t t_flush = g_ms_tick;
        while (W5500_GetTxFreeSize() < 8192 && (g_ms_tick - t_flush) < 200)
        {
            uint8_t sr_cur = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
            if (sr_cur != SOCK_ESTABLISHED && sr_cur != SOCK_CLOSE_WAIT) break;
        }
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 2. /cmnd — CAN Command GUI */
    if (strstr((char *)ram_buffer, "/cmnd") != NULL)
    {
        if (strstr((char *)ram_buffer, "POST") != NULL)
        {
            uint8_t tail[4] = { 0 };
            char *pBody = NULL;
            while (pBody == NULL)
            {
                static uint8_t overlap[4 + CHUNK_BUFFER_SIZE];
                memcpy(overlap, tail, 4);
                memcpy(overlap + 4, ram_buffer, fetch);
                overlap[4 + fetch] = '\0';
                char *found = strstr((char *)overlap, "\r\n\r\n");
                if (found)
                {
                    int pos_in_overlap = (found - (char *)overlap);
                    int pos_in_buf = pos_in_overlap - 4;
                    int body_start = pos_in_buf + 4;
                    if (body_start < 0) body_start = 0;
                    if (body_start > fetch) body_start = fetch;
                    pBody = (char *)ram_buffer + body_start;
                    rx_rd += fetch;
                    W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                    W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                    W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                    while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
                }
                else
                {
                    memcpy(tail, ram_buffer + fetch - 4, 4);
                    rx_rd += fetch;
                    W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                    W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                    W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                    while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
                    rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR, W5500_S0_REG_OP) << 8) |
                             W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
                    if (rx_len == 0) continue;
                    fetch = (rx_len > (CHUNK_BUFFER_SIZE - 1)) ? (CHUNK_BUFFER_SIZE - 1) : rx_len;
                    s_off = rx_rd & W5500_S0_BUF_MASK;
                    W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, fetch);
                    ram_buffer[fetch] = '\0';
                }
            }

            uint16_t header_len = (uint8_t *)pBody - ram_buffer;
            uint16_t body_in_chunk = fetch - header_len;
            if (body_in_chunk > 0)
            {
                memmove(ram_buffer, pBody, body_in_chunk);
            }

            rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR, W5500_S0_REG_OP) << 8) |
                     W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
            if (rx_len > 0)
            {
                uint16_t grab = rx_len;
                if (body_in_chunk + grab > CHUNK_BUFFER_SIZE - 1)
                {
                    grab = (CHUNK_BUFFER_SIZE - 1) - body_in_chunk;
                }
                s_off = rx_rd & W5500_S0_BUF_MASK;
                W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer + body_in_chunk, grab);
                body_in_chunk += grab;
                if (body_in_chunk >= CHUNK_BUFFER_SIZE) body_in_chunk = CHUNK_BUFFER_SIZE - 1;
                ram_buffer[body_in_chunk] = '\0';
                rx_rd += rx_len;
                W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
                while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
            }

            const char prefix_tag[] = "POST /cmnd\r\n\r\n";
            uint8_t tag_len = strlen(prefix_tag);
            if (body_in_chunk + tag_len < CHUNK_BUFFER_SIZE)
            {
                memmove(ram_buffer + tag_len, ram_buffer, body_in_chunk + 1);
                memcpy(ram_buffer, prefix_tag, tag_len);
                body_in_chunk += tag_len;
            }

            WebCmnd_Handle(ram_buffer, body_in_chunk);
            for (volatile int d = 0; d < 20000; d++);
            W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
            while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
            return;
        }

        WebCmnd_Handle(ram_buffer, fetch);
        rx_rd += rx_len;
        W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        for (volatile int d = 0; d < 20000; d++);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 2.5 Favicon handler — Return 204 No Content seketika (hindari kirim ulang 18KB HTML) */
    if (strstr((char *)ram_buffer, "GET /favicon.ico") != NULL)
    {
        const char fav_resp[] = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
        W5500_SendTCP((const uint8_t *)fav_resp, strlen(fav_resp));
        rx_rd += rx_len;
        W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR,        W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        W5500_WriteReg(Sn_CR,        W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 3. GET / — Kirim dashboard dari Flash eksternal atau Landing Page */
    if (strstr((char *)ram_buffer, "GET /") != NULL)
    {
        if (HtmlFlash_IsValid())
        {
            HtmlFlash_SendPage();
        }
        else
        {
            uint16_t llen = (uint16_t)strlen(HTML_LANDING);
            char header[128];
            snprintf(header, sizeof(header),
                    "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length: %d\r\nConnection: close\r\n\r\n",
                    llen);
            W5500_SendTCP((uint8_t *)header, strlen(header));
            W5500_SendTCP((const uint8_t *)HTML_LANDING, llen);
        }

        rx_rd += rx_len;
        W5500_WriteReg(Sn_RX_RD, W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        // Tunggu TX buffer tuntas terkirim ke PC sebelum close
        uint32_t drain_to = g_ms_tick;
        while (W5500_GetTxFreeSize() < 8192)
        {
            uint8_t cur_sr = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
            if (cur_sr != SOCK_ESTABLISHED && cur_sr != SOCK_CLOSE_WAIT) break;
            if (g_ms_tick - drain_to > 1000) break;
        }

        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 4. POST /html-upload — Simpan file HTML baru ke flash eksternal */
    if (strstr((char *)ram_buffer, "POST /html-upload") != NULL)
    {
        char *pSize = strstr((char *)ram_buffer, "?sz=");
        char *pCrc  = strstr((char *)ram_buffer, "&crc=");
        if (!pSize || !pCrc) return;
        uint32_t expected_size = strtoul(pSize + 4, NULL, 10);
        uint32_t expected_crc  = strtoul(pCrc  + 5, NULL, 0);

        if (expected_size == 0 || expected_size > HTML_FLASH_DATA_MAX)
        {
            const char err[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nSize invalid";
            W5500_SendTCP((uint8_t *)err, strlen(err));
            W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
            while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
            return;
        }

        // Cari \r\n\r\n dengan overlap 4 byte (identik sha256)
        uint8_t  html_rx_already_updated = 0;
        uint8_t  html_tail[4] = { 0 };
        char    *pBody = NULL;
        while (pBody == NULL)
        {
            uint8_t overlap[4 + CHUNK_BUFFER_SIZE];
            memcpy(overlap, html_tail, 4);
            memcpy(overlap + 4, ram_buffer, fetch);
            overlap[4 + fetch] = '\0';
            char *found = strstr((char *)overlap, "\r\n\r\n");
            if (found)
            {
                int pos_in_overlap = (found - (char *)overlap);
                int pos_in_buf     = pos_in_overlap - 4;
                int body_start     = pos_in_buf + 4;
                if (body_start < 0)      body_start = 0;
                if (body_start > fetch)  body_start = fetch;
                pBody = (char *)ram_buffer + body_start;
                rx_rd += fetch;
                W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
                while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
                html_rx_already_updated = 1;
            }
            else
            {
                memcpy(html_tail, ram_buffer + fetch - 4, 4);
                rx_rd += fetch;
                W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
                W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
                W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
                while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
                rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR,     W5500_S0_REG_OP) << 8) |
                                     W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
                if (rx_len == 0) continue;
                fetch = (rx_len > (CHUNK_BUFFER_SIZE - 1)) ? (CHUNK_BUFFER_SIZE - 1) : rx_len;
                s_off = rx_rd & W5500_S0_BUF_MASK;
                W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, fetch);
                ram_buffer[fetch] = '\0';
            }
        }

        // Erase sektor target HTML
        ExtFlash_EraseSector(HTML_FLASH_META_ADDR);
        uint32_t html_sectors = (expected_size + 4095) / 4096;
        for (uint32_t s = 0; s < html_sectors; s++)
        {
            ExtFlash_EraseSector(HTML_FLASH_DATA_ADDR + (s * 4096));
        }

        // Tulis chunk awal body
        uint16_t header_len      = (uint8_t *)pBody - ram_buffer;
        uint16_t body_in_packet  = fetch - header_len;
        uint32_t bytes_received  = 0;
        if (body_in_packet > 0)
        {
            uint16_t written = 0;
            while (written < body_in_packet)
            {
                uint32_t cur_flash   = HTML_FLASH_DATA_ADDR + written;
                uint16_t page_offset = cur_flash & 0xFF;
                uint16_t space_left  = 256 - page_offset;
                uint16_t page_len    = (body_in_packet - written > space_left)
                                        ? space_left : (body_in_packet - written);
                ExtFlash_WritePage(cur_flash, (uint8_t *)pBody + written, page_len);
                written += page_len;
            }
            bytes_received += body_in_packet;
        }

        if (!html_rx_already_updated)
        {
            rx_rd += fetch;
            W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
            W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
            W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
            while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
        }

        // Stream sisa payload — selalu baca pointer aktual dari register Sn_RX_RD W5500
        while (bytes_received < expected_size)
        {
            rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR,     W5500_S0_REG_OP) << 8) |
                                 W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
            if (rx_len == 0) continue;
            rx_rd = ((uint16_t)W5500_ReadReg(Sn_RX_RD,     W5500_S0_REG_OP) << 8) |
                                W5500_ReadReg(Sn_RX_RD + 1, W5500_S0_REG_OP);
            uint16_t grab = (rx_len > CHUNK_BUFFER_SIZE) ? CHUNK_BUFFER_SIZE : rx_len;
            if (bytes_received + grab > expected_size) grab = expected_size - bytes_received;
            s_off = rx_rd & W5500_S0_BUF_MASK;
            if ((s_off + grab) > (W5500_S0_BUF_MASK + 1))
            {
                uint16_t p1 = (W5500_S0_BUF_MASK + 1) - s_off;
                W5500_ReadBuf(s_off,   W5500_S0_RX_OP, ram_buffer,      p1);
                W5500_ReadBuf(0x0000,  W5500_S0_RX_OP, ram_buffer + p1, grab - p1);
            }
            else
            {
                W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, grab);
            }

            uint16_t written = 0;
            while (written < grab)
            {
                uint32_t cur_flash   = HTML_FLASH_DATA_ADDR + bytes_received + written;
                uint16_t page_offset = cur_flash & 0xFF;
                uint16_t space_left  = 256 - page_offset;
                uint16_t page_len    = (grab - written > space_left)
                                        ? space_left : (grab - written);
                ExtFlash_WritePage(cur_flash, &ram_buffer[written], page_len);
                written += page_len;
            }

            bytes_received += grab;
            rx_rd += grab;
            W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
            W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
            W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
            while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
        }

        // Verifikasi CRC & simpan meta
        if (HtmlFlash_VerifyAndSaveMeta(expected_size, expected_crc))
        {
            const char ok[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 15\r\nConnection: close\r\n\r\nHTML tersimpan!";
            W5500_SendTCP((uint8_t *)ok, strlen(ok));
        }
        else
        {
            const char fail[] = "HTTP/1.1 500 Error\r\nContent-Type: text/plain\r\nContent-Length: 14\r\nConnection: close\r\n\r\nCRC Mismatch!";
            W5500_SendTCP((uint8_t *)fail, strlen(fail));
        }

        uint32_t drain_to = g_ms_tick;
        while (W5500_GetTxFreeSize() < 8192)
        {
            uint8_t cur_sr = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
            if (cur_sr != SOCK_ESTABLISHED && cur_sr != SOCK_CLOSE_WAIT) break;
            if (g_ms_tick - drain_to > 1000) break;
        }

        for (volatile int d = 0; d < 200000; d++);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    /* 5. POST /ota — Upload Firmware OTA (identik sha256) */
    if (strstr((char *)ram_buffer, "POST /ota") == NULL) return;

    char *pSize = strstr((char *)ram_buffer, "?sz=");
    char *pCrc  = strstr((char *)ram_buffer, "&crc=");
    if (!pSize || !pCrc) return;
    uint32_t expected_size = strtoul(pSize + 4, NULL, 10);
    uint32_t expected_crc  = strtoul(pCrc + 5, NULL, 0);

    if (expected_size == 0 || expected_size > MAX_FIRMWARE_SIZE)
    {
        const char err[] = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nSize invalid";
        W5500_SendTCP((uint8_t *)err, strlen(err));
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
        return;
    }

    uint8_t rx_already_updated = 0;
    uint8_t tail[4] = { 0 };
    char *pBody = NULL;
    while (pBody == NULL)
    {
        uint8_t overlap[4 + CHUNK_BUFFER_SIZE];
        memcpy(overlap, tail, 4);
        memcpy(overlap + 4, ram_buffer, fetch);
        overlap[4 + fetch] = '\0';
        char *found = strstr((char *)overlap, "\r\n\r\n");
        if (found)
        {
            int pos_in_overlap = (found - (char *)overlap);
            int pos_in_buf     = pos_in_overlap - 4;
            int body_start     = pos_in_buf + 4;
            if (body_start < 0)     body_start = 0;
            if (body_start > fetch) body_start = fetch;
            pBody = (char *)ram_buffer + body_start;
            rx_rd += fetch;
            W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
            W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
            W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
            while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
            rx_already_updated = 1;
        }
        else
        {
            memcpy(tail, ram_buffer + fetch - 4, 4);
            rx_rd += fetch;
            W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
            W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
            W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
            while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
            rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR,     W5500_S0_REG_OP) << 8) |
                                 W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
            if (rx_len == 0) continue;
            fetch = (rx_len > (CHUNK_BUFFER_SIZE - 1)) ? (CHUNK_BUFFER_SIZE - 1) : rx_len;
            s_off = rx_rd & W5500_S0_BUF_MASK;
            W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, fetch);
            ram_buffer[fetch] = '\0';
        }
    }

    uint32_t target_meta = FLASH_STAGING_META;
    uint32_t target_bin  = FLASH_STAGING_BIN;

    ExtFlash_EraseSector(target_meta);
    uint32_t sectors = (expected_size + 4095) / 4096;
    for (uint32_t s = 0; s < sectors; s++)
    {
        ExtFlash_EraseSector(target_bin + (s * 4096));
    }

    uint16_t header_len      = (uint8_t *)pBody - ram_buffer;
    uint16_t body_in_packet  = fetch - header_len;
    uint32_t bytes_received  = 0;
    if (body_in_packet > 0)
    {
        uint16_t written = 0;
        while (written < body_in_packet)
        {
            uint32_t cur_flash   = target_bin + written;
            uint16_t page_offset = cur_flash & 0xFF;
            uint16_t space_left  = 256 - page_offset;
            uint16_t page_len    = (body_in_packet - written > space_left)
                                    ? space_left : (body_in_packet - written);
            ExtFlash_WritePage(cur_flash, (uint8_t *)pBody + written, page_len);
            written += page_len;
        }
        bytes_received += body_in_packet;
    }

    if (!rx_already_updated)
    {
        rx_rd += fetch;
        W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
    }

    while (bytes_received < expected_size)
    {
        rx_len = ((uint16_t)W5500_ReadReg(Sn_RX_RSR,     W5500_S0_REG_OP) << 8) |
                             W5500_ReadReg(Sn_RX_RSR + 1, W5500_S0_REG_OP);
        if (rx_len == 0) continue;
        rx_rd = ((uint16_t)W5500_ReadReg(Sn_RX_RD,     W5500_S0_REG_OP) << 8) |
                            W5500_ReadReg(Sn_RX_RD + 1, W5500_S0_REG_OP);
        uint16_t grab = (rx_len > CHUNK_BUFFER_SIZE) ? CHUNK_BUFFER_SIZE : rx_len;
        if (bytes_received + grab > expected_size) grab = expected_size - bytes_received;
        s_off = rx_rd & W5500_S0_BUF_MASK;
        if ((s_off + grab) > (W5500_S0_BUF_MASK + 1))
        {
            uint16_t p1 = (W5500_S0_BUF_MASK + 1) - s_off;
            W5500_ReadBuf(s_off,   W5500_S0_RX_OP, ram_buffer,      p1);
            W5500_ReadBuf(0x0000,  W5500_S0_RX_OP, ram_buffer + p1, grab - p1);
        }
        else
        {
            W5500_ReadBuf(s_off, W5500_S0_RX_OP, ram_buffer, grab);
        }

        uint16_t written = 0;
        while (written < grab)
        {
            uint32_t cur_flash   = target_bin + bytes_received + written;
            uint16_t page_offset = cur_flash & 0xFF;
            uint16_t space_left  = 256 - page_offset;
            uint16_t page_len    = (grab - written > space_left)
                                    ? space_left : (grab - written);
            ExtFlash_WritePage(cur_flash, &ram_buffer[written], page_len);
            written += page_len;
        }

        bytes_received += grab;
        rx_rd += grab;
        W5500_WriteReg(Sn_RX_RD,     W5500_S0_REG_OP, (uint8_t)(rx_rd >> 8));
        W5500_WriteReg(Sn_RX_RD + 1, W5500_S0_REG_OP, (uint8_t)rx_rd);
        W5500_WriteReg(Sn_CR,         W5500_S0_REG_OP, CMD_RECV);
        while (W5500_ReadReg(Sn_CR,   W5500_S0_REG_OP));
    }

    CRC->CR = 1;
    uint32_t words = (expected_size + 3) / 4;
    uint32_t r_addr = target_bin;
    while (words > 0)
    {
        uint32_t c_w = (words > 16) ? 16 : words;
        uint8_t  temp[64];
        Flash_ReadBytes(r_addr, temp, c_w * 4);
        uint32_t *p = (uint32_t *)temp;
        for (uint32_t w = 0; w < c_w; w++) CRC->DR = p[w];
        r_addr += (c_w * 4);
        words -= c_w;
    }

    if (CRC->DR == expected_crc)
    {
        uint8_t meta[10] = { 0 };
        meta[0] = (uint8_t)(expected_size);
        meta[1] = (uint8_t)(expected_size >> 8);
        meta[2] = (uint8_t)(expected_size >> 16);
        meta[3] = (uint8_t)(expected_size >> 24);
        meta[4] = (uint8_t)(expected_crc);
        meta[5] = (uint8_t)(expected_crc >> 8);
        meta[6] = (uint8_t)(expected_crc >> 16);
        meta[7] = (uint8_t)(expected_crc >> 24);
        meta[8] = 0xA5; /* STAGING VALID */
        ExtFlash_WritePage(target_meta, meta, 10);

        const char ok_body[] = "Update OK! Rebooting...";
        char ok_msg[128];
        snprintf(ok_msg, sizeof(ok_msg),
                 "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: %d\r\nConnection: close\r\n\r\n%s",
                 (int)strlen(ok_body), ok_body);
        W5500_SendTCP((uint8_t *)ok_msg, strlen(ok_msg));

        for (volatile int d = 0; d < 200000; d++);
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));

        for (volatile int d = 0; d < 500000; d++);
        NVIC_SystemReset();
    }
    else
    {
        const char fail[] = "HTTP/1.1 500 Internal Error\r\nConnection: close\r\n\r\nCRC Error";
        W5500_SendTCP((uint8_t *)fail, strlen(fail));
        W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_DISCON);
        while (W5500_ReadReg(Sn_CR, W5500_S0_REG_OP));
    }
}
