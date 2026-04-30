#include "ch32fun.h"
#include <stdbool.h>
#include "clock_i2c.h"
#include "uart.h"

#define PPS_PORT GPIOC
#define PPS_PIN 5
#define GPS_SET_RMC_ONLY "$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29"
#define GPS_SET_0_1HZ "$PMTK220,10000*2F"

typedef enum
{
    GPS_SETUP,
    UART_WAIT_FOR_RX,
	CLOCK_READY,
} State_t;
State_t state = GPS_SETUP;
uint8_t hours, minutes, seconds;
// flags - bit 0: pps irq flag bit 1: colon on/off flag
volatile uint8_t flags;
void configure_pps(){
    // configure pin as input
    GPIOC->CFGLR &= ~(0x0F << (4 * 5));
    GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_IN_FLOATING) << (4 * 5);
    // configure pin change interrupt for rising edge on port c5
    AFIO->EXTICR = AFIO_EXTICR_EXTI5_PC;

    EXTI->FTENR &= ~(1 << 0);  // Disable falling edge
    EXTI->RTENR |= (1 << 0);   // Enable rising edge
    EXTI->INTENR |= (1 << 0);  // Enable EXTI0

    NVIC_EnableIRQ(EXTI7_0_IRQn);
    NVIC_SetPriority(EXTI7_0_IRQn, 1);

}

void EXTI7_0_IRQHandler( void ) __attribute__((interrupt));
void EXTI7_0_IRQHandler( void ) 
{
    if(EXTI->INTFR & (1 << 0)){ // Check if EXTI0 caused the interrupt
        // Clear the interrupt flag
        EXTI->INTFR = (1 << 0);
        // we are at a precision second now!!!!
		flags |= 0x01; // Set bit 0 to indicate a PPS event occurred
		seconds++; // increment seconds; the main loop will handle rolling over minutes and hours and updating the
		           // display
    }
}
int main(){
    SystemInit();

    //start i2c
    i2c_setup();
	// configure pps input and interrupt
    configure_pps();
	// configure uart for communication with the PA1616
	uart_setup();
	dma_uart_setup();

    //ch455 set system parameters
    //0x0 [koff] [INTENS][7SEG][SLEEP]0[ENA]
    uint8_t set = 0b001110001; //0, keyboard on; full brightness, 8seg mode; sleep off, 0, enable
    i2c_send_raw((uint8_t[]){CH455_SYSSET, set}, 2);

    //main loop
    while(1){
		switch ( state )
		{
		
            case GPS_SETUP: 
                // set to just RMC
                dma_uart_tx( GPS_SET_RMC_ONLY, sizeof( GPS_SET_RMC_ONLY ) );
				while ( TxBusy() )
				{
				} // wait for transmission to complete
                // set to 0.1Hz update rate
                dma_uart_tx( GPS_SET_0_1HZ, sizeof( GPS_SET_0_1HZ ) );
				while ( TxBusy() )
				{
				} // wait for transmission to complete
				state = UART_WAIT_FOR_RX;
				break;
			case UART_WAIT_FOR_RX:
				if ( rx_available() > 0 )
				{
					// read the incoming data and parse
					cleanRead();
					// check if the message is an RMC message
                    if (cmd_buf[0] == '$' && cmd_buf[1] == 'G' && cmd_buf[2] == 'P' && cmd_buf[3] == 'R' && cmd_buf[4] == 'M' && cmd_buf[5] == 'C')
                    {
						uint8_t current_field = 0;
						// byte 1: hours, byte 2: minutes, byte 3: seconds byte 4: flags (bit 0: valid fix)
						uint32_t response_parsed = 0; 

						// loop until the null terminator of the command buffer
						for ( uint8_t i = 0; cmd_buf[i] != '\0'; i++ ) 
						{
                            if (current_field == 1 && cmd_buf[i] == ',') //utc is field 1; this runs when field 1 ends
                            {
                                // parse the time from the utc field, which is in the format hhmmss.sss
								response_parsed |=
									( cmd_buf[i - 7] - '0' ) * 10 + ( cmd_buf[i - 6] - '0' ); // put hours in byte 1
								response_parsed |= (( cmd_buf[i - 5] - '0' ) * 10 + ( cmd_buf[i - 4] - '0' )) << 8; // put minutes in byte 2
								response_parsed |= (( cmd_buf[i - 3] - '0' ) * 10 + ( cmd_buf[i - 2] - '0' )) << 16; // put seconds in byte 3
                            }
							if ( current_field == 2 &&
									 cmd_buf[i] == ',') // status is field 2; this runs when field 2 ends
                            {
                                // check if status is A (valid fix) or V (invalid fix)
                                if (cmd_buf[i - 1] == 'A')
                                {
                                    response_parsed |= 0x01 << 24; // set bit 0 of byte 4 to indicate valid fix
                                }
                            }
							if ( cmd_buf[i] == ',' ) // fields are separated by commas
                            {
                                current_field++;
                            }
							if ( current_field > 2 )
							{
								if ( response_parsed & ( 0x01 << 24 ) ) // if we have a valid fix, set global time variables
                                {
									seconds = ( response_parsed >> 16 ) & 0xFF;
									minutes = ( response_parsed >> 8 ) & 0xFF;
									hours = response_parsed & 0xFF;
                                }
								state = CLOCK_READY;
								break;
							}
							
						}
                    }
                }
				break;

            case CLOCK_READY:
				if ( flags & 0x01 ) // if we have a pps event, update the display
				{
					// clear the pps flag
					flags &= ~0x01;
					// toggle colon flag
					flags ^= 0x02;
					// handle rolling over seconds, minutes, hours
					if ( seconds >= 60 )
					{
						seconds = 0;
						minutes++;
						if ( minutes >= 60 )
						{
							minutes = 0;
							hours++;
							if ( hours >= 24 )
							{
								hours = 0;
							}
						}
					}
					// update the display with the current time
					ch455_writeclock( hours, minutes, flags & 0x02 );
				}
				ch455_read_key(); //update button state;


		
        }
    }

}