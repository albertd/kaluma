#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

extern "C" {

#include "jerryscript.h"
#include "jerryxx.h"
#include "spi.h"
#include "gpio.h"
#include "system.h"

#include "module_MCP3X0X.h"
#include "magic_strings_MCP3X0X.h"

}


template <const uint8_t BITS, const uint8_t CHANNELBITS>
class MCP3X0XType 
{
public:
    enum mode : uint8_t
    {
        CHANNEL_0 = 0x0,
        CHANNEL_1 = 0x1,
        CHANNEL_2 = 0x2,
        CHANNEL_3 = 0x3,
        CHANNEL_4 = 0x4,
        CHANNEL_5 = 0x5,
        CHANNEL_6 = 0x6,
        CHANNEL_7 = 0x7,
        DIFFERENTIAL_0 = 0x8,
        DIFFERENTIAL_1 = 0x9,
        DIFFERENTIAL_2 = 0xA,
        DIFFERENTIAL_3 = 0xB,
        DIFFERENTIAL_4 = 0xC,
        DIFFERENTIAL_5 = 0xD,
        DIFFERENTIAL_6 = 0xE,
        DIFFERENTIAL_7 = 0xF
    };

    static constexpr uint16_t Range = (1 << BITS) - 1;
    static constexpr uint8_t Channels = 1 << CHANNELBITS;

public:
    MCP3X0XType() = delete;
    MCP3X0XType(MCP3X0XType<BITS,CHANNELBITS>&&) = delete;
    MCP3X0XType(const MCP3X0XType<BITS, CHANNELBITS>&) = delete;
    MCP3X0XType<BITS,CHANNELBITS>& operator=(MCP3X0XType<BITS,CHANNELBITS>&&) = delete;
    MCP3X0XType<BITS,CHANNELBITS>& operator=(const MCP3X0XType<BITS,CHANNELBITS>&) = delete;

    MCP3X0XType(const uint8_t bus, const uint8_t ce, const mode channel, const uint8_t average, const int16_t min=0, const int16_t max=Range)
        : _bus(bus)
        , _ce(ce)
        , _a1(~0)
        , _index(~0)
        , _channel(channel)
        , _average(average == 0 ? 1 : average)
        , _inverse(_min > _max)
        , _min(_inverse ? max : min)
        , _max(_inverse ? min : max) {
        km_gpio_set_io_mode(_ce, KM_GPIO_IO_MODE_OUTPUT);
        km_gpio_write(_ce, KM_GPIO_HIGH);
    }
    MCP3X0XType(const uint8_t bus, const uint8_t a0, const uint8_t a1, const uint8_t index, const mode channel, const uint8_t average, const int16_t min=0, const int16_t max=Range)
        : _bus(bus)
        , _ce(a0)
        , _a1(a1)
        , _index(index)
        , _channel(channel)
        , _average(average == 0 ? 1 : average)
        , _inverse(_min > _max)
        , _min(_inverse ? max : min)
        , _max(_inverse ? min : max) {
        km_gpio_set_io_mode(_ce, KM_GPIO_IO_MODE_OUTPUT);
        km_gpio_write(_ce, KM_GPIO_HIGH);
        km_gpio_set_io_mode(_a1, KM_GPIO_IO_MODE_OUTPUT);
        km_gpio_write(_a1, KM_GPIO_HIGH);
    }
 
    ~MCP3X0XType() = default;

public:
    uint8_t Channel() const {
        return (_channel & 0x07);
    }
    int16_t Get() const {
        int16_t value;
        if (Value(value) == 0) {
            return (value);
        }
        return (0);
    }
    // Value determination of this element
    uint16_t Value(int16_t& value) const {
        int16_t result = Value(_channel & 0x07, ((_channel & 0x08) != 0));
        value = result;
        // value = ToRange(_inverse ? -result : result);


        return (0);
    }

    static mode ToChannel(const uint8_t channel, bool diffrential) {
        return (static_cast<mode>((channel & 0x07) | (diffrential ? 0x8 : 0x0)));
    }

private:
    uint16_t Value(const uint8_t channel, const bool differential) const
    {
        uint32_t sum = 0;

        if (channel < Channels) {

            uint8_t request;
            uint8_t buffer[3];
            uint16_t results[_average];
            uint8_t index = 0;

            if (CHANNELBITS == 1) {
                request = 0x80 | (differential ? 0x00 : 0x40) | ((channel & 0x01) << 5) | 0x10;
            }
            else {
                request = 0x40 | (differential ? 0x00 : 0x20) | ((channel & 0x07) << 2);
            }

            // Send out and receive the requested bytes...
            if (_index == static_cast<uint8_t>(~0)) {
                km_gpio_write(_ce, KM_GPIO_LOW);
            }
            else {
                if ((_index & 0x01) == 0) { km_gpio_write(_ce, KM_GPIO_LOW); }
                if ((_index & 0x02) == 0) { km_gpio_write(_a1, KM_GPIO_LOW); }
            }
            km_micro_delay(100);

            while (index < _average) {

                buffer[0] = request;
                buffer[1] = 0xFF;
                buffer[2] = 0xFF;

                km_spi_sendrecv(_bus, buffer, buffer, sizeof(buffer), 10000);

                if ((CHANNELBITS == 1) && (BITS == 10)) {
                    results[index] = ((buffer[0] & 0x02)  << 8) | (buffer[1] & 0xFF);
                }
                else if (CHANNELBITS == 1) {
                    results[index] = ((buffer[0] & 0x02) << 10) | (buffer[1] << 2) | ((buffer[2] & 0xC0) >> 6);
                }
                else {
                    results[index] = (((buffer[1] & 0xFF) << 8) | buffer[2]) >> (16 - BITS);
                }
                sum += results[index];
                index++;
            }
            km_gpio_write(_ce, KM_GPIO_HIGH);
            if (_index == static_cast<uint8_t>(~0)) {
                km_gpio_write(_a1, KM_GPIO_HIGH);
            }
        }

        // Just calcuate the Average, for now, maybe later we make it more advanced :-)
        return (static_cast<uint16_t>(sum / _average));
    }
    inline int16_t ToRange(const int16_t value) const
    {
        int32_t result = value;

        // Adapt it to the requetsed value, if needed..
        if ((_max - _min) != Range) {
            result = ((((value * Range * 2) + (_max - _min)) / (2 * (_max - _min))) + _min);
        }

        return (result);
    }

 
private:
    const uint8_t _bus;
    const uint8_t _ce;
    const uint8_t _a1;
    const uint8_t _index;
    mode          _channel;
    uint8_t       _average;
    bool          _inverse;
    int16_t       _min;
    int16_t       _max;
};

using MCP3002 = MCP3X0XType<10, 1>;
using MCP3004 = MCP3X0XType<10, 2>;
using MCP3008 = MCP3X0XType<10, 3>;
using MCP3202 = MCP3X0XType<12, 1>;
using MCP3204 = MCP3X0XType<12, 2>;
using MCP3208 = MCP3X0XType<12, 3>;

static void handle_freecb(void *handle) { delete (MCP3208*) handle; }
static const jerry_object_native_info_t handle_info = {.free_cb = handle_freecb};

/* ************************************************************************** */
/*                              MCP3208 CLASS                             */
/* ************************************************************************** */

/**
 * MCP3208() constructor
 */
JERRYXX_FUN(ctor_MCP3208_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "bus");
  JERRYXX_CHECK_ARG_NUMBER(2, "channel");
  JERRYXX_CHECK_ARG_NUMBER(3, "differenial");
  JERRYXX_CHECK_ARG_NUMBER(4, "average");

  // read parameters
  uint8_t bus  = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t line = (uint8_t)JERRYXX_GET_ARG_NUMBER(2);
  bool diffrential = (bool)JERRYXX_GET_ARG_NUMBER(3);
  uint8_t average = 1;

  if (JERRYXX_HAS_ARG(4)) {
    average = (uint8_t)JERRYXX_GET_ARG_NUMBER(4);
  }

  // set native handle
  MCP3208* object = nullptr;

  jerry_value_t address = JERRYXX_GET_ARG(1);
  if (!jerry_value_is_object(address)) {
    JERRYXX_CHECK_ARG_NUMBER_OPT(1, "ce");
    uint8_t ce   = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);

    printf("We have the single argument constructor: ce: %d\n", ce);
    object = new MCP3208(bus, ce, MCP3208::ToChannel(line, diffrential), average);
  }
  else {
    uint8_t a0 = (uint8_t)jerryxx_get_property_number(address, "a0", ~0);
    uint8_t a1 = (uint8_t)jerryxx_get_property_number(address, "a1", ~0);
    uint8_t index = (uint8_t)jerryxx_get_property_number(address, "index", ~0);

    printf("We have the multi argument constructor: address: [%d,%d], index=%d\n", a0,a1, index);
    object = new MCP3208(bus, a0, a1, index, MCP3208::ToChannel(line, diffrential), average);
  }

  // set native handle
  jerry_set_object_native_pointer(this_val, object, &handle_info);

  return jerry_create_undefined();
}

/**
 * ADCChannel.prototype.getChannel()
 */
JERRYXX_FUN(get_channel_fn) {
  JERRYXX_GET_NATIVE_HANDLE(object, MCP3208, handle_info);
  return jerry_create_number(object->Channel());
}

/**
 * ADCChannel.prototype.getValue()
 */
JERRYXX_FUN(get_value_fn) {
  JERRYXX_GET_NATIVE_HANDLE(object, MCP3208, handle_info);
  int16_t value;
  object->Value(value);
  double converted = value;

  return jerry_create_number(converted / MCP3208::Range);
}

/**
 * Initialize MCP3X0X module
 */
jerry_value_t module_MCP3X0X_init() {
  /* ADCChannel class */
  jerry_value_t ctor = jerry_create_external_function(ctor_MCP3208_fn);
  jerry_value_t prototype = jerry_create_object();
  jerryxx_set_property(ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_MCP3X0X_GET_CHANNEL, get_channel_fn);
  jerryxx_set_property_function(prototype, MSTR_MCP3X0X_GET_VALUE,   get_value_fn);
  jerry_release_value(prototype);

  /* ADCChannel module exports */
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property(exports, MSTR_MCP3208_CONTEXT, ctor);
  jerry_release_value(ctor);

  return exports;
}
