#ifndef __UART_H__
#define __UART_H__

#include "ch32fun.h"
#include <stdbool.h>

#define UART_BR 9600 // baud rate for GPS module
// longer rx buf for long nmea messages
#define RX_BUF_LEN 128 // size of receive circular buffer

extern u8 rx_buf[RX_BUF_LEN]; // DMA receive buffer for incoming data
extern u8 cmd_buf[RX_BUF_LEN]; // buffer for complete command strings

extern volatile uint16_t rx_tail;

void DMA1_Channel4_IRQHandler( void );
bool TxBusy(void);
void uart_setup( void );
void dma_uart_setup( void );
void dma_uart_tx( const void *data, uint32_t len );
uint16_t rx_available( void );
int16_t rx_read( void );
void rxEnable( void );
void txEnable( void );
bool cleanRead( volatile uint32_t *now, uint32_t timeout_ms );

#endif // __UART_H__