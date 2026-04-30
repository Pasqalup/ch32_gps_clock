#include "ch32fun.h"
#include <stdbool.h>
#include "clock_i2c.h"
#include "uart.h"

//GPS update rate is set to 0.1Hz, so we should get a pulse every 10 seconds
#define PPS_PORT GPIOC
#define PPS_PIN 5
#define GPS_SET_RMC_ONLY "$PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0*29"
#define GPS_SET_0_1HZ "$PMTK220,10000*2F"

//alarm buzzer pin
#define BUZZER_PORT GPIOB
#define BUZZER_PIN 0

// which buttons are we looking for?
#define BUTTON_1_MASK 0x01
#define BUTTON_2_MASK 0x02

// macros for systick timing functions
#define millis() (systick_millis)
#define micros() (SysTick->CNT / DELAY_US_TIME)

// millisecond systick timer
volatile uint32_t systick_millis;

// save last button state to detect changes
uint8_t last_button_state = 0; 
// handles long button presses
uint32_t last_button1_press_time = 0;
uint32_t last_button2_press_time = 0;

// global variable to store alarm time; format is 0xHHMM, where HH is hours and MM is minutes
uint16_t alarm_time = 0; // byte 1: hours, byte 2: minutes
// global time variables for clock time
uint8_t hours, minutes, seconds;
// all flags - [pps triggered][colon on][alarm stop]
volatile uint8_t flags;

typedef enum
{
    GPS_SETUP,
    UART_WAIT_FOR_RX,
	CLOCK_READY,
} State_t;
State_t state = GPS_SETUP;
typedef enum
{
	REGULAR_TIME,
	ALARM_SET_DIG1,
	ALARM_SET_DIG2
} TimeMode_t;
TimeMode_t time_mode = REGULAR_TIME;
void configure_buzzer(){
	RCC->APB2PCENR |= RCC_APB2Periph_TIM1; // enable clock for timer 1
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOB; // enable clock for GPIOB
}
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


// pps interrupt handler - precision second pulse
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
void systick_init(void)
{
	// Reset any pre-existing configuration
	SysTick->CTLR = 0x0000;
	
	// Set the compare register to trigger once per millisecond
	SysTick->CMP = DELAY_MS_TIME - 1;

	// Reset the Count Register, and the global millis counter to 0
	SysTick->CNT = 0x00000000;
	systick_millis = 0x00000000;
	
	// Set the SysTick Configuration
	// NOTE: By not setting SYSTICK_CTLR_STRE, we maintain compatibility with
	// busywait delay funtions used by ch32v003_fun.
	SysTick->CTLR |= SYSTICK_CTLR_STE   |  // Enable Counter
	                 SYSTICK_CTLR_STIE  |  // Enable Interrupts
	                 SYSTICK_CTLR_STCLK ;  // Set Clock Source to HCLK/1
	
	// Enable the SysTick IRQ
	NVIC_EnableIRQ(SysTick_IRQn);
}
void SysTick_Handler(void) __attribute__((interrupt));
void SysTick_Handler(void)
{
	// Increment the Compare Register for the next trigger
	// If more than this number of ticks elapse before the trigger is reset,
	// you may miss your next interrupt trigger
	// (Make sure the IQR is lightweight and CMP value is reasonable)
	SysTick->CMP += DELAY_MS_TIME;

	// Clear the trigger state for the next IRQ
	SysTick->SR = 0x00000000;

	// Increment the milliseconds count
	systick_millis++;
}

void handle_alarm(){
	// if we are in regular time mode and the current time matches the alarm time, buzz
	// if we press snooze, stop buzzing
	if( time_mode == REGULAR_TIME && ( ( hours << 8 ) | minutes ) == alarm_time && !( flags & 0x04 ) ){
		// buzz on and off every 500ms
		if( ( millis() / 500 ) % 2 ){
			// turn buzzer on
		} else{
			// turn buzzer off
		}
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
				ch455_read_key(); //update button state;
				uint8_t button_state = ch455_get_key();
				//detect rising edge
				uint8_t changed_buttons = ( button_state ^ last_button_state ) & button_state;
				uint8_t rising_edges = changed_buttons & button_state;
				uint8_t falling_edges = changed_buttons & ~button_state;
				if( rising_edges & BUTTON_1_MASK ){
					last_button1_press_time = millis();
				} else if( rising_edges & BUTTON_2_MASK ){
					last_button2_press_time = millis();
				}
				// if button 1 is held go into alarm set mode
				// if not it is used to advance digits in alarm set mode
				if (falling_edges & BUTTON_1_MASK) {
					if( millis() - last_button1_press_time >= 3000 ){ // if button was held for at least 3 seconds
						time_mode = ALARM_SET_DIG1;
					} else{
						//regular press to advance digits in alarm set mode
						if( time_mode == ALARM_SET_DIG1 ){
							time_mode = ALARM_SET_DIG2;
						} else if( time_mode == ALARM_SET_DIG2 ){
							time_mode = REGULAR_TIME;
						}
					}
					if(millis-last_button1_press_time >=10000){
						// 10 seconds press rereads gps data
						state = UART_WAIT_FOR_RX; 
					}
				}
				if(falling_edges & BUTTON_2_MASK){ // release button 2 to increment current digit
					if( time_mode == ALARM_SET_DIG1 ){ // set hours
						alarm_time += 0x0100; // increment the hour byte
						if( ( alarm_time >> 8 ) > 23 ){ // roll over if hours exceed 23
							alarm_time &= 0x00FF; // reset hour byte to 0
						}
					} else if( time_mode == ALARM_SET_DIG2 ){ //set minutes
						alarm_time += 0x01; // increment the minute byte
						if( ( alarm_time & 0x00FF ) > 59 ){ // roll over if minutes exceed 59
							alarm_time &= 0xFF00; // reset minute byte to 0
						}
					}

					// buttons 2 acts as snooze/alarm stop
					flags |= 0x04; // set bit 2 to indicate alarm stop/snooze
				}

				last_button_state = button_state; // store button state for next iteration to detect changes
		
				switch ( time_mode )
				{
					case REGULAR_TIME: // display regular time
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
						break;
					case ALARM_SET_DIG1: // display alarm hour with blinking colon
						ch455_writeclock( ( alarm_time >> 8 ) & 0xFF, ( alarm_time & 0x00FF ), ( millis() / 500 ) % 2 ); // blink colon every 500ms
						break;
					case ALARM_SET_DIG2: // display alarm minute with solid colon
						ch455_writeclock( ( alarm_time >> 8 ) & 0xFF, ( alarm_time & 0x00FF ), 1 ); // solid colon
						break;

				
        }
    }

}