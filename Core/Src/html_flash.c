#include "html_flash.h"
#include "main.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// FORWARD DECLARATION — fungsi SPI Flash & TCP dari main.c
// =============================================================================
extern void     ExtFlash_EraseSector (uint32_t addr);
extern void     ExtFlash_WritePage   (uint32_t addr, const uint8_t *data, uint16_t len);
extern void     Flash_ReadBytes      (uint32_t addr, uint8_t *buf, uint32_t len);
extern void     W5500_SendTCP        (const uint8_t *data, uint16_t len);

// ram_buffer[256] dari main.c — dipakai bersama, tidak alokasi RAM baru
// Pastikan ram_buffer di main.c tidak dideklarasi static
extern uint8_t  ram_buffer[256];

// =============================================================================
// PRIVATE: Hitung CRC32 dari SPI Flash (baca 16 word per iterasi)
// Menggunakan hardware CRC STM32 — algoritmanya sama dengan browser JS
// =============================================================================
static uint32_t html_calc_crc_from_flash(uint32_t addr, uint32_t size)
{
    CRC->CR = 1; // Reset hardware CRC
    uint32_t words_left = (size + 3) / 4;
    uint32_t cur_addr   = addr;
    while (words_left > 0)
    {
        uint32_t batch = (words_left > 16) ? 16 : words_left;
        uint8_t  tmp[64];
        uint32_t read_len = batch * 4;
        // Pre-fill 0xFF — agar byte padding (size tidak kelipatan 4) konsisten
        // dengan apa yang dihitung browser (pad dengan 0xFF)
        for (uint32_t z = 0; z < read_len; z++) tmp[z] = 0xFF;
        // Hitung berapa byte yang benar-benar ada dari file
        uint32_t real_bytes = (cur_addr - addr < size) ? (size - (cur_addr - addr)) : 0;
        if (real_bytes > read_len) real_bytes = read_len;
        if (real_bytes > 0) Flash_ReadBytes(cur_addr, tmp, real_bytes);
        uint32_t *p = (uint32_t *)tmp;
        for (uint32_t w = 0; w < batch; w++)
        {
            CRC->DR = p[w];
        }

        cur_addr   += read_len;
        words_left -= batch;
    }

    return CRC->DR;
}

// =============================================================================
// HtmlFlash_ReadMeta
// =============================================================================
void HtmlFlash_ReadMeta(HtmlMeta_t *meta)
{
    Flash_ReadBytes(HTML_FLASH_META_ADDR, (uint8_t *)meta, sizeof(HtmlMeta_t));
}

// =============================================================================
// HtmlFlash_WriteMeta
// =============================================================================
void HtmlFlash_WriteMeta(const HtmlMeta_t *meta)
{
    ExtFlash_EraseSector(HTML_FLASH_META_ADDR);
    ExtFlash_WritePage(HTML_FLASH_META_ADDR, (const uint8_t *)meta, sizeof(HtmlMeta_t));
}

// =============================================================================
// HtmlFlash_IsValid
// =============================================================================
uint8_t HtmlFlash_IsValid(void)
{
    HtmlMeta_t meta;
    HtmlFlash_ReadMeta(&meta);
    // Cek magic dulu
    if (meta.magic != HTML_FLASH_MAGIC) return 0;
    if (meta.size == 0 || meta.size > HTML_FLASH_DATA_MAX) return 0;
    // Verifikasi CRC dari data yang tersimpan
    uint32_t crc_actual = html_calc_crc_from_flash(HTML_FLASH_DATA_ADDR, meta.size);
    return (crc_actual == meta.crc) ? 1 : 0;
}

// =============================================================================
// HtmlFlash_SendPage
// Stream HTML dari SPI Flash langsung ke TCP, 256 byte per chunk.
// Pakai ram_buffer yang sudah ada — tidak alokasi RAM tambahan.
// =============================================================================
void HtmlFlash_SendPage(void)
{
    HtmlMeta_t meta;
    HtmlFlash_ReadMeta(&meta);
    if (meta.size == 0 || meta.size > HTML_FLASH_DATA_MAX) return;

    // Kirim HTTP header dulu
    char hdr[128];
    uint16_t hdr_len = (uint16_t)snprintf(hdr, sizeof(hdr),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n\r\n",
        (unsigned long)meta.size);
    W5500_SendTCP((const uint8_t *)hdr, hdr_len);

    // Stream langsung dari Memory-Mapped Flash (0x90000000 + HTML_FLASH_DATA_ADDR)
    // Chunk 2048 byte agar TCP packet memenuhi MSS Ethernet (1460 bytes) tanpa delay ACK
    uint32_t remaining = meta.size;
    const uint8_t *pFlash = (const uint8_t *)(0x90000000UL + HTML_FLASH_DATA_ADDR);
    while (remaining > 0)
    {
        uint16_t chunk = (remaining > 1460) ? 1460 : (uint16_t)remaining;
        W5500_SendTCP(pFlash, chunk);
        pFlash    += chunk;
        remaining -= chunk;
    }
}

// =============================================================================
// HtmlFlash_VerifyAndSaveMeta
// Dipanggil setelah main.c selesai streaming write ke SPI Flash sendiri.
// Hanya verifikasi CRC lalu tulis meta — tidak erase/write data.
// =============================================================================
uint8_t HtmlFlash_VerifyAndSaveMeta(uint32_t size, uint32_t crc)
{
    if (size == 0 || size > HTML_FLASH_DATA_MAX) return 0;
    uint32_t crc_actual = html_calc_crc_from_flash(HTML_FLASH_DATA_ADDR, size);
    if (crc_actual != crc) return 0;
    HtmlMeta_t meta;
    meta.magic = HTML_FLASH_MAGIC;
    meta.size  = size;
    meta.crc   = crc;
    HtmlFlash_WriteMeta(&meta);
    return 1;
}

// =============================================================================
// HtmlFlash_SavePage
// Erase SPI Flash, tulis data HTML, verifikasi CRC, update meta.
// Data sudah ada di buffer pemanggil — tidak perlu baca ulang dari W5500.
// =============================================================================
uint8_t HtmlFlash_SavePage(const uint8_t *data, uint32_t len, uint32_t crc)
{
    if (len == 0 || len > HTML_FLASH_DATA_MAX) return 0;
    // Erase semua sektor data HTML
    for (uint32_t s = 0; s < HTML_FLASH_DATA_SECTORS; s++)
    {
        ExtFlash_EraseSector(HTML_FLASH_DATA_ADDR + (s * 4096));
    }

    // Tulis data ke SPI Flash, 256 byte per page
    uint32_t       bytes_left = len;
    uint32_t       cur_addr   = HTML_FLASH_DATA_ADDR;
    const uint8_t *ptr        = data;
    while (bytes_left > 0)
    {
        // Sesuaikan dengan page boundary W25Q (256 byte)
        uint16_t page_offset = (uint16_t)(cur_addr & 0xFF);
        uint16_t space       = 256 - page_offset;
        uint16_t chunk       = (bytes_left > space) ? space : (uint16_t)bytes_left;
        ExtFlash_WritePage(cur_addr, ptr, chunk);
        ptr        += chunk;
        cur_addr   += chunk;
        bytes_left -= chunk;
    }

    // Verifikasi CRC hardware setelah tulis
    uint32_t crc_actual = html_calc_crc_from_flash(HTML_FLASH_DATA_ADDR, len);
    if (crc_actual != crc) return 0;
    // Simpan meta
    HtmlMeta_t meta;
    meta.magic = HTML_FLASH_MAGIC;
    meta.size  = len;
    meta.crc   = crc;
    HtmlFlash_WriteMeta(&meta);
    return 1;
}
