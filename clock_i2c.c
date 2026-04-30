#include "clock_i2c.h"

#define I2C_PRERATE 1000000

static uint8_t buttons = 0;
// ch455 digit bitmaps
// use dig_num2byte[digit] | colon_on to to get a digit with the colon on
static uint8_t dig_num2byte[10] = {
	0b00111111, //0
	0b00000110, //1
	0b01011011, //2
	0b01001111, //3
	0b01100110, //4
	0b01101101, //5
	0b01111101, //6
	0b00000111, //7
	0b01111111, //8
	0b01101111, //9
};
static uint8_t colon_on = 0b10000000;

static uint32_t _i2c_timeout = 0;
#define I2C_TIMEOUT_WAIT_FOR(condition, err_var) \
	do { \
		uint32_t to = _i2c_timeout; \
		while((condition)) \
			if(to-- == 0) {(err_var) = i2c_get_busy_error(); break;} \
	} while(0)

/// @brief Checks the I2C Status against a mask value, returns 1 if it matches
/// @param Status To match to
/// @return uint32_t masked status value: 1 if mask and status match
__attribute__((always_inline))
static inline uint32_t i2c_status(const uint32_t status_mask)
{
	uint32_t status = (uint32_t)I2C1->STAR1 | (uint32_t)(I2C1->STAR2 << 16);
	return (status & status_mask) == status_mask;
}
/// @brief Starts the I2C Bus for communications
/// @param None
/// @return None
__attribute__((always_inline))
static inline void i2c_start()
{
	// Send a START Signal and wait for it to assert
	I2C1->CTLR1 |= I2C_CTLR1_START;
	while(!i2c_status(I2C_EVENT_MASTER_MODE_SELECT));
}
/// @brief Stops the I2C Bus
/// @param None
/// @return None
__attribute__((always_inline))
static inline void i2c_stop()
{
	I2C1->CTLR1 |= I2C_CTLR1_STOP;
}

static inline i2c_err_t i2c_error(void)
{
	if(I2C1->STAR1 & I2C_STAR1_BERR)  {I2C1->STAR1 &= ~I2C_STAR1_BERR;  return I2C_ERR_BERR;}
	if(I2C1->STAR1 & I2C_STAR1_AF)    {I2C1->STAR1 &= ~I2C_STAR1_AF;    return I2C_ERR_NACK;}
	if(I2C1->STAR1 & I2C_STAR1_ARLO)  {I2C1->STAR1 &= ~I2C_STAR1_ARLO;  return I2C_ERR_ARLO;}
	if(I2C1->STAR1 & I2C_STAR1_OVR)   {I2C1->STAR1 &= ~I2C_STAR1_OVR;   return I2C_ERR_OVR;}

	return I2C_OK;
}
/// @brief Called when the I2C Bus Timesout - Returns any known error code
/// if applicable - returns generic I2C_ERR_BUSY if not
/// @param None
/// @return i2c_err_t error value
__attribute__((always_inline))
static inline uint32_t i2c_get_busy_error(void)
{

	i2c_err_t i2c_err = i2c_error();
	if(i2c_err == I2C_OK) i2c_err = I2C_ERR_BUSY;
	return i2c_err;
}

/// @brief Waits for the I2C Bus to be ready
/// @param None
/// @return i2c_err_t, I2C_OK if the bus is ready
__attribute__((always_inline))
static inline i2c_err_t i2c_wait()
{
	i2c_err_t i2c_ret = I2C_OK;
	I2C_TIMEOUT_WAIT_FOR((I2C1->STAR2 & I2C_STAR2_BUSY), i2c_ret);

	return i2c_ret;
}

i2c_err_t i2c_setup(){

	// Toggle the I2C Reset bit to init Registers
	RCC->APB1PRSTR |= RCC_APB1Periph_I2C1;
	RCC->APB1PRSTR &= ~RCC_APB1Periph_I2C1;

	// Enable the I2C Peripheral Clock
	RCC->APB1PCENR |= RCC_APB1Periph_I2C1;


	// Enable the selected I2C Port, and the Alternate Function enable bit
	RCC->APB2PCENR |= RCC_APB2Periph_GPIOC | RCC_APB2Periph_AFIO;

	// Reset the AFIO_PCFR1 register, then set it up
	AFIO->PCFR1 &= ~(0x04400002);
	AFIO->PCFR1 |= 0x00000000;


	// Clear, then set the GPIO Settings for SCL and SDA, on the selected port
	GPIOC->CFGLR &= ~(0x0F << (4 * 1));
	GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (4 * 1);	
	GPIOC->CFGLR &= ~(0x0F << (4 * 2));
	GPIOC->CFGLR |= (GPIO_Speed_10MHz | GPIO_CNF_OUT_OD_AF) << (4 * 2);

	// Set the Prerate frequency
	uint16_t i2c_conf = I2C1->CTLR2 & ~I2C_CTLR2_FREQ;
	i2c_conf |= (FUNCONF_SYSTEM_CORE_CLOCK / I2C_PRERATE) & I2C_CTLR2_FREQ;
	I2C1->CTLR2 = i2c_conf;

	// Set I2C Clock
	// Fast mode. Default to 33% Duty Cycle
	i2c_conf = (FUNCONF_SYSTEM_CORE_CLOCK / (3 * 400000)) & I2C_CKCFGR_CCR; //400khz
	i2c_conf |= I2C_CKCFGR_FS;

	I2C1->CKCFGR = i2c_conf;

	// Enable the I2C Peripheral
	I2C1->CTLR1 |= I2C_CTLR1_PE;

	return i2c_error();
}
void i2c_send_raw(const uint8_t *buf, const size_t len){
	// Wait for the I2C Bus the be Available
	i2c_err_t i2c_ret = i2c_wait();
	// Start the I2C Bus
	i2c_start();
	// Write the data
	if(i2c_ret == I2C_OK)    {
		uint8_t cbyte = 0;
		while(cbyte < len)    {
			// Write the byte and wait for it to finish transmitting
			I2C_TIMEOUT_WAIT_FOR(!(I2C1->STAR1 & I2C_STAR1_TXE), i2c_ret);
			//while(!(I2C1->STAR1 & I2C_STAR1_TXE));
			I2C1->DATAR = buf[cbyte];
			++cbyte;

			// Make sure no errors occured for this byte
			if(i2c_ret != I2C_OK || (i2c_ret = i2c_error()) != I2C_OK) break;
		}
	}
	// Wait for the bus to finish transmitting
	I2C_TIMEOUT_WAIT_FOR(!i2c_status(I2C_EVENT_MASTER_BYTE_TRANSMITTED), i2c_ret);
	// Signal a STOP
	i2c_stop();
}

void ch455_writeclock(uint8_t hour, uint8_t minute, bool colonis_on){
	uint8_t d0 = dig_num2byte[hour/10];
	uint8_t d1 = dig_num2byte[hour%10];
	uint8_t d2 = dig_num2byte[minute/10];
	uint8_t d3 = dig_num2byte[minute%10];
	if(colonis_on) {
		// colon is dp of dig 3 & 4 (d2 & d3 here)
		d2 |= colon_on;
		d3 |= colon_on;
	}
	i2c_send_raw((uint8_t[]){CH455_SETDIG0, d0}, 2);
	i2c_send_raw((uint8_t[]){CH455_SETDIG1, d1}, 2);
	i2c_send_raw((uint8_t[]){CH455_SETDIG2, d2}, 2);
	i2c_send_raw((uint8_t[]){CH455_SETDIG3, d3}, 2);
}
void ch455_read_key(){
	i2c_err_t i2c_ret = i2c_wait();
	i2c_start();
	if(i2c_ret == I2C_OK)    {
		// Send the Read Key Command
		I2C1->DATAR = CH455_READKEY;
		// Wait for the byte to be received
		I2C_TIMEOUT_WAIT_FOR(!(I2C1->STAR1 & I2C_STAR1_TXE), i2c_ret);
	}
	// Wait for the bus to finish transmitting
	I2C_TIMEOUT_WAIT_FOR(!i2c_status(I2C_EVENT_MASTER_BYTE_TRANSMITTED), i2c_ret);

	// Read the Key Value
	uint8_t key_val = 0;
	// Wait for the byte to be received
	I2C_TIMEOUT_WAIT_FOR(!(I2C1->STAR1 & I2C_STAR1_RXNE), i2c_ret);
	key_val = I2C1->DATAR;
	// Signal a STOP
	i2c_stop();

	// Process the Key Value
	// Key Value Format: 0b0 [pressed/released] [num2 num1 num0] [1] [dig1 dig0]
	// first bit always 0, 1 bit for press/release, 3 bits for num (0-7), one bit always high, 2 bits for digit (0-3) (we only use dig0)
	
	if(key_val & 0x03){ // only look for dig0
		uint8_t num = (key_val & 0x38) >> 3; // get the num value from the key_val
		if(key_val & 0x40){ // indicates button pressed
			buttons |= (1 << num);
		}
		else{ // button released
			buttons &= ~(1 << num);
		}
	}
}

uint8_t ch455_get_buttons(void)
{
	return buttons;
}
