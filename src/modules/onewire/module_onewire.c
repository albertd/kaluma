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
#include <port/onewire.h>
#include <utils.h>

#include "err.h"
#include "jerryscript.h"
#include "jerryxx.h"
#include "onewire_magic_strings.h"

static bool onewire_address_from_string(const char* string, onewire_address_t* address) {
  if ((string[0] == '0') && (string[1] == 'x')) {
    km_string_to_bytes(&(string[2]), address->address, 8);
    return (true);
  }
  return (false);
}

static void onewire_address_to_string(const onewire_address_t* address, char* text) {
  text[0] = '0';
  text[1] = 'x';
  km_bytes_to_string(address->address, 8, &(text[2]));
  text[2 + 3] = '-'; 
  text[2 + (8 * 3)] = '\0'; 
}

JERRYXX_FUN(onewire_ctor_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pin");
  uint8_t pin = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t bus = onewire_create(pin);
  if (bus < KM_MAX_ONEWIRE_BUS) {
    jerryxx_set_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BUS, bus);
    return jerry_create_undefined();
  } else {
    return jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "No more OneWire busses can be created.");
  }
}

JERRYXX_FUN(onewire_scan_fn) {
  jerry_value_t result;
  uint8_t bus = jerryxx_get_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BUS, KM_MAX_ONEWIRE_BUS);
  if (bus >= KM_MAX_ONEWIRE_BUS) {
    result = jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "Invalid OneWire bus.");
  }
  else {
    uint8_t devices = onewire_scan(bus); 

    result = jerry_create_array(devices);

    for (uint8_t index = 0; index < devices; index++) {
      const onewire_address_t* address = onewire_link(bus, index);
      if (address != NULL) {
        char text[28];
        onewire_address_to_string(address, text);
        jerry_value_t text_address = jerry_create_string((const jerry_char_t *)text);
        jerry_value_t ret = jerry_set_property_by_index(result, index, text_address);
        jerry_release_value(ret);
        jerry_release_value(text_address);
      }
    }
  }
  return (result);
}

JERRYXX_FUN(onewire_parasite_fn) {
  jerry_value_t result;
  uint8_t bus = jerryxx_get_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BUS, KM_MAX_ONEWIRE_BUS);
  if (bus >= KM_MAX_ONEWIRE_BUS) {
    result = jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "Invalid OneWire bus.");
  }
  else {
    JERRYXX_GET_ARG_STRING_AS_CHAR(1, address)
    onewire_address_t raw_address;
    onewire_address_t* selected = NULL;

    if ((address != NULL) && (onewire_address_from_string(address, &raw_address))) {
      selected = &raw_address;
    }

    result = jerry_create_boolean(onewire_parasite(bus, selected));
  }
  return (result);
}

jerry_value_t module_onewire_init() {
  /* OneWire class */
  jerry_value_t onewire_ctor = jerry_create_external_function(onewire_ctor_fn);
  jerry_value_t prototype = jerry_create_object();
  jerryxx_set_property(onewire_ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_ONEWIRE_SCAN, onewire_scan_fn);
  jerryxx_set_property_function(prototype, MSTR_ONEWIRE_PARASITE, onewire_parasite_fn);
  jerry_release_value(prototype);

  /* pwm module exports */
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property(exports, MSTR_ONEWIRE_ONEWIRE, onewire_ctor);
  jerry_release_value(onewire_ctor);

  return exports;
}
