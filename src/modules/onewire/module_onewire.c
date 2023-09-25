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
#include <system.h>
#include <string.h>

#include "err.h"
#include "jerryscript.h"
#include "jerryxx.h"
#include "onewire_magic_strings.h"
#include "module_onewire.h"

typedef struct onewire_temperature_d {
  uint8_t bus;
  uint16_t delay;
  onewire_address_t address;
  jerry_value_t callback;
  uint64_t converted;
} onewire_temperature_t;

onewire_temperature_t* active_temp_sensors[KM_MAX_TEMP_SENSOR_READS] = {};

static uint8_t onewire_address_from_string(const char* string, onewire_address_t* address) {
  uint8_t loaded = 0;
  if ((string[0] == '0') && (string[1] == 'x')) {
    uint8_t swap;
    loaded = km_string_to_bytes(&(string[2]), address->address, 7);
    if (loaded < 7) {
      memset(&(address->address[loaded]), 0, sizeof (address->address) - loaded);
    }
    else {
      swap = address->address[1]; address->address[1] = address->address[6]; address->address[1] = swap;
      swap = address->address[2]; address->address[2] = address->address[5]; address->address[2] = swap;
      swap = address->address[3]; address->address[3] = address->address[4]; address->address[3] = swap;
      address->address[7] = onewire_calculate_crc(7, address->address);
      loaded = 8;
    }
  }
  return (loaded);
}

static void onewire_address_to_string(const onewire_address_t* address, char* text) {
  static char hexArray[] = "0123456789ABCDEF";
  text[0] = '0';
  text[1] = 'x';
  text[2] = hexArray[address->address[0] >> 4];
  text[3] = hexArray[address->address[0] & 0x0F];
  text[4] = '-';
  text[5] = hexArray[address->address[6] >> 4];
  text[6] = hexArray[address->address[6] & 0x0F];
  text[7] = hexArray[address->address[5] >> 4];
  text[8] = hexArray[address->address[5] & 0x0F];
  text[9] = hexArray[address->address[4] >> 4];
  text[10] = hexArray[address->address[4] & 0x0F];
  text[11] = hexArray[address->address[3] >> 4];
  text[12] = hexArray[address->address[3] & 0x0F];
  text[13] = hexArray[address->address[2] >> 4];
  text[14] = hexArray[address->address[2] & 0x0F];
  text[15] = hexArray[address->address[1] >> 4];
  text[16] = hexArray[address->address[1] & 0x0F];
  text[17] = '\0';
}

uint16_t conversion_delay (const onewire_address_t* address, const uint8_t bits) {
  uint16_t delay = ~0;

  if ((address->address[0] == FAMILY_DS18B20) ||
      (address->address[0] == FAMILY_DS1822)  ||
      (address->address[0] == FAMILY_DS18S20)) {
    if (bits == 9) {
      delay = 94;        
    }
    else if (bits == 10) {
      delay = 188;
    }
    else if (bits == 11) {
      delay = 375;
    }
    else {
      delay = 750;
    }
  }
  else if (address->address[0] == FAMILY_MAX31826) {
    delay = 150;// 12bit conversion
  }
  return (delay);
}

static int request_temperature(const uint8_t busid, const onewire_address_t* address) {
  int result = onewire_write(busid, address, ConvertTempCommand, 0, NULL);
  
  if (result == 0) {
    if (onewire_parasite(busid, address)) {
      onewire_power(busid, true);
    }
  }
  return (result);
}

static bool set_config(const uint8_t busid, const onewire_address_t* address, const uint8_t resolution, const uint16_t alarm) {
  uint8_t bytes[3];
  bytes[0] = (alarm >> 8);
  bytes[1] = (alarm & 0xFF);
  bytes[2] = (resolution - 9) << 5;
  if (onewire_write(busid, address, WriteScratchPadCommand, sizeof(bytes), bytes) == 0) {
    return (true);
  }
  return (false);
}

static void temperature_time_out(const uint8_t index) {
  if ( (active_temp_sensors[index]->callback != JERRY_TYPE_NONE) && (active_temp_sensors[index]->converted < km_gettime()) ) {
    // This is an expired callback. Fire it..
    jerry_value_t callback = active_temp_sensors[index]->callback;
    active_temp_sensors[index]->callback = JERRY_TYPE_NONE;

    uint8_t bus = active_temp_sensors[index]->bus & 0x7F;
    const onewire_address_t* address = ((active_temp_sensors[index]->bus & 0x80) != 0 ? &(active_temp_sensors[index]->address) : NULL);
    active_temp_sensors[index] = NULL;
    float outcome = 0;
    uint8_t buffer[9];

    // If this was a parasite temp sensor, first turn of the bus power so we can control it again..
    onewire_power(bus, false);

    // Allright, now, in due time we get a callback to read the temperature..
    int result = onewire_read(bus, address, ReadScratchPadCommand, sizeof(buffer), buffer);
    
    if (result == 0) {
      if (onewire_calculate_crc(8, buffer) != buffer[8]) {
        result = ERR_BAD_CRC;
      }
      else {
        outcome = ((buffer[0] | (buffer[1] << 8)) + 8) / 16.0f;
      }
    }

    jerry_value_t this_val = jerry_create_undefined();
    jerry_value_t temperature = jerry_create_number(outcome);
    jerry_value_t errno = jerry_create_number(result);
    jerry_value_t args_p[2] = { temperature, errno};
    jerry_call_function(callback, this_val, args_p, (result == 0 ? 1 : 2));
    jerry_release_value(temperature);
    jerry_release_value(errno);
    jerry_release_value(this_val);
    jerry_release_value(callback);
  }
}

// ------------------------------------------------------------------------
// Javascript TemperatureSensor class definition 
// ------------------------------------------------------------------------

// ------------------------------------------------------------------------
// Destructor glue logic for JerryScript..
// ------------------------------------------------------------------------
static void temperature_destructor(void *handle) 
{ 
  free(handle); 
}

static const jerry_object_native_info_t temperature_handle_info = 
{
  .free_cb = temperature_destructor
};

// ------------------------------------------------------------------------
// JerryScript Javascript to C bridge code
// ------------------------------------------------------------------------
JERRYXX_FUN(temperature_ctor_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "bus");
  JERRYXX_CHECK_ARG_STRING_OPT(1, "address");
  JERRYXX_CHECK_ARG_NUMBER_OPT(2, "length");
  uint8_t bus = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  JERRYXX_GET_ARG_STRING_AS_CHAR(1, address);
  uint8_t bit_length = JERRYXX_GET_ARG_NUMBER_OPT(2, 0xFF);

  uint8_t buffer[9];
  uint8_t bits = 12;
  uint8_t loaded = 0;
  onewire_temperature_t* native = (onewire_temperature_t*) malloc(sizeof(onewire_temperature_t));
  const onewire_address_t* selected = NULL;

  if (address != NULL) {
    loaded = onewire_address_from_string(address, &(native->address));
  }

  if (loaded == 8) {
    native->bus = bus | 0x80;
    selected = &(native->address); 
  }
  else {
    if (loaded == 0) {
      native->address.address[0] = FAMILY_DS18B20;
    }
    native->bus = bus;
  }

  // Find the bits for the temperature:
  int result = onewire_read(bus, selected, ReadScratchPadCommand, sizeof(buffer), buffer);

  if ( (result == 0) && (onewire_calculate_crc(8, buffer) == buffer[8]) )  {
    bits = ((buffer[4] >> 5) & 0x03) + 9;
    if ((bit_length != 0xFF) && (bits != bit_length) && (set_config(bus, selected, bit_length, 0xFFFF))) {
      bits = bit_length;
    }
  }
  
  native->delay = conversion_delay(&(native->address), bits);
  native->callback = JERRY_TYPE_NONE;
  jerryxx_set_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BUS, bus);
  jerryxx_set_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BITS, bits);
  jerry_set_object_native_pointer(JERRYXX_GET_THIS, (void**) native, &temperature_handle_info);

  return ( jerry_create_undefined());
}

JERRYXX_FUN(temperature_read_fn) {
  JERRYXX_CHECK_ARG_FUNCTION_OPT(0, "callback");

  onewire_temperature_t* native;
  jerry_get_object_native_pointer(JERRYXX_GET_THIS, (void**) &native, &temperature_handle_info);
  int result = (native->callback == JERRY_TYPE_NONE ? 0 : ERR_INVALID_REQUEST);

  if (result != 0) {
      return (jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "Temperature reading already in progress."));
  }
  else {
    uint8_t index = 0;

    // See if we can allocate a slot..
    while ((index < (sizeof(active_temp_sensors) / sizeof(jerry_value_t))) && (active_temp_sensors[index] != NULL)) {
      ++index;
    }

    if (index >= (sizeof(active_temp_sensors) / sizeof(jerry_value_t))) {
      return (jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "No more slots for reading temperature available."));
    }
    else {
      active_temp_sensors[index] = native;

      result = request_temperature((native->bus & 0x7F), ((native->bus & 0x80) != 0 ? &(native->address) : NULL));

      if (result != 0) {
        return (jerry_create_error(JERRY_ERROR_COMMON, (const jerry_char_t *) "Temperature could not be read."));
      }
      else if ( (JERRYXX_HAS_ARG(0)) && (jerry_value_is_function(JERRYXX_GET_ARG(0))) ) {

        native->converted = km_gettime() + native->delay;
        native->callback = jerry_acquire_value(JERRYXX_GET_ARG(0));
      }
    }
  }

  return (jerry_create_undefined());
}

// ------------------------------------------------------------------------
// Javascript OneWireBus class definition 
// ------------------------------------------------------------------------

// ------------------------------------------------------------------------
// Destructor glue logic for JerryScript..
// ------------------------------------------------------------------------
static void onewirebus_destructor(void *handle) 
{ 
  uint8_t busid = (uint8_t) (intptr_t) handle;
  onewire_destroy(busid); 
}

static const jerry_object_native_info_t onewirebus_handle_info = 
{
  .free_cb = onewirebus_destructor
};

// ------------------------------------------------------------------------
// JerryScript Javascript to C bridge code
// ------------------------------------------------------------------------
JERRYXX_FUN(onewire_ctor_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "pin");
  uint8_t pin = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t bus = onewire_create(pin);
  if (bus < KM_MAX_ONEWIRE_BUS) {
    jerryxx_set_property_number(JERRYXX_GET_THIS, MSTR_ONEWIRE_BUS, bus);
    jerry_set_object_native_pointer(JERRYXX_GET_THIS, (void*) (intptr_t) bus, &onewirebus_handle_info);
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
    uint8_t devices;
    
    int outcome = onewire_scan(bus, &devices); 

    if (outcome != 0) {
      result = jerry_create_number(outcome);
    }
    else {
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
  for (uint8_t index = 0; index < (sizeof(active_temp_sensors) / sizeof(jerry_value_t)); index++) {
    active_temp_sensors[index] = NULL;
  } 

  /* OneWire class */
  jerry_value_t  onewire_ctor = jerry_create_external_function(onewire_ctor_fn);
  jerry_value_t prototype = jerry_create_object();
  jerryxx_set_property(onewire_ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_ONEWIRE_SCAN, onewire_scan_fn);
  jerryxx_set_property_function(prototype, MSTR_ONEWIRE_PARASITE, onewire_parasite_fn);
  jerry_release_value(prototype);

  /* Temperature class */
  prototype = jerry_create_object();
  jerry_value_t temperature_ctor = jerry_create_external_function(temperature_ctor_fn);
  jerryxx_set_property(temperature_ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_TEMPERATURE_READ, temperature_read_fn);
  jerry_release_value(prototype);

  /* onewire module exports */
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property(exports, MSTR_ONEWIRE_ONEWIRE, onewire_ctor);
  jerryxx_set_property(exports, MSTR_TEMPERATURE, temperature_ctor);
  jerry_release_value(onewire_ctor);
  jerry_release_value(temperature_ctor);

  return exports;
}

void module_onewire_process() {
  for (uint8_t index = 0; index < (sizeof(active_temp_sensors) / sizeof(jerry_value_t)); index++) {
    if (active_temp_sensors[index] != NULL) { 
      temperature_time_out(index);      
    }
  } 
}
