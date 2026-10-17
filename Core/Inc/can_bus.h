#ifndef CAN_BUS_H
#define CAN_BUS_H

#include <stdint.h>

/* CAN Bus dinonaktifkan sementara (belum ada modul CAN) */

#define CAN_ID_BOARD_A_CMD     0x201U
#define CAN_ID_BOARD_B_STATUS  0x181U

#define RELAY_CMD_OFF          0x00U
#define RELAY_CMD_ON           0x01U
#define RELAY_CMD_TOGGLE       0x02U
#define RELAY_CMD_STATUS_REQ   0x03U

typedef struct {
    uint8_t  relay_state;
    uint8_t  temperature;
    uint16_t voltage;
    uint16_t uptime;
    uint8_t  fault_flags;
    uint8_t  counter;
} CanBoardBData_t;

extern CanBoardBData_t g_can_b_data;
extern uint8_t         g_can_rx_flag;
extern uint32_t        g_can_rx_count;
extern uint32_t        g_can_tx_count;
extern uint8_t         g_can_err_count;

static inline void    CAN_Bus_Init(void) {}
static inline uint8_t CAN_Bus_SendCmd(uint8_t cmd) { (void)cmd; return 0; }
static inline void    CAN_Bus_RxFifo0Callback(void) {}
static inline void    CAN_Bus_RxFifo1Callback(void) {}

#endif /* CAN_BUS_H */
