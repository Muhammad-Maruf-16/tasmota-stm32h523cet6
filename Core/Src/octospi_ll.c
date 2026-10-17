/**
  ******************************************************************************
  * @file           : octospi_ll.c
  * @brief          : OctoSPI LL Driver untuk Macronix MX25LM (STM32H523CET6)
  *                   - Mendukung XIP Memory-Mapped mode
  *                   - Fungsi Erase/Write ditaruh di RAM (.data) agar aman saat XIP
  ******************************************************************************
  */

#include "octospi_ll.h"
#include <string.h>

/* Helper RAM functions for In-System XIP Flash write/erase */
__attribute__((noinline, section(".RamFunc")))
void OCTOSPI1_LL_IndirectInit_RAM(void)
{
    OCTOSPI1->CR &= ~OCTOSPI_CR_EN;
    while (OCTOSPI1->CR & OCTOSPI_CR_EN);

    OCTOSPI1->DCR1 = (24U << OCTOSPI_DCR1_DEVSIZE_Pos)
                   | (0U  << OCTOSPI_DCR1_CSHT_Pos);

    OCTOSPI1->DCR2 = (3U  << OCTOSPI_DCR2_PRESCALER_Pos);

    OCTOSPI1->TCR  = (0U  << OCTOSPI_TCR_DCYC_Pos)
                   | (0U  << OCTOSPI_TCR_SSHIFT_Pos);

    OCTOSPI1->CR = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk);
    OCTOSPI1->CR |= OCTOSPI_CR_EN;
    while (!(OCTOSPI1->CR & OCTOSPI_CR_EN));
}

__attribute__((noinline, section(".RamFunc")))
void OCTOSPI1_LL_MemoryMapped_RAM(void)
{
    OCTOSPI1->CR &= ~OCTOSPI_CR_EN;
    while (OCTOSPI1->CR & OCTOSPI_CR_EN);

    OCTOSPI1->DCR1 = (24U << OCTOSPI_DCR1_DEVSIZE_Pos)
                   | (0U  << OCTOSPI_DCR1_CSHT_Pos);

    OCTOSPI1->DCR2 = (3U  << OCTOSPI_DCR2_PRESCALER_Pos);

    OCTOSPI1->TCR  = (0U  << OCTOSPI_TCR_DCYC_Pos)
                   | (0U  << OCTOSPI_TCR_SSHIFT_Pos);

    OCTOSPI1->CCR = (1U << OCTOSPI_CCR_IMODE_Pos)
                  | (1U << OCTOSPI_CCR_ADMODE_Pos)
                  | (2U << OCTOSPI_CCR_ADSIZE_Pos)
                  | (1U << OCTOSPI_CCR_DMODE_Pos);

    OCTOSPI1->IR = 0x03U;

    OCTOSPI1->CR = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk)
                 | (3U << OCTOSPI_CR_FMODE_Pos);

    OCTOSPI1->CR |= OCTOSPI_CR_EN;
    while (!(OCTOSPI1->CR & OCTOSPI_CR_EN));
}

__attribute__((noinline, section(".RamFunc")))
static void ExtFlash_WaitBusy_RAM(void)
{
    uint8_t status;
    do {
        while (OCTOSPI1->SR & OCTOSPI_SR_BUSY);
        OCTOSPI1->DLR = 0U;
        OCTOSPI1->CCR = (1U << OCTOSPI_CCR_IMODE_Pos)
                      | (1U << OCTOSPI_CCR_DMODE_Pos);
        OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk)
                      | (1U << OCTOSPI_CR_FMODE_Pos);
        OCTOSPI1->IR  = 0x05U;

        while (!(OCTOSPI1->SR & OCTOSPI_SR_FTF) &&
               !(OCTOSPI1->SR & OCTOSPI_SR_TCF));
        status = *(__IO uint8_t *)(&OCTOSPI1->DR);
        while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF));
        OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;
        OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk);
    } while (status & 0x01U);
}

__attribute__((noinline, section(".RamFunc")))
static void ExtFlash_WriteEnable_RAM(void)
{
    while (OCTOSPI1->SR & OCTOSPI_SR_BUSY);
    OCTOSPI1->DLR = 0U;
    OCTOSPI1->CCR = (1U << OCTOSPI_CCR_IMODE_Pos);
    OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk);
    OCTOSPI1->IR  = 0x06U;

    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF));
    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;
}

__attribute__((noinline, section(".RamFunc")))
void ExtFlash_EraseSector(uint32_t addr)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    OCTOSPI1_LL_IndirectInit_RAM();
    ExtFlash_WaitBusy_RAM();
    ExtFlash_WriteEnable_RAM();

    while (OCTOSPI1->SR & OCTOSPI_SR_BUSY);
    OCTOSPI1->DLR = 0U;
    OCTOSPI1->CCR = (1U << OCTOSPI_CCR_IMODE_Pos)
                  | (1U << OCTOSPI_CCR_ADMODE_Pos)
                  | (2U << OCTOSPI_CCR_ADSIZE_Pos);
    OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk);
    OCTOSPI1->IR  = 0x20U;
    OCTOSPI1->AR  = addr;

    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF));
    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;
    ExtFlash_WaitBusy_RAM();

    OCTOSPI1_LL_MemoryMapped_RAM();

    if (!primask) {
        __enable_irq();
    }
}

__attribute__((noinline, section(".RamFunc")))
void ExtFlash_WritePage(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > 256) return;
    uint32_t primask = __get_PRIMASK();
    __disable_irq();

    OCTOSPI1_LL_IndirectInit_RAM();
    ExtFlash_WaitBusy_RAM();
    ExtFlash_WriteEnable_RAM();

    while (OCTOSPI1->SR & OCTOSPI_SR_BUSY);
    OCTOSPI1->DLR = len - 1U;
    OCTOSPI1->CCR = (1U << OCTOSPI_CCR_IMODE_Pos)
                  | (1U << OCTOSPI_CCR_ADMODE_Pos)
                  | (2U << OCTOSPI_CCR_ADSIZE_Pos)
                  | (1U << OCTOSPI_CCR_DMODE_Pos);
    OCTOSPI1->CR  = (OCTOSPI1->CR & ~OCTOSPI_CR_FMODE_Msk);
    OCTOSPI1->IR  = 0x02U;
    OCTOSPI1->AR  = addr;

    for (uint16_t i = 0; i < len; i++)
    {
        while (!(OCTOSPI1->SR & OCTOSPI_SR_FTF) &&
               !(OCTOSPI1->SR & OCTOSPI_SR_TCF));
        *(__IO uint8_t *)(&OCTOSPI1->DR) = data[i];
    }
    while (!(OCTOSPI1->SR & OCTOSPI_SR_TCF));
    OCTOSPI1->FCR = OCTOSPI_FCR_CTCF;
    ExtFlash_WaitBusy_RAM();

    OCTOSPI1_LL_MemoryMapped_RAM();

    if (!primask) {
        __enable_irq();
    }
}

/* Membaca langsung via CPU memory map (sangat cepat & aman di XIP) */
void Flash_ReadBytes(uint32_t flash_offset, uint8_t *buf, uint32_t len)
{
    if (len == 0 || buf == NULL) return;
    memcpy(buf, (const void *)(OCTOSPI1_MM_BASE + flash_offset), len);
}

void OCTOSPI1_LL_IndirectInit(void)
{
    OCTOSPI1_LL_IndirectInit_RAM();
}

void OCTOSPI1_LL_MemoryMapped(void)
{
    OCTOSPI1_LL_MemoryMapped_RAM();
}
