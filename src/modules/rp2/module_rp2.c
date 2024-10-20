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

#include <stdlib.h>

#include "board.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/pll.h"
#include "hardware/regs/io_bank0.h"
#include "io.h"
#include "jerryscript.h"
#include "jerryxx.h"
#include "pico/stdlib.h"
#include "rp2_magic_strings.h"

#define __PIO_INT_FULL 0xF00

static jerry_value_t __pio_call_back[NUM_PIOS];

static PIO __pio(uint8_t pio) {
  if (pio == 0) {
    return pio0;
  } else if (pio == 1) {
    return pio1;
  }
  return NULL;
}

void __pio_handler(uint8_t pio, uint8_t interrupt) {
  if (jerry_value_is_function(__pio_call_back[pio])) {
    jerry_value_t this_val = jerry_create_undefined();
    jerry_value_t args[1];
    args[0] = jerry_create_number(interrupt);
    jerry_value_t ret_val =
        jerry_call_function(__pio_call_back[pio], this_val, args, 1);
    if (jerry_value_is_error(ret_val)) {
      // print error
      jerryxx_print_error(ret_val, true);
    }
    jerry_release_value(ret_val);
    jerry_release_value(this_val);
  }
}

void __pio0_irq_0_handler(void) {
  PIO _pio = __pio(0);
  uint32_t ints = _pio->ints0;
  ints >>= 8;
  _pio->irq = ints;
  __pio_handler(0, ints);
}

void __pio1_irq_0_handler(void) {
  PIO _pio = __pio(1);
  uint32_t ints = _pio->ints0;
  ints >>= 8;
  _pio->irq = ints;
  __pio_handler(1, ints);
}

JERRYXX_FUN(pio_add_program_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG(1, "prog");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  jerry_value_t prog = JERRYXX_GET_ARG(1);
  pio_program_t pio_prog;
  if (jerry_value_is_typedarray(prog) &&
      jerry_get_typedarray_type(prog) ==
          JERRY_TYPEDARRAY_UINT16) { /* Uint16Array */
    jerry_length_t byteLength = 0;
    jerry_length_t byteOffset = 0;
    jerry_value_t array_buffer =
        jerry_get_typedarray_buffer(prog, &byteOffset, &byteLength);
    pio_prog.origin = -1;
    pio_prog.instructions =
        (uint16_t *)jerry_get_arraybuffer_pointer(array_buffer),
    pio_prog.length = jerry_get_arraybuffer_byte_length(array_buffer) / 2,
    jerry_release_value(array_buffer);
    PIO _pio = __pio(pio);
    int offset = pio_add_program(_pio, &pio_prog);
    return jerry_create_number(offset);
  } else {
    return jerry_create_error(
        JERRY_ERROR_TYPE,
        (const jerry_char_t *)"The prog argument must be Uint16Array");
  }
}

JERRYXX_FUN(pio_sm_init_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  JERRYXX_CHECK_ARG_OBJECT(2, "options");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  jerry_value_t options = JERRYXX_GET_ARG(2);
  pio_sm_config sm_config = pio_get_default_sm_config();
  PIO _pio = __pio(pio);
  uint32_t freq = (uint32_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_FREQ, SYS_CLK_HZ);
  float div = clock_get_hz(clk_sys) / freq;
  sm_config_set_clkdiv(&sm_config, div);
  // setup in pins
  int8_t in_base =
      (int8_t)jerryxx_get_property_number(options, MSTR_RP2_PIO_SM_IN_BASE, -1);
  if (in_base >= 0) {
    uint8_t in_count = (uint8_t)jerryxx_get_property_number(
        options, MSTR_RP2_PIO_SM_IN_COUNT, 1);
    sm_config_set_in_pins(&sm_config, in_base);
    pio_sm_set_consecutive_pindirs(_pio, sm, in_base, in_count, false);
    for (int i = 0; i < in_count; i++) {
      pio_gpio_init(_pio, in_base + i);
    }
  }
  // setup out pins
  int8_t out_base = (int8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_OUT_BASE, -1);
  if (out_base >= 0) {
    uint8_t out_count = (uint8_t)jerryxx_get_property_number(
        options, MSTR_RP2_PIO_SM_OUT_COUNT, 1);
    sm_config_set_out_pins(&sm_config, out_base, out_count);
    pio_sm_set_consecutive_pindirs(_pio, sm, out_base, out_count, true);
    for (int i = 0; i < out_count; i++) {
      pio_gpio_init(_pio, out_base + i);
    }
  }
  // setup set pins
  int8_t set_base = (int8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_SET_BASE, -1);
  if (set_base >= 0) {
    uint8_t set_count = (uint8_t)jerryxx_get_property_number(
        options, MSTR_RP2_PIO_SM_SET_COUNT, 1);
    sm_config_set_set_pins(&sm_config, set_base, set_count);
    pio_sm_set_consecutive_pindirs(_pio, sm, set_base, set_count, true);
    for (int i = 0; i < set_count; i++) {
      pio_gpio_init(_pio, set_base + i);
    }
  }
  // setup sideset pins
  bool sideset = (uint8_t)jerryxx_get_property_boolean(
      options, MSTR_RP2_PIO_SM_SIDESET, false);
  if (sideset) {
    int8_t sideset_base = (int8_t)jerryxx_get_property_number(
        options, MSTR_RP2_PIO_SM_SIDESET_BASE, -1);
    if (sideset_base >= 0) {
      uint8_t sideset_bits = (uint8_t)jerryxx_get_property_number(
          options, MSTR_RP2_PIO_SM_SIDESET_BITS, 1);
      bool sideset_opt = (uint8_t)jerryxx_get_property_boolean(
          options, MSTR_RP2_PIO_SM_SIDESET_OPT, false);
      bool sideset_pindirs = (uint8_t)jerryxx_get_property_boolean(
          options, MSTR_RP2_PIO_SM_SIDESET_PINDIRS, false);
      pio_sm_set_consecutive_pindirs(_pio, sm, sideset_base, sideset_bits,
                                     true);
      for (int i = 0; i < sideset_bits; i++) {
        pio_gpio_init(_pio, sideset_base + i);
      }
      sm_config_set_sideset_pins(&sm_config, sideset_base);
      if (sideset_opt) {
        sideset_bits++;  // Add 1 bit for option.
      }
      sm_config_set_sideset(&sm_config, sideset_bits, sideset_opt,
                            sideset_pindirs);
    }
  }
  // setup jmp pin
  int8_t jmp_pin =
      (int8_t)jerryxx_get_property_number(options, MSTR_RP2_PIO_SM_JMP_PIN, -1);
  if (jmp_pin >= 0) {
    sm_config_set_jmp_pin(&sm_config, jmp_pin);
  }
  // setup wrap
  uint8_t wrap_target = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_WRAP_TARGET, 0);
  uint8_t wrap =
      (uint8_t)jerryxx_get_property_number(options, MSTR_RP2_PIO_SM_WRAP, 31);
  int offset =
      (int)jerryxx_get_property_number(options, MSTR_RP2_PIO_SM_OFFSET, 0);
  sm_config_set_wrap(&sm_config, offset + wrap_target, offset + wrap);
  // setup in-shift
  uint8_t inshift_dir = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_INSHIFT_DIR, 1);
  bool autopush = (uint8_t)jerryxx_get_property_boolean(
      options, MSTR_RP2_PIO_SM_AUTOPUSH, 0);
  uint8_t push_threshold = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_PUSH_THRESHOLD, 32);
  sm_config_set_in_shift(&sm_config, inshift_dir, autopush, push_threshold);
  // setup out-shift
  uint8_t outshift_dir = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_OUTSHIFT_DIR, 1);
  bool autopull = (uint8_t)jerryxx_get_property_boolean(
      options, MSTR_RP2_PIO_SM_AUTOPULL, 0);
  uint8_t pull_threshold = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_PULL_THRESHOLD, 32);
  sm_config_set_out_shift(&sm_config, outshift_dir, autopull, pull_threshold);
  // setup fifoJoin
  uint8_t fifo_join = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_FIFO_JOIN, 0);
  sm_config_set_fifo_join(&sm_config, fifo_join);
  // setup out special
  bool out_sticky = (uint8_t)jerryxx_get_property_boolean(
      options, MSTR_RP2_PIO_SM_OUT_STICKY, false);
  int8_t out_enable_pin = (int8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_OUT_ENABLE_PIN, -1);
  sm_config_set_out_special(&sm_config, out_sticky, (out_enable_pin > -1),
                            (out_enable_pin > -1) ? out_enable_pin : 0);
  // setup mov status
  uint8_t move_status_sel = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_MOV_STATUS_SEL, 0);
  uint8_t move_status_n = (uint8_t)jerryxx_get_property_number(
      options, MSTR_RP2_PIO_SM_MOV_STATUS_N, 0);
  sm_config_set_mov_status(&sm_config, move_status_sel, move_status_n);

  pio_sm_init(_pio, sm, offset, &sm_config);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_set_enabled_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  JERRYXX_CHECK_ARG_BOOLEAN(2, "enabled");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  bool enabled = JERRYXX_GET_ARG_BOOLEAN(2);
  PIO _pio = __pio(pio);
  pio_sm_set_enabled(_pio, sm, enabled);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_restart_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  pio_sm_restart(_pio, sm);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_exec_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  JERRYXX_CHECK_ARG_NUMBER(2, "inst");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  uint16_t inst = (uint16_t)JERRYXX_GET_ARG_NUMBER(2);
  PIO _pio = __pio(pio);
  pio_sm_exec(_pio, sm, inst);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_put_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  JERRYXX_CHECK_ARG(2, "data");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  jerry_value_t data = JERRYXX_GET_ARG(2);
  PIO _pio = __pio(pio);
  if (jerry_value_is_typedarray(data) &&
      jerry_get_typedarray_type(data) ==
          JERRY_TYPEDARRAY_UINT32) { /* Uint32Array */
    jerry_length_t byteLength = 0;
    jerry_length_t byteOffset = 0;
    jerry_value_t array_buffer =
        jerry_get_typedarray_buffer(data, &byteOffset, &byteLength);
    size_t len = jerry_get_arraybuffer_byte_length(array_buffer) / 4;
    uint8_t *data_buf = jerry_get_arraybuffer_pointer(array_buffer);
    jerry_release_value(array_buffer);
    for (int i = 0; i < len; i++) {
      pio_sm_put_blocking(_pio, sm, *((uint32_t *)data_buf + i));
    }
  } else if (jerry_value_is_number(data)) {
    uint32_t data_value = jerry_get_number_value(data);
    pio_sm_put_blocking(_pio, sm, data_value);
  } else {
    return jerry_create_error(
        JERRY_ERROR_TYPE,
        (const jerry_char_t
             *)"The data argument must be number of Uint32Array");
  }
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_get_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  uint32_t data = pio_sm_get_blocking(_pio, sm);
  return jerry_create_number(data);
}

JERRYXX_FUN(pio_sm_set_pins_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  JERRYXX_CHECK_ARG_NUMBER(2, "value");
  JERRYXX_CHECK_ARG_NUMBER(3, "mask");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  uint32_t value = (uint32_t)JERRYXX_GET_ARG_NUMBER(2);
  uint32_t mask = (uint32_t)JERRYXX_GET_ARG_NUMBER(3);
  PIO _pio = __pio(pio);
  pio_sm_set_pins_with_mask(_pio, sm, value, mask);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_rxfifo_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  uint length = pio_sm_get_rx_fifo_level(_pio, sm);
  return jerry_create_number(length);
}

JERRYXX_FUN(pio_sm_txfifo_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  uint length = pio_sm_get_tx_fifo_level(_pio, sm);
  return jerry_create_number(length);
}

JERRYXX_FUN(pio_sm_clear_fifos_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  pio_sm_clear_fifos(_pio, sm);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_drain_txfifo_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_NUMBER(1, "sm");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t sm = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  PIO _pio = __pio(pio);
  pio_sm_drain_tx_fifo(_pio, sm);
  return jerry_create_undefined();
}

JERRYXX_FUN(pio_sm_irq_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pio");
  JERRYXX_CHECK_ARG_FUNCTION(1, "callback");
  uint8_t pio = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  jerry_value_t callback = JERRYXX_GET_ARG(1);
  PIO _pio = __pio(pio);
  if (pio == 0) {
    _pio->inte0 = __PIO_INT_FULL;
    irq_set_mask_enabled((1u << PIO0_IRQ_0), true);
    irq_set_enabled(PIO0_IRQ_0, true);
  } else {
    _pio->inte0 = __PIO_INT_FULL;
    irq_set_mask_enabled((1u << PIO1_IRQ_0), true);
    irq_set_enabled(PIO1_IRQ_0, true);
  }
  __pio_call_back[pio] = jerry_acquire_value(callback);

  return jerry_create_undefined();
}

/**
 * Initialize 'rp2' module and return exports
 */
jerry_value_t module_rp2_init() {
  irq_set_exclusive_handler(PIO0_IRQ_0, __pio0_irq_0_handler);
  irq_set_exclusive_handler(PIO1_IRQ_0, __pio1_irq_0_handler);
  for (int i = 0; i < NUM_PIOS; i++) {
    __pio_call_back[i] = jerry_create_undefined();
  }

  // pio module exports
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_ADD_PROGRAM,
                                pio_add_program_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_INIT, pio_sm_init_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_SET_ENABLED,
                                pio_sm_set_enabled_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_RESTART,
                                pio_sm_restart_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_EXEC, pio_sm_exec_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_PUT, pio_sm_put_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_GET, pio_sm_get_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_SET_PINS,
                                pio_sm_set_pins_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_RXFIFO,
                                pio_sm_rxfifo_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_TXFIFO,
                                pio_sm_txfifo_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_CLEAR_FIFOS,
                                pio_sm_clear_fifos_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_DRAIN_TXFIFO,
                                pio_sm_drain_txfifo_fn);
  jerryxx_set_property_function(exports, MSTR_RP2_PIO_SM_IRQ, pio_sm_irq_fn);
  return exports;
}
