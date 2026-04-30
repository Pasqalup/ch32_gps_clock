all : flash

TARGET:=clock

ADDITIONAL_C_FILES:=clock_i2c.c
ADDITIONAL_C_FILES+=uart.c

TARGET_MCU?=CH32V003
include ../../ch32fun/ch32fun.mk

flash : cv_flash
clean : cv_clean


