#ifndef HTML_FLASH_H
#define HTML_FLASH_H

#include <stdint.h>

// =============================================================================
// ADDRESS SPI FLASH — HTML Storage
// =============================================================================
#define HTML_FLASH_META_ADDR    0x00210000U   // Meta: size + CRC (8 byte)
#define HTML_FLASH_DATA_ADDR    0x00211000U   // Data: file HTML gabungan (max 4 sektor = 16KB)
#define HTML_FLASH_DATA_SECTORS 32U           // Jumlah sektor yang di-erase saat upload (32 x 4KB = 128KB)
#define HTML_FLASH_DATA_MAX     (128U * 1024U)// Batas maksimal ukuran HTML (128KB)

// Magic number untuk validasi meta
#define HTML_FLASH_MAGIC        0x484D4C21U   // "HML!"

// =============================================================================
// STRUCT META
// =============================================================================
typedef struct {
    uint32_t magic;   // HTML_FLASH_MAGIC jika valid
    uint32_t size;    // Ukuran file HTML dalam byte
    uint32_t crc;     // CRC32 file HTML (dihitung di browser, sama algoritmanya dengan OTA)
} HtmlMeta_t;         // Total 12 byte

// =============================================================================
// PAGE ID — untuk referensi internal (opsional, untuk ekspansi)
// =============================================================================
#define HTML_PAGE_MAIN  0U   // Satu-satunya halaman (gabungan semua tab)

// =============================================================================
// FUNGSI PUBLIK
// =============================================================================

/**
 * @brief  Cek apakah HTML di SPI Flash valid (magic + CRC match).
 * @retval 1 = valid, 0 = tidak valid / kosong
 */
uint8_t HtmlFlash_IsValid(void);

/**
 * @brief  Baca meta dari SPI Flash ke struct.
 * @param  meta  Pointer ke HtmlMeta_t yang akan diisi
 */
void HtmlFlash_ReadMeta(HtmlMeta_t *meta);

/**
 * @brief  Tulis meta ke SPI Flash.
 * @param  meta  Pointer ke HtmlMeta_t yang akan ditulis
 */
void HtmlFlash_WriteMeta(const HtmlMeta_t *meta);

/**
 * @brief  Stream HTML dari SPI Flash langsung ke TCP (256 byte per chunk).
 *         Hanya dipanggil setelah HtmlFlash_IsValid() == 1.
 */
void HtmlFlash_SendPage(void);

/**
 * @brief  Erase + tulis file HTML baru ke SPI Flash, lalu update meta.
 *         Data ditulis langsung dari buffer (streaming dari W5500).
 * @param  data  Pointer ke data HTML
 * @param  len   Panjang data dalam byte
 * @param  crc   CRC32 yang dikirim browser (untuk verifikasi)
 * @retval 1 = berhasil (CRC match), 0 = gagal
 */
uint8_t HtmlFlash_SavePage(const uint8_t *data, uint32_t len, uint32_t crc);

/**
 * @brief  Verifikasi CRC data yang sudah ditulis ke SPI Flash, lalu simpan meta.
 *         Digunakan setelah main.c selesai streaming write sendiri.
 * @param  size  Ukuran file HTML yang sudah ditulis
 * @param  crc   CRC32 yang dikirim browser
 * @retval 1 = CRC match + meta tersimpan, 0 = gagal
 */
uint8_t HtmlFlash_VerifyAndSaveMeta(uint32_t size, uint32_t crc);

#endif /* HTML_FLASH_H */
