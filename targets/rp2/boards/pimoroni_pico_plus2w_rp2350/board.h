/* Copyright (c) 2017 Kaluma
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __RP2_PICO_H
#define __RP2_PICO_H

#include "jerryscript.h"
#include "hardware/flash.h"

// repl
#define KALUMA_REPL_BUFFER_SIZE 1024
#define KALUMA_REPL_HISTORY_SIZE 10

// Flash allocation map
//
// |         A        | B |     C     |     D      |
// |------------------|---|-----------|------------|
// |      1008K       |16K|   512K    |   14848K   |
//
// - A : binary (firmware)
// - B : storage (key-value database)
// - C : user program (js)
// - D : file system (lfs)
// (Total : 16MB)

// binary (1008KB)
#define KALUMA_BINARY_MAX 1008 * 1024

// flash (B + C + D = mimimal 1040KB)
#define KALUMA_FLASH_OFFSET KALUMA_BINARY_MAX
#define KALUMA_FLASH_SECTOR_SIZE FLASH_SECTOR_SIZE
#define KALUMA_FLASH_SECTOR_COUNT (PICO_FLASH_SIZE_BYTES - KALUMA_BINARY_MAX) / KALUMA_FLASH_SECTOR_SIZE
#define KALUMA_FLASH_PAGE_SIZE FLASH_PAGE_SIZE

// storage on flash (16KB)
#define KALUMA_STORAGE_SECTOR_BASE 0
#define KALUMA_STORAGE_SECTOR_COUNT (16 * 1024) / KALUMA_FLASH_SECTOR_SIZE

// user program on flash (512KB)
#define KALUMA_PROG_SECTOR_BASE KALUMA_STORAGE_SECTOR_COUNT
#define KALUMA_PROG_SECTOR_COUNT (512 * 1024) / KALUMA_FLASH_SECTOR_SIZE

// file system on flash (16384KB - (A + B + C) = 14848KB)
// - sector base : 132 (16KB + 512KB)
// - sector count : 3625 (14848KB)
// - use block device : new Flash(132, 3625)

// -----------------------------------------------------------------

#define KALUMA_GPIO_COUNT 29
// #define ADC_NUM 3
#define PWM_NUM 27
// #define I2C_NUM 2
// #define SPI_NUM 2
// #define UART_NUM 2
// #define LED_NUM 1
// #define BUTTON_NUM 0
// #define PIO_NUM 2
#define PIO_SM_NUM 4

#define ADC_RESOLUTION_BIT 12
#define PWM_CLK_REF 1250
#define I2C_MAX_CLOCK 1000000
#define SCR_LOAD_GPIO 22  // GPIO 22

void board_init();

#endif /* __RP2_PICO_H */
