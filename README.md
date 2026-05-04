# GPS Clock CH32V003 code
## How to use it
1. Download [ch32fun](https://github.com/cnlohr/ch32fun)
2. Clone this repository into the examples folder
```bash
git clone https://github.com/cnlohr/ch32fun.git
cd ch32fun/examples/
git clone https://github.com/Pasqalup/ch32_gps_clock.git
```
3. Follow my [guide](https://github.com/Pasqalup/CH32v003-MINI#programming) for programming the devboard
### Use the clock
#### Set the alarm time
1. Hold the set button for 3 seconds to enter alarm set mode
2. Use the UP/ALARM_OFF button to increase the hour (colon blinking)
3. Click set button to set minutes
4. Use UP/ALARM_OFF button to increase the minute (colon solid)
5. Click set button to return to regular clock mode
#### 
## How it works
1. Configure all peripherals
2. Send commands via UART to initialize PA1616 GPS Module
3. Wait for GPS module to return a response and parse that response to extract the current time
4. Write the current time to the display
- PPS Interrupt: update the current time based on precision PPS signal
- Check buttons
- Check for alarm
## Code breakdown
- `clock.c`: Main code
- `uart.c`/`uart.h`: Library for sending and recieveing UART using DMA (used to communicate to PA1616 GPS Module)
- `clock_i2c.c`/`clock_i2c.h`: Special i2c library specifically used to handle the unique protocol used by the CH455
