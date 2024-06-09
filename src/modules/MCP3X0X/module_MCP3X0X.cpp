#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

#include "jerryscript.h"
#include "jerryxx.h"
#include "spi.h"
#include "gpio.h"
#include "system.h"

#include "module_MCP3X0X.h"
#include "magic_strings_MCP3X0X.h"

class SPIPort {
public:
    SPIPort(
        const uint8_t bus,
        const uint8_t ce,
        const uint8_t mode,
        const bool msb_order,
        const uint32_t speed,
        const uint8_t bitsPerWord,
        const uint8_t clk,
        const uint8_t mosi,
        const uint8_t miso)
        : _bus(bus)
        , _ce(ce) {

        km_gpio_set_io_mode(_ce, KM_GPIO_IO_MODE_OUTPUT);

        /*
        km_spi_mode_t converted;
        km_spi_pins_t pins;
        pins.miso = miso;
        pins.mosi = mosi;
        pins.sck  = clk;

        switch (mode) {
            case 0: converted = KM_SPI_MODE_0; break;
            case 1: converted = KM_SPI_MODE_1; break;
            case 2: converted = KM_SPI_MODE_2; break;
            case 3: converted = KM_SPI_MODE_3; break;
        }

        km_spi_setup(
            bus,
            converted,
            speed,
            (msb_order ? KM_SPI_BITORDER_MSB : KM_SPI_BITORDER_LSB),
            pins,
            false);
        */
    }
    ~SPIPort() {
        // km_spi_close(_bus);
    }

public:
    void Exchange (const uint8_t length, uint8_t buffer[]) {

        // Send out and receive the requested bytes...
        km_gpio_write(_ce, KM_GPIO_LOW);
        km_micro_delay(100);
        km_spi_sendrecv(_bus, buffer, buffer, length, 10000);
        km_gpio_write(_ce, KM_GPIO_HIGH);
    }

private:
    const uint8_t _bus;
    const uint8_t _ce;
};

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

    MCP3X0XType(SPIPort& port, const mode channel, const int16_t min=0, const int16_t max=Range)
        : _port(port)
        , _channel(channel)
        , _inverse(_min > _max)
        , _min(_inverse ? max : min)
        , _max(_inverse ? min : max) {
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
        result = (_inverse ? -result : result);

        value = ToRange(result);

        return (0);
    }

    static mode ToChannel(const uint8_t channel, bool diffrential) {
        return (static_cast<mode>((channel & 0x07) | (diffrential ? 0x8 : 0x0)));
    }

private:
    uint16_t Value(const uint8_t channel, const bool differential) const
    {
        uint16_t result = 0;

        if (channel < Channels) {

            uint8_t buffer[3];

            if (CHANNELBITS == 1) {
                    buffer[0] = 0x80 | (differential ? 0x00 : 0x40) | ((channel & 0x01) << 5) | 0x10;
            }
            else {
                    buffer[0] = 0x40 | (differential ? 0x00 : 0x20) | ((channel & 0x07) << 2);
            }
            buffer[1] = 0xFF;
            buffer[2] = 0xFF;

            _port.Exchange(3, buffer);

            if ((CHANNELBITS == 1) && (BITS == 10)) {
                    result = ((buffer[0] & 0x02)  << 8) | (buffer[1] & 0xFF);
            }
            else if (CHANNELBITS == 1) {
                    result = ((buffer[0] & 0x02) << 10) | (buffer[1] << 2) | ((buffer[2] & 0xC0) >> 6);
            }
            else {
                    result = (((buffer[1] & 0xFF) << 8) | buffer[2]) >> (16 - BITS);
            }
        }

        return (result);
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
    SPIPort& _port;
    mode     _channel;
    bool     _inverse;
    int16_t  _min;
    int16_t  _max;
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

static SPIPort channel(1, 14, 0, true, 1000000, 8, 10,11, 12);

/**
 * MCP3208() constructor
 */
JERRYXX_FUN(ctor_MCP3208_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "index");
  JERRYXX_CHECK_ARG_NUMBER(1, "channel");
  JERRYXX_CHECK_ARG_NUMBER(2, "differenial");

  // read parameters
  uint8_t index = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t line = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  bool diffrential = (bool)JERRYXX_GET_ARG_NUMBER(2);

  // set native handle
  MCP3208* object = new MCP3208(channel, MCP3208::ToChannel(line, diffrential));
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
  return jerry_create_number(value);
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
