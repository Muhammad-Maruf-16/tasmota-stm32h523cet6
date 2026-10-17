#ifndef SECURE_PACKET_H
#define SECURE_PACKET_H

#include <stdint.h>
#include <stddef.h>

/* 0 = belum dites, 1 = lulus, -1 = cmox_initialize gagal, -2 = SHA-256 salah,
 * -3 = verifikasi perintah salah */
extern volatile int8_t g_crypto_status;

/* Init CMOX + tes SHA-256("abc") dan paket uji. Dipanggil sekali saat boot. */
void SecurePacket_Init(void);

/**
 * Buat payload "COMMAND,RAW_MINUTE,HASH_HEX\n" dengan
 * hash = SHA256("SECRET_KEY:COMMAND:(RAW_MINUTE-100)"). Cocok dengan server/server.js.
 * @retval 0 sukses, -1 gagal
 */
int Generate_Secure_Packet(const char *command, uint32_t raw_minute,
                           char *packet_buffer, size_t max_len);

/* Hasil Verify_Secure_Command */
#define SECCMD_OK            0
#define SECCMD_ERR_FORMAT   -1
#define SECCMD_ERR_AUTH     -2
#define SECCMD_ERR_REPLAY   -3
#define SECCMD_ERR_INTERNAL -4

/**
 * Verifikasi perintah "COMMAND,SEQ,HASH_HEX" dari server, hash = SHA256("SECRET_KEY:SECURE:COMMAND:SEQ").
 * Urutan cek: format -> hash -> replay (SEQ harus > SEQ terakhir yang diterima). SEQ terakhir
 * hanya naik kalau semuanya lolos. Cocok dengan server/secure_cmd.js.
 * @param cmd_out buffer nama command (diisi hanya saat SECCMD_OK)
 * @retval SECCMD_*
 */
int Verify_Secure_Command(const char *payload, char *cmd_out, size_t cmd_len);

#endif /* SECURE_PACKET_H */
