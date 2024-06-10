/* Copyright (c) 2019 Kaluma
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
 *
 *
 */

#include "gc_16bit_prims.h"

#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "gc.h"
#include "jerryscript.h"
#include "jerryxx.h"
#include "spi.h"
#include "gpio.h"
#include "system.h"

static const char keyword_bus[]         = "bus";
static const char keyword_cmd_select[]  = "cmd";
static const char keyword_chip_select[] = "cs";
static const char keyword_reset[]       = "reset";

static uint8_t command_select;
static uint8_t chip_select;
static uint8_t spi_bus;

// In case of a WaveShare there is a shift register in front of it, we must fill it with 16 bits before it shifts it in parallel
#ifdef WAVESHARE_PARALLEL

static void gc_send_command(uint8_t command, const uint8_t length, const uint8_t data[]) {
    // Command to send..
    uint8_t buffer[2];
    buffer[0] = command;
    buffer[1] = command;

    km_gpio_write (command_select, 0);
    km_gpio_write (chip_select, 0);
    km_spi_send(spi_bus, buffer, 2, 100);
    km_gpio_write (chip_select, 1);
    km_gpio_write (command_select, 1);

    // Send the data that go with the command..
    if (length > 0) {
      km_gpio_write (chip_select, 0);
      for (uint8_t index = 0; index < length; index++) {
        buffer[0] = data[index];
        buffer[1] = data[index];
        km_spi_send(spi_bus, buffer, 2, 100);
      }
      km_gpio_write (chip_select, 1);
    }
}

#else

static void gc_send_command(uint8_t command, const uint8_t length, const uint8_t data[]) {
    // Command to send..

    km_gpio_write (command_select, 0);
    km_gpio_write (chip_select, 0);
    km_spi_send(spi_bus, &command, 1, 100);
    km_gpio_write (chip_select, 1);
    km_gpio_write (command_select, 1);

    // Send the data that go with the command..
    if (length > 0) {
      km_gpio_write (chip_select, 0);
      km_spi_send(spi_bus, (uint8_t*) data, length, 100);
      km_gpio_write (chip_select, 1);
    }
}

#endif

static void gc_initialize_display() {

  static const uint8_t data_C2[] = { 0x33 };
  static const uint8_t data_C5[] = { 0x00, 0x1E, 0x80 };
  static const uint8_t data_B1[] = { 0xB0 };
  static const uint8_t data_E0[] = { 0x00, 0x13, 0x18, 0x04, 0x0F, 0x06, 0x3A, 0x56, 0x4D, 0x03, 0x0A, 0x06, 0x30, 0x3E, 0x0F };
  static const uint8_t data_E1[] = { 0x00, 0x13, 0x18, 0x01, 0x11, 0x06, 0x38, 0x34, 0x4D, 0x06, 0x0D, 0x0B, 0x31, 0x37, 0x0F };
  static const uint8_t data_3A[] = { 0x66 }; // 18 pixel deep color
  // static const uint8_t data_3A[] = { 0x77 }; // 24 pixel deep color
  static const uint8_t data_B6[] = { 0x00, 0x62 };
  static const uint8_t data_36[] = { 0x28 };

  gc_send_command(0x21,  0, NULL );
  gc_send_command(0xC2,  sizeof(data_C2), data_C2);
  gc_send_command(0xC5,  sizeof(data_C5), data_C5);
  gc_send_command(0xB1,  sizeof(data_B1), data_B1);
  gc_send_command(0xE0,  sizeof(data_E0), data_E0);
  gc_send_command(0xE1,  sizeof(data_E1), data_E1);
  gc_send_command(0x3A,  sizeof(data_3A), data_3A);

  gc_send_command(0x11,  0, NULL);
  km_delay(120);
  gc_send_command(0x29,  0, NULL);
  gc_send_command(0xB6,  sizeof(data_B6), data_B6);
  gc_send_command(0x36,  sizeof(data_36), data_36);
}

/* 
 */
static void gc_fill_area(uint16_t x, uint16_t y, uint16_t width, uint16_t height, gc_color color) {
    uint8_t point[4];
    uint32_t pixels = height * width;

    point[0] = (x >> 8);
    point[1] = (x & 0xFF);
    point[2] = ((x + width - 1) >> 8);
    point[3] = ((x + width - 1) & 0xFF);
    gc_send_command(0x2A,  4, point);

    point[0] = (y >> 8);
    point[1] = (y & 0xFF);
    point[2] = ((y + height) >> 8);
    point[3] = ((y + height) & 0xFF);
    gc_send_command(0x2B,  4, point);

    gc_send_command(0x2C, 0, NULL);

    // Push the pixelData through...
    // 3A -> 0x66
    point[2] = (color & 0x00003F) << 2;
    point[1] = (color & 0x003F00) >> 6;
    point[0] = (color & 0x3F0000) >> 14;
    point[0] ^= 0xFC;
    point[1] ^= 0xFC;
    point[2] ^= 0xFC;

    km_gpio_write(chip_select, 0);
    for (uint32_t count = 0; count < pixels; count++) {
        km_spi_send(spi_bus, point, 3, 100);
    }
    km_gpio_write (chip_select, 1);
}

static bool gc_adjust_coordinates(gc_handle_t *handle, int16_t* x, int16_t* y) {
  if ((*x >= 0) && (*x < handle->width) && (*y >= 0) && (*y < handle->height)) {
    switch (handle->rotation) {
      case 1:
        SWAP_INT16(*x, *y)
        *x = handle->device_width - *x - 1;
        break;
      case 2:
        *x = handle->device_width - *x - 1;
        *y = handle->device_height - *y - 1;
        break;
      case 3:
        SWAP_INT16(*x, *y)
        *y = handle->device_height - *y - 1;
        break;
    }
    return (true);
  }
  return (false);
}

/**
 * Graphic primitive functions for 16-bits color graphic buffer
 */

static void gc_prim_16bit_set_pixel(gc_handle_t *handle, int16_t x, int16_t y,
                             gc_color color) {
  if (gc_adjust_coordinates(handle, &x, &y) == true) {
    gc_fill_area(x,y,1,1,color);
  }
}

static void gc_prim_16bit_get_pixel(gc_handle_t *handle, int16_t x, int16_t y,
                             gc_color *color) {
  if ((x >= 0) && (x < handle->width) && (y >= 0) && (y < handle->height)) {
   uint32_t idx = ((y * handle->device_width) + x) * 2;
    *color = handle->buffer[idx] << 8 | handle->buffer[idx + 1];
  }
}

static void gc_prim_16bit_draw_vline(gc_handle_t *handle, int16_t x, int16_t y,
                              int16_t h, gc_color color) {

  if (gc_adjust_coordinates(handle, &x, &y) == true) {
    gc_fill_area(x,y,1,h,color);
  }
}

static void gc_prim_16bit_draw_hline(gc_handle_t *handle, int16_t x, int16_t y,
                              int16_t w, gc_color color) {
  if (gc_adjust_coordinates(handle, &x, &y) == true) {
    gc_fill_area(x,y,w,1,color);
  }
}

static void gc_prim_16bit_fill_rect(gc_handle_t *handle, int16_t x, int16_t y,
                             int16_t w, int16_t h, gc_color color) {

  if (gc_adjust_coordinates(handle, &x, &y) == true) {
    gc_fill_area(x,y,w,h,color);
  }
}

static void gc_prim_16bit_fill_screen(gc_handle_t *handle, gc_color color) {
  gc_fill_area(0,0,handle->device_width,handle->device_height,color);
}

uint16_t gc_prim_16bit_setup(gc_handle_t* handle, jerry_value_t options) {
  handle->set_pixel_cb = gc_prim_16bit_set_pixel;
  handle->get_pixel_cb = gc_prim_16bit_get_pixel;
  handle->draw_hline_cb = gc_prim_16bit_draw_hline;
  handle->draw_vline_cb = gc_prim_16bit_draw_vline;
  handle->fill_rect_cb = gc_prim_16bit_fill_rect;
  handle->fill_screen_cb = gc_prim_16bit_fill_screen;

  spi_bus        = (uint8_t) jerryxx_get_property_number(options, keyword_bus, 1);
  command_select = (uint8_t) jerryxx_get_property_number(options, keyword_cmd_select, 8);
  chip_select    = (uint8_t) jerryxx_get_property_number(options, keyword_chip_select, 9);
  uint8_t reset  = (uint8_t) jerryxx_get_property_number(options, keyword_reset, 0);

  km_gpio_set_io_mode(chip_select, KM_GPIO_IO_MODE_OUTPUT);
  km_gpio_set_io_mode(command_select, KM_GPIO_IO_MODE_OUTPUT);
  km_gpio_write (chip_select, 1);
  km_gpio_write (command_select, 1);

  if (reset != 0) {
    km_gpio_set_io_mode(reset, KM_GPIO_IO_MODE_OUTPUT);
    km_gpio_write (reset, 0);
    km_delay(10); // Sleep for 10 ms, so the dispay observes the reset..
    km_gpio_write (reset, 1);
    km_delay(5);
  }
  gc_initialize_display();

  // allocate buffer, no buffer or callback needed, we go to the hardware directly!
  return (0);
}

