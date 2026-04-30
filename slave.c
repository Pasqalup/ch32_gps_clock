#define FUNCONF_SYSTEM_CORE_CLOCK  48000000
#include "ch32fun.h"

#include <stdbool.h>
#define UART_BR 115200
#define RX_BUF_LEN 64 // size of receive circular buffer
test

#define MY_ID '1'

u8 rx_buf[RX_BUF_LEN] = { 0 }; // DMA receive buffer for incoming data
u8 cmd_buf[RX_BUF_LEN] = { 0 }; // buffer for complete command strings
volatile uint16_t rx_tail = 0;
typedef enum {
	READING,
	TX_BUSY,
	IDLE,
	PROCESSING,
} SystemState_t;

static SystemState_t system_state = IDLE;
/*


*/


// DMA transfer completion interrupt. It will fire when the DMA transfer is
// complete.
__attribute__( ( interrupt ) ) __attribute__( ( section( ".srodata" ) ) ) 
void DMA1_Channel4_IRQHandler( void )
{
	// Clear flag
	DMA1->INTFCR |= DMA_CTCIF4;
}
bool TxBusy()
{
	// Check if the transmission complete flag is set.
	// return true if the flag is not set, meaning a transmission is in progress.
	return !( USART1->STATR & USART_STATR_TC );
}

static void uart_setup( void )
{
	// Enable UART and GPIOD
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOD | RCC_APB2Periph_USART1;

	// Push-Pull, 10MHz Output on D5, with AutoFunction
	GPIOD->CFGLR =
		( GPIOD->CFGLR & ~( 0xF << ( 4 * 6 ) ) ) | ( ( GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF ) << ( 4 * 6 ) );

	GPIOD->CFGLR = 
		( GPIOD->CFGLR & ~( ( 0xF << ( 4 * 6 ) ) | ( 0xF << ( 4 * 5 ) ) | ( 0xF << ( 4 * 4 ) ) ) ) | // clear config for D4, D5, D6 (DE,RX, TX)
		( ( GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF) << (4 * 6) ) | // set D6 (TX) to push-pull output with alternate function 
		( ( GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING ) << (4 * 5) ) | // set D5 (RX) to floating input
		( ( GPIO_Speed_10MHz | GPIO_CNF_OUT_PP_AF ) << (4 * 4) ); // set D4 (DE) to push-pull output with alternate function

	GPIOD->BSHR = (1 << (16+4) ); // Set D4 (DE) low to disable driver

	// USART1_RM = 10;
	//  Enable USART1 remap to D5/D6
	AFIO->PCFR1 = ( AFIO->PCFR1 & ~( 1 << 2 ) ) | ( 1 << 21 );
	// Setup UART for Tx 8n1
	USART1->CTLR1 = USART_WordLength_8b | USART_Parity_No | USART_Mode_Rx;
	USART1->CTLR2 = USART_StopBits_1;
	// Enable Tx DMA event
	USART1->CTLR3 = USART_DMAReq_Tx | USART_DMAReq_Rx;

	// Set baud rate and enable UART
	USART1->BRR = ( ( FUNCONF_SYSTEM_CORE_CLOCK ) + ( UART_BR ) / 2 ) / ( UART_BR );
	USART1->CTLR1 |= CTLR1_UE_Set;
}
static void dma_uart_setup( void )
{
	// Enable DMA peripheral
	RCC->AHBPCENR = RCC_AHBPeriph_SRAM | RCC_AHBPeriph_DMA1;

	// Disable channel just in case there is a transfer in progress
	DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;

	// USART1 TX uses DMA channel 4
	DMA1_Channel4->PADDR = (uint32_t)&USART1->DATAR;
	// MEM2MEM: 0 (memory to peripheral)
	// PL: 0 (low priority since UART is a relatively slow peripheral)
	// MSIZE/PSIZE: 0 (8-bit)
	// MINC: 1 (increase memory address)
	// CIRC: 0 (one shot)
	// DIR: 1 (read from memory)
	// TEIE: 0 (no tx error interrupt)
	// HTIE: 0 (no half tx interrupt)
	// TCIE: 1 (transmission complete interrupt enable)
	// EN: 0 (do not enable DMA yet)
	DMA1_Channel4->CFGR = DMA_CFGR1_MINC | DMA_CFGR1_DIR | DMA_CFGR1_TCIE;



	// Disable channel just in case there is a transfer in progress
	DMA1_Channel5->CFGR &= ~DMA_CFGR1_EN;
	// configure dma for UART reception, it should fire on RXNE
	DMA1_Channel5->MADDR = (u32)&rx_buf;
	DMA1_Channel5->PADDR = (u32)&USART1->DATAR;
	DMA1_Channel5->CNTR = RX_BUF_LEN;
	// MEM2MEM: 0 (memory to peripheral)
	// PL: 0 (low priority since UART is a relatively slow peripheral)
	// MSIZE/PSIZE: 0 (8-bit)
	// MINC: 1 (increase memory address)
	// PINC: 0 (peripheral address remains unchanged)
	// CIRC: 1 (circular)
	// DIR: 0 (read from peripheral)
	// TEIE: 0 (no tx error interrupt)
	// HTIE: 0 (no half tx interrupt)
	// TCIE: 0 (no transmission complete interrupt)
	// EN: 1 (enable DMA)
	DMA1_Channel5->CFGR = DMA_CFGR1_CIRC | DMA_CFGR1_MINC | DMA_CFGR1_EN;

	// Enable channel 4 interrupts
	NVIC_EnableIRQ( DMA1_Channel4_IRQn );
}
static void dma_uart_tx( const void *data, uint32_t len )
{
	// set system state to transmitting
	system_state = TX_BUSY;
	// switch to tx mode
	USART1->CTLR1 = ( USART1->CTLR1 & ~USART_Mode_Rx ) | USART_Mode_Tx; // remove rx, set tx

	// Set DE high to enable driver
	GPIOD->BSHR = ( 1 << ( 4 ) ); // Set D4 (DE) high to enable driver

	USART1->STATR &= ~USART_FLAG_TC; // Clear transmission complete flag
	// Disable DMA channel (just in case a transfer is pending)
	DMA1_Channel4->CFGR &= ~DMA_CFGR1_EN;
	// Set transfer length and source address
	DMA1_Channel4->CNTR = len;
	DMA1_Channel4->MADDR = (uint32_t)data;
	// Enable DMA channel to start the transfer
	DMA1_Channel4->CFGR |= DMA_CFGR1_EN;
}
uint16_t Serial_available(void)
{
	// Calculate the head index in the circular buffer based on the current value of the DMA counter.
	uint16_t rx_head = RX_BUF_LEN - DMA1_Channel5->CNTR;
	// Calculate the number of bytes available in the buffer by comparing the head index with the tail index.
	if ( rx_head >= rx_tail )
	{
		return rx_head - rx_tail;
	}
	else
	{
		// The DMA has wrapped around the end of the circular buffer
		return ( RX_BUF_LEN - rx_tail ) + rx_head;
	}
}
int16_t Serial_read( void )
{
	// Check if there are bytes available to read
	if ( Serial_available() == 0 )
	{
		return -1; // No data available
	}
	// Read the byte at the tail index and increment the tail index, wrapping around if necessary
	uint8_t byte = rx_buf[rx_tail];
	rx_tail = ( rx_tail + 1 ) % RX_BUF_LEN;
	return byte;
}
void rxEnable(void)
{
	// switch to rx mode
	USART1->CTLR1 = ( USART1->CTLR1 & ~USART_Mode_Tx ) | USART_Mode_Rx;
	// assert DE low to disable driver
	GPIOD->BSHR = ( 1 << ( 16 + 4 ) );
}
void txEnable(void)
{
	USART1->CTLR1 = ( USART1->CTLR1 & ~USART_Mode_Rx ) | USART_Mode_Tx;
}
uint8_t cmd_index = 0; // index for filling cmd_buf
int main()
{
	SystemInit();
	uart_setup();
	dma_uart_setup();
	/*
	FLOW:
	1. Begin in IDLE state, waiting for incoming data.
	2. Incoming data => READING
	3. READ a full command. If command is for us, go to PROCESSING. Else, go back to IDLE.
	4. PROCESS command.
	5a. No response needed => back to IDLE. EOF
	5b. Response needed => dma_uart_tx() => TX_BUSY
	6. Wait for transmission to complete. Disable driver. Go back to IDLE.
	*/
	while ( 1 )
	{
		//main loop
		switch ( system_state )
		{
		
		case IDLE: // waiting for incoming data; DE is low, driver disabled, UART in RX mode

			// check for incoming data
			if ( Serial_available() > 0 )
			{
				// move to reading state to process incoming data
				system_state = READING;

			}
			break;
		case TX_BUSY:
			// Wait for transmission to complete
			if ( !TxBusy() )
			{
				// Transmission is complete, switch back to idle state
				system_state = IDLE;
				// reset to rx mode and disable driver
				rxEnable();
			}
			break;
		case READING:
			if ( Serial_available() > 0 )
			{
				// theres stuff to read. pare into cmd_buf.
				int16_t byte = Serial_read();
				if ( byte != -1 )
				{ // valid byte read
					cmd_buf[cmd_index++] = (uint8_t)byte; // add byte to command buffer
					if ( byte == '\n' || cmd_index >= RX_BUF_LEN )
					{ // end of command
						cmd_buf[cmd_index] = '\0'; // null terminate command string
						cmd_index = 0; // reset index for next command
						

						// take care of command
						
						// check if command is for us
						if ( cmd_buf[0] == MY_ID )
						{ // command is for us
							// move to processing state to handle command
							system_state = PROCESSING;
						}
						else
						{
							// not for us, ignore and go back to idle
							system_state = IDLE;
						}
					}
					// else, keep reading until we get a full command
				}
				// else, no valid byte read, wait for next one
			}
			break;
		case PROCESSING: 
			// for this example, we just echo the command back
			dma_uart_tx( cmd_buf, sizeof(cmd_buf) ); //example transmit
			break;
		}
	}
}