/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Default Main Application (STM32H523CET6)
  *                   - Eksekusi langsung XIP OctoSPI Flash @ 0x90001000
  *                   - W5500 via SPI1 + DHCP Socket 1 + Web Server / OTA Socket 0
  *                   - MQTT Client Socket 2
  *                   - Web Dashboard / Web Config / Secure Packet (SHA-256)
  *                   - CAN Bus: Dinonaktifkan sementara (commented out)
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "crc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "octospi_ll.h"
#include "dhcp.h"
#include "mqtt_config.h"
#include "mqtt_client.h"
#include "web_config.h"
#include "web_cmnd.h"
#include "html_flash.h"
#include "html_landing.h"
#include "secure_packet.h"
#include "can_bus.h"
#include "ota.h"
#include "bl0906.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */
/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */
#define DEBUG_MQTT
#define MAX_FIRMWARE_SIZE     (1020UL * 1024UL)

/* Alamat partisi di OctoSPI Flash eksternal */
#define FLASH_APP_META        0x00000000U
#define FLASH_APP_BIN         0x00001000U
#define FLASH_STAGING_META    0x00100000U  /* 1 MB offset (Sektor 256) */
#define FLASH_STAGING_BIN     0x00101000U  /* 1 MB + 4 KB (Sektor 257) */

/* W5500 Ethernet Defines */
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

uint8_t ram_buffer[CHUNK_BUFFER_SIZE];
/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */
/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
volatile uint32_t g_ms_tick = 0;
volatile uint8_t  g_phy_reg = 0;
volatile uint8_t  g_w5500_version = 0;

/* Variabel dummy untuk CAN bus (dinonaktifkan sementara) */
CanBoardBData_t g_can_b_data = {0};
uint8_t         g_can_rx_flag = 0;
uint32_t        g_can_rx_count = 0;
uint32_t        g_can_tx_count = 0;
uint8_t         g_can_err_count = 0;

#ifdef DEBUG_MQTT
uint8_t g_dbg_s2_tx_size = 0;
uint8_t g_dbg_s2_sr     = 0;
uint8_t g_dbg_s2_fsr_hi = 0;
uint8_t g_dbg_s2_fsr_lo = 0;
#endif
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
/* USER CODE BEGIN PFP */
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* ==============================================================================
 * DRIVER SPI1 - W5500 ETHERNET BARE-METAL (STM32H5 SPI1)
 * ============================================================================== */
static inline uint8_t SPI1_Transfer(uint8_t data)
{
    uint32_t t = 200000;
    while (!LL_SPI_IsActiveFlag_TXP(SPI1))
    {
        if (--t == 0) return 0xFF;
    }
    LL_SPI_TransmitData8(SPI1, data);
    t = 200000;
    while (!LL_SPI_IsActiveFlag_RXP(SPI1))
    {
        if (--t == 0) return 0xFF;
    }
    return LL_SPI_ReceiveData8(SPI1);
}

void W5500_WriteReg(uint16_t addr, uint8_t block, uint8_t data)
{
    LL_GPIO_ResetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
    SPI1_Transfer((uint8_t)(addr >> 8));
    SPI1_Transfer((uint8_t)addr);
    SPI1_Transfer(block | 0x04);
    SPI1_Transfer(data);
    LL_GPIO_SetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
}

uint8_t W5500_ReadReg(uint16_t addr, uint8_t block)
{
    LL_GPIO_ResetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
    SPI1_Transfer((uint8_t)(addr >> 8));
    SPI1_Transfer((uint8_t)addr);
    SPI1_Transfer(block | 0x00);
    uint8_t val = SPI1_Transfer(0xFF);
    LL_GPIO_SetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
    return val;
}

void W5500_WriteBuf(uint16_t addr, uint8_t block, const uint8_t *buf, uint16_t len)
{
    LL_GPIO_ResetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
    SPI1_Transfer((uint8_t)(addr >> 8));
    SPI1_Transfer((uint8_t)addr);
    SPI1_Transfer(block | 0x04);
    for (uint16_t i = 0; i < len; i++) SPI1_Transfer(buf[i]);
    LL_GPIO_SetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
}

void W5500_ReadBuf(uint16_t addr, uint8_t block, uint8_t *buf, uint16_t len)
{
    LL_GPIO_ResetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
    SPI1_Transfer((uint8_t)(addr >> 8));
    SPI1_Transfer((uint8_t)addr);
    SPI1_Transfer(block | 0x00);
    for (uint16_t i = 0; i < len; i++) buf[i] = SPI1_Transfer(0xFF);
    LL_GPIO_SetOutputPin(ETH_CS_GPIO_Port, ETH_CS_Pin);
}

void W5500_InitNetwork(void)
{
    LL_GPIO_ResetOutputPin(ETH_RST_GPIO_Port, ETH_RST_Pin);
    for (volatile int i = 0; i < 250000; i++);
    LL_GPIO_SetOutputPin(ETH_RST_GPIO_Port, ETH_RST_Pin);
    for (volatile int i = 0; i < 250000; i++);

    W5500_WriteReg(0x0000, W5500_COMMON_REG_OP, 0x80);
    for (volatile int i = 0; i < 100000; i++);

    static const uint8_t default_mac[6] = { 0x00, 0x08, 0xDC, 0x11, 0x22, 0x33 };
    for (int i = 0; i < 6; i++)
        W5500_WriteReg(0x0009 + i, W5500_COMMON_REG_OP, default_mac[i]);

    W5500_WriteReg(0x001E, W5500_S0_REG_OP, 8);
    W5500_WriteReg(0x001F, W5500_S0_REG_OP, 8);
    W5500_WriteReg(0x001E, 0x28, 2);
    W5500_WriteReg(0x001F, 0x28, 2);
    W5500_WriteReg(0x001E, 0x48, 2);
    W5500_WriteReg(0x001F, 0x48, 2);

    for (volatile int i = 0; i < 100000; i++);

    g_w5500_version = W5500_ReadReg(0x0039, W5500_COMMON_REG_OP);
    g_phy_reg = W5500_ReadReg(0x002E, W5500_COMMON_REG_OP);
}

static inline void W5500_WaitCR(uint8_t block, uint8_t ms)
{
    uint32_t t_start = g_ms_tick;
    while (W5500_ReadReg(Sn_CR, block))
    {
        if ((g_ms_tick - t_start) >= ms)
        {
            break;
        }
    }
}

void W5500_SocketOpenListen(uint16_t port)
{
    W5500_WriteReg(Sn_MR,   W5500_S0_REG_OP, 0x01);
    W5500_WriteReg(Sn_PORT,     W5500_S0_REG_OP, (uint8_t)(port >> 8));
    W5500_WriteReg(Sn_PORT + 1, W5500_S0_REG_OP, (uint8_t)port);
    W5500_WriteReg(Sn_CR,   W5500_S0_REG_OP, CMD_OPEN);
    W5500_WaitCR(W5500_S0_REG_OP, 5);

    if (W5500_ReadReg(Sn_SR, W5500_S0_REG_OP) != SOCK_INIT) return;

    W5500_WriteReg(Sn_CR, W5500_S0_REG_OP, CMD_LISTEN);
    W5500_WaitCR(W5500_S0_REG_OP, 5);
}

uint16_t W5500_GetTxFreeSize(void)
{
    uint16_t fsr1, fsr2;
    uint32_t t_start = g_ms_tick;
    do {
        fsr1 = ((uint16_t)W5500_ReadReg(Sn_TX_FSR,     W5500_S0_REG_OP) << 8) |
                          W5500_ReadReg(Sn_TX_FSR + 1, W5500_S0_REG_OP);
        fsr2 = ((uint16_t)W5500_ReadReg(Sn_TX_FSR,     W5500_S0_REG_OP) << 8) |
                          W5500_ReadReg(Sn_TX_FSR + 1, W5500_S0_REG_OP);
        if ((g_ms_tick - t_start) > 50) return 0;
    } while (fsr1 != fsr2);
    return fsr1;
}

void W5500_SendTCP(const uint8_t *data, uint16_t len)
{
    if (len == 0 || data == NULL) return;

    // Tunggu sampai buffer hardware W5500 memiliki ruang cukup
    uint32_t t_start = g_ms_tick;
    while (W5500_GetTxFreeSize() < len)
    {
        uint8_t sr = W5500_ReadReg(Sn_SR, W5500_S0_REG_OP);
        if (sr != SOCK_ESTABLISHED && sr != SOCK_CLOSE_WAIT) return;
        if (g_ms_tick - t_start > 1500) return;
    }

    uint16_t ptr = ((uint16_t)W5500_ReadReg(Sn_TX_WR,     W5500_S0_REG_OP) << 8) |
                               W5500_ReadReg(Sn_TX_WR + 1, W5500_S0_REG_OP);
    uint16_t offset = ptr & 0x1FFF; // 8KB buffer mask
    if ((offset + len) > 0x2000)
    {
        uint16_t part1 = 0x2000 - offset;
        W5500_WriteBuf(offset, W5500_S0_TX_OP, data, part1);
        W5500_WriteBuf(0x0000, W5500_S0_TX_OP, data + part1, len - part1);
    }
    else
    {
        W5500_WriteBuf(offset, W5500_S0_TX_OP, data, len);
    }
    ptr += len;
    W5500_WriteReg(Sn_TX_WR,     W5500_S0_REG_OP, (uint8_t)(ptr >> 8));
    W5500_WriteReg(Sn_TX_WR + 1, W5500_S0_REG_OP, (uint8_t)ptr);
    W5500_WriteReg(Sn_CR,        W5500_S0_REG_OP, CMD_SEND);
    W5500_WaitCR(W5500_S0_REG_OP, 10);
}

/* ==============================================================================
 * CATATAN: Modul Web Server & OTA dipindahkan ke Core/Inc/ota.h & Core/Src/ota.c
 *          (Fungsi: OTA_Init(), OTA_Process() / Process_Web_OTA())
 * ============================================================================== */

/* ==============================================================================
 * UART DHCP STATUS REPORTER
 * ============================================================================== */
static void UART_PrintDHCPStatus(void)
{
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "%d.%d.%d.%d (Status: %d)\r\n",
                       (int)g_dhcp_ip[0], (int)g_dhcp_ip[1],
                       (int)g_dhcp_ip[2], (int)g_dhcp_ip[3],
                       (int)g_dhcp_state);
    if (len > 0)
    {
        for (int i = 0; i < len; i++)
        {
            while (!LL_USART_IsActiveFlag_TXE_TXFNF(USART1));
            LL_USART_TransmitData8(USART1, (uint8_t)buf[i]);
        }
    }
}
/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
    SCB->VTOR = 0x90001000UL;
    __enable_irq();
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */
    SysTick->CTRL |= SysTick_CTRL_TICKINT_Msk;
  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_SPI1_Init();
  MX_CRC_Init();
  MX_USART1_UART_Init();
  MX_UART5_Init();
  /* USER CODE BEGIN 2 */
    LL_SPI_SetTransferSize(SPI1, 0);
    LL_SPI_Enable(SPI1);
    LL_SPI_StartMasterTransfer(SPI1);

    /* W5500 Init */
    W5500_InitNetwork();

    /* DHCP Init */
    DHCP_Init();

    /* Web Config & Crypto Init */
    WebConfig_Init();
    SecurePacket_Init();

    /* CAN Bus: Dinonaktifkan sementara */
    // CAN_Bus_Init();

    /* BL0906 Energy Meter Init */
    BL0906_Init();

    /* MQTT Init */
    MQTT_Init();

#ifdef DEBUG_MQTT
    W5500_WriteReg(0x001E, 0x48, 2);
    W5500_WriteReg(0x001F, 0x48, 2);
    g_dbg_s2_tx_size = W5500_ReadReg(0x001E, 0x48);
#endif
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        /* Bagian CAN Bus test request dinonaktifkan sementara */
        /*
        static uint32_t last_test = 0;
        if ((g_ms_tick - last_test) >= 1000) {
            last_test = g_ms_tick;
            CAN_Bus_SendCmd(RELAY_CMD_STATUS_REQ);
        }
        */

        DHCP_Process();

        /* Trigger 1: Cetak saat berhasil dapat IP DHCP (edge-triggered) */
        static uint8_t s_dhcp_bound_last = 0;
        uint8_t is_bound = DHCP_IsBound();
        if (is_bound && !s_dhcp_bound_last)
        {
            UART_PrintDHCPStatus();
        }
        s_dhcp_bound_last = is_bound;

        /* Trigger 2: Cetak saat ada karakter masuk di USART1 RX */
        if (LL_USART_IsActiveFlag_ORE(USART1))
        {
            LL_USART_ClearFlag_ORE(USART1);
        }
        if (LL_USART_IsActiveFlag_RXNE_RXFNE(USART1))
        {
            (void)LL_USART_ReceiveData8(USART1);
            UART_PrintDHCPStatus();
        }

        if (DHCP_IsBound())
        {
            OTA_Process();
            static MqttConfig_t s_mqtt_cfg;
            static uint8_t s_cfg_loaded = 0;
            if (!s_cfg_loaded || g_config_updated) {
                MqttConfig_Load(&s_mqtt_cfg);
                s_cfg_loaded     = 1;
                g_config_updated = 0;
                MQTT_Init();
            }
            MQTT_Process(&s_mqtt_cfg);
        }
        static uint32_t lTime;
        if(g_ms_tick-lTime>=100)
        {
        	LL_GPIO_TogglePin(LED_BUILTIN_GPIO_Port, LED_BUILTIN_Pin);
        	g_phy_reg = W5500_ReadReg(0x002E, W5500_COMMON_REG_OP);
        	lTime = g_ms_tick;
        }

        /* BL0906 Energy Meter non-blocking process */
        BL0906_Process();
        /* Bagian CAN Bus RX flag dinonaktifkan sementara */
        /*
        if (g_can_rx_flag) {
            g_can_rx_flag = 0;
        }
        */
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_5);
  while(LL_FLASH_GetLatency()!= LL_FLASH_LATENCY_5)
  {
  }

  LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE0);
  while (LL_PWR_IsActiveFlag_VOS() == 0)
  {
  }
  LL_RCC_HSE_Enable();

   /* Wait till HSE is ready */
  while(LL_RCC_HSE_IsReady() != 1)
  {
  }

  LL_RCC_PLL1_SetSource(LL_RCC_PLL1SOURCE_HSE);
  LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
  LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
  LL_RCC_PLL1_SetM(2);
  LL_RCC_PLL1_SetN(125);
  LL_RCC_PLL1_SetP(2);
  LL_RCC_PLL1_SetQ(2);
  LL_RCC_PLL1_SetR(2);
  LL_RCC_PLL1P_Enable();
  LL_RCC_PLL1_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL1_IsReady() != 1)
  {
  }

  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);

   /* Wait till System clock is ready */
  while(LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL1)
  {
  }

  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_1);
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1);
  LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_1);
  LL_SetSystemCoreClock(250000000);

   /* Update the time base */
  if (HAL_InitTick (TICK_INT_PRIORITY) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  LL_RCC_PLL2_SetSource(LL_RCC_PLL2SOURCE_HSE);
  LL_RCC_PLL2_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
  LL_RCC_PLL2_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
  LL_RCC_PLL2_SetM(2);
  LL_RCC_PLL2_SetN(125);
  LL_RCC_PLL2_SetP(4);
  LL_RCC_PLL2_SetQ(2);
  LL_RCC_PLL2_SetR(2);
  LL_RCC_PLL2P_Enable();
  LL_RCC_PLL2_Enable();

   /* Wait till PLL is ready */
  while(LL_RCC_PLL2_IsReady() != 1)
  {
  }

}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @param None
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
