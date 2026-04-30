#ifndef CLOCK_I2C_H
#define CLOCK_I2C_H

#include "ch32fun.h"
#include <stdbool.h>

#define CH455_SYSSET 0x48
#define CH455_SETDIG0 0x68
#define CH455_SETDIG1 0x6A
#define CH455_SETDIG2 0x6C
#define CH455_SETDIG3 0x6E
#define CH455_READKEY 0x4F

/// @brief I2C Error Codes
typedef enum {
    I2C_OK	  = 0,   // No Error. All OK
    I2C_ERR_BERR,   // Bus Error
    I2C_ERR_NACK,   // ACK Bit failed
    I2C_ERR_ARLO,   // Arbitration Lost
    I2C_ERR_OVR,    // Overun/underrun condition
    I2C_ERR_BUSY,   // Bus was busy and timed out
} i2c_err_t;

i2c_err_t i2c_setup(void);
void i2c_send_raw(const uint8_t *buf, size_t len);
void ch455_writeclock(uint8_t hour, uint8_t minute, bool colon_on);
void ch455_read_key(void);
uint8_t ch455_get_buttons(void);

#endif
