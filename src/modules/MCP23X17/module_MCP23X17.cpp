#include <stdio.h>
#include <stdlib.h>
#include <algorithm>
#include <cstring>
#include <port/spi.h>

extern "C" {

#include "jerryscript.h"
#include "jerryxx.h"
#include "spi.h"
#include "gpio.h"
#include "system.h"

#include "module_MCP23X17.h"
#include "magic_strings_MCP23X17.h"

}

class MCP23X17
{
public:
    enum pin_mode : uint8_t {
        INPUT,
        OUTPUT,
        PWM_TONE,
        PWM,
        CLOCK
    };

    enum pull_mode : uint8_t {
        OFF      = 0x00,
        DOWN     = 0x01,
        UP       = 0x02,
        NEGATIVE = 0x08 
    };

    enum trigger_mode : uint8_t {
        NONE     = 0x00,
        FALLING  = 0x01,
        RISING   = 0x02,
        BOTH     = 0x03,
        HIGH     = 0x04,
        LOW      = 0x08
    };

private:
    class Device {
    public:
        enum registers : uint8_t {
            IODIRA   = 0x00,  // I/O direction A
            IODIRB   = 0x01,  // I/O direction B
            IPOLA    = 0x02,  // I/O polarity A
            IPOLB    = 0x03,  // I/O polarity B
            GPINTENA = 0x04,  // interupt enable A
            GPINTENB = 0x05,  // interupt enable B
            DEFVALA  = 0x06,  // register default value A (interupts)
            DEFVALB  = 0x07,  // register default value B (interupts)
            INTCONA  = 0x08,  // interupt control A
            INTCONB  = 0x09,  // interupt control B
            IOCON    = 0x0A,  // I/O config (also 0x0B)
            GPPUA    = 0x0C,  // port A pullups
            GPPUB    = 0x0D,  // port B pullups
            INTFA    = 0x0E,  // interupt flag A (where the interupt came from)
            INTFB    = 0x0F,  // interupt flag B
            INTCAPA  = 0x10,  // interupt capture A (value at interupt is saved here)
            INTCAPB  = 0x11,  // interupt capture B
            GPIOA    = 0x12,  // port A
            GPIOB    = 0x13,  // port B
            OLATA    = 0x14,  // output latch A
            OLATB    = 0x15   // output latch B
        };
    
        enum port : uint8_t {
            PORTA,
            PORTB
        };
    
    public:
        Device() = delete;
        Device(Device&&) = delete;
        Device(const Device&) = delete;
        Device& operator= (Device&&) = delete;
        Device& operator= (const Device&) = delete;
    
        /*
         * The MCP23S17 is a slave SPI device. The slave address contains four
         * fixed bits (0b0100) and three user-defined hardware address bits
         * (if enabled via IOCON.HAEN; pins A2, A1 and A0) with the
         * read/write command bit filling out the rest of the control byte::
         *
         *     +--------------------+
         *     |0|1|0|0|A2|A1|A0|R/W|
         *     +--------------------+
         *     |fixed  |hw_addr |R/W|
         *     +--------------------+
         *     |7|6|5|4|3 |2 |1 | 0 |
         *     +--------------------+
         *
         */
        Device(const uint8_t bus, const uint8_t ce, const uint8_t address)
            : _bus(bus)
            , _ce(ce)
            , _address(0x40 | ((address & 0x07) << 1)) {
	    km_gpio_set_io_mode(_ce, KM_GPIO_IO_MODE_OUTPUT);
        }
        ~Device() = default;
    
    public:
        uint8_t Dump(enum registers index) {
            return (ReadRegister(index));
        }
        inline void Reset() {
           WriteRegister(IODIRA,   0xA5);
           WriteRegister(IODIRB,   0x5A);
           WriteRegister(GPINTENA, 0x00);
           WriteRegister(GPINTENB, 0x00);
           WriteRegister(IPOLA,    0x00);
           WriteRegister(IPOLB,    0x00);
           WriteRegister(GPPUA,    0x00);
           WriteRegister(GPPUB,    0x00);
           WriteRegister(OLATA,    0x00);
           WriteRegister(OLATB,    0x00);
        }
        inline void Interrupt(const bool single, const bool opendrain, const bool activeLow) {
            uint8_t result = (ReadRegister(IOCON) & ~(0x46));
            result |= (single ? 0x40 : 0x00) | (opendrain ? 0x04 : 0x00) | (activeLow ? 0x00 : 0x02);
            WriteRegister(IOCON, result);
        }
        inline bool Pin(const uint8_t pin) const {
            return ((Get(pin < 8 ? PORTA : PORTB) & (1 << (pin < 8 ? pin : pin - 8))) != 0);
        }
        inline void Pin(const uint8_t pin, const bool value) {
            uint8_t current (Get(pin < 8 ? PORTA : PORTB));
            uint8_t mask = (1 << (pin < 8 ? pin : pin - 8));
            if (value == true) {
                if ((current & mask) == 0) {
                    Set ( static_cast<port>(pin < 8 ? PORTA : PORTB), static_cast<uint8_t>(current | mask));
                }
            }
            else if ((current & mask) != 0) {
                Set ( static_cast<port>(pin < 8 ? PORTA : PORTB), static_cast<uint8_t>(current & (~mask)));
            }
        }
        inline uint8_t Get(const port which) const {
            return (ReadRegister(which == PORTA ? GPIOA : GPIOB));
        }
        inline void Set(const port which, const uint8_t value) {
            WriteRegister(which == PORTA ? GPIOA : GPIOB, value);
        }
        inline void Set (const uint16_t value) {
            Set (PORTA, (value & 0xFF));
            Set (PORTB, ((value >> 8) & 0xFF));
        }
        void Mode(const uint8_t pin, const bool input, const trigger_mode trigger, const uint8_t character) {
            const uint8_t offset  = (pin < 8 ? 0: 1);
            const uint8_t mask = (1 << (pin < 8 ? pin : pin - 8));
    
            // Read current states of register..
            uint8_t pol  = ReadRegister(IPOLA + offset);
            uint8_t dir  = ReadRegister(IODIRA + offset);
    
            // Depending on in, or out, change the plan of attach..
            if (input == true) {
                WriteRegister(IODIRA + offset, dir | mask);
                Interrupt(trigger, mask, offset);
    
                uint8_t pull = ReadRegister(GPPUA + offset);
                if (character & pull_mode::UP) {
                    WriteRegister(GPPUA + offset, pull | mask);
                }
                else {
                    WriteRegister(GPPUA + offset, pull & (~mask));
                }
                if (character & pull_mode::NEGATIVE) {
                    WriteRegister(IPOLA + offset, pol | mask);
                }
                else {
                    WriteRegister(IPOLA + offset, pol & (~mask));
                }
            }
            else {
                if (character & pull_mode::NEGATIVE) {
                    WriteRegister(IPOLA + offset, pol | mask);
                }
                else {
                    WriteRegister(IPOLA + offset, pol & (~mask));
                }
                WriteRegister(IODIRA + offset, dir & (~mask));
                Interrupt(trigger, mask, offset);
            }
        }
        uint8_t ReadRegister(const uint8_t reg) const {
            uint8_t buffer[3] = { static_cast<uint8_t>(_address | 0x01), reg, 0xFF };

            // Send out and receive the requested bytes...
            km_gpio_write(_ce, KM_GPIO_LOW);
            km_micro_delay(200);
            km_spi_sendrecv(_bus, buffer, buffer, sizeof(buffer), 10000);
            km_gpio_write(_ce, KM_GPIO_HIGH);
 
            return (buffer[2]);
        }
        void WriteRegister(const uint8_t reg, const uint8_t value) {
            uint8_t buffer[] = { _address, reg, value};

            // Send out and receive the requested bytes...
            km_gpio_write(_ce, KM_GPIO_LOW);
            km_micro_delay(200);
            km_spi_sendrecv(_bus, buffer, buffer, sizeof(buffer), 10000);
            km_gpio_write(_ce, KM_GPIO_HIGH);
        }
    
    private:
        bool Interrupt(const trigger_mode mode, const uint8_t mask, const uint8_t offset) {
            bool result = true;
    
            uint8_t ena  = ReadRegister(GPINTENA + offset);
    
            switch (mode) {
            case HIGH:
            case LOW:
                 result = false;
                 break;
            case NONE:
                 WriteRegister(GPINTENA + offset, (ena & (~mask)));
                 break;
            case FALLING: {
                 WriteRegister(GPINTENA + offset, (ena | mask));
                 uint8_t def = ReadRegister(DEFVALA + offset);
                 uint8_t con = ReadRegister(INTCONA + offset);
                 WriteRegister(DEFVALA + offset, (def | mask));
                 WriteRegister(INTCONA + offset, (con | mask));
                 break;
                 }
            case RISING: {
                 WriteRegister(GPINTENA + offset, (ena | mask));
                 uint8_t def = ReadRegister(DEFVALA + offset);
                 uint8_t con = ReadRegister(INTCONA + offset);
                 WriteRegister(DEFVALA + offset, (def & (~mask)));
                 WriteRegister(INTCONA + offset, (con | mask));
                 break;
                 }
            case BOTH: {
                 WriteRegister(GPINTENA + offset, (ena | mask));
                 uint8_t con = ReadRegister(INTCONA + offset);
                 WriteRegister(INTCONA + offset, (con & (~mask)));
                 break;
                 }
            }
    
            return (result);
        }
   
    private:
        const uint8_t _bus;
        const uint8_t _ce;
        const uint8_t _address;
    };
    
public:
    MCP23X17() = delete;
    MCP23X17(MCP23X17&&) = delete;
    MCP23X17(const MCP23X17&) = delete;
    MCP23X17& operator=(MCP23X17&&) = delete;
    MCP23X17& operator=(const MCP23X17&) = delete;

    // bit -> Lowest Nible is the bit 0 => Port A bit 0, 8 => Port B Bit 0, highest nible is the number of bit to occupy (-1).
    MCP23X17(const uint8_t bus,const uint8_t ce, const uint8_t address, const uint8_t bit, trigger_mode trigger, const uint8_t character, const bool output)
        : _device(bus, ce, address)
        , _bits(bit)
        , _max((1 << (((bit >> 4) & 0xF) + 1)) - 1) {

        // Set the mode of the associated pins...
        uint8_t pin = (bit & 0x0F);
        uint16_t mask = _max;

        while (mask > 0) {
            _device.Mode(pin, !output, trigger, character);
            pin++;
            mask = (mask >> 1);
        }
    }
    ~MCP23X17() = default;

public:
    void Reset() {
            _device.Reset();
            _device.Interrupt(false, true, true);
    }
    void Dump() {
        printf("\n===================");
        printf("\nIODIRA:   %02X", _device.Dump(Device::IODIRA));
        printf("\nIODIRB:   %02X", _device.Dump(Device::IODIRB));
        printf("\nGPINTENA: %02X", _device.Dump(Device::GPINTENA));
        printf("\nGPINTENB: %02X", _device.Dump(Device::GPINTENB));
        printf("\nINTCONA:  %02X", _device.Dump(Device::INTCONA));
        printf("\nINTCONB:  %02X", _device.Dump(Device::INTCONB));
        printf("\nIOCON:    %02X", _device.Dump(Device::IOCON));
        printf("\nGPPUA:    %02X", _device.Dump(Device::GPPUA));
        printf("\nGPPUB:    %02X", _device.Dump(Device::GPPUB));
        printf("\nOLATA:    %02X", _device.Dump(Device::OLATA));
        printf("\nOLATB:    %02X", _device.Dump(Device::OLATB));
        printf("\nGPIOA:    %02X", _device.Dump(Device::GPIOA));
        printf("\nGPIOB:    %02X", _device.Dump(Device::GPIOB));
        printf("\nINTFA:    %02X", _device.Dump(Device::INTFA));
        printf("\nINTFB:    %02X", _device.Dump(Device::INTFB));
        printf("\n===================");
    }
    bool Get() const {
        bool result = false;
        uint16_t value;

        if (Read(value) == 0) {
            result = (value != 0);
        }
        return (result);
    }
    void Set(const bool value) {
        Write(value ? 1 : 0);
    }
    uint16_t Read(uint16_t& value) const {
        uint8_t offset = (_bits & 0x7);
        uint8_t bits = (((_bits >> 4) & 0x0F) + 1);

        uint16_t part = _device.Get(((_bits & 0x08) == 0) ? Device::port::PORTA : Device::port::PORTB);

        if (bits <= (8 - offset)) {
            part = (part >> offset);
        }
        else {
            uint8_t part2 = _device.Get(Device::port::PORTB);

            part = (part >> offset);

            // We also need to read the second part..
            part = (part & ((1 << (8 - offset)) - 1)) | (part2 << (8 - offset));
        }

        value = (part & _max);

        return (0);
    }
    uint16_t Write(const uint16_t value) {
        uint8_t offset = (_bits & 0x7);
        uint8_t mask = static_cast<uint8_t>((_max << offset) & 0xFF);


        uint16_t newValue = (value & _max);
        newValue = newValue << offset;

        if ((_bits & 0x08) == 0)
        {
            // We start at PORTA
            uint8_t part = _device.Get(Device::port::PORTA);
            part = (part & (~mask)) | (newValue & mask);

            _device.Set(Device::port::PORTA, part);
            mask = static_cast<uint8_t>((_max >> (8 - offset)) & 0xFF);
            newValue = newValue >> 8;
            offset = 0;
        }

        if (((_bits & 0x08) != 0) || (mask != 0))
        {
            // Let see what we need to push to PortB
            uint8_t part = (_device.Get(Device::port::PORTB) & 0xFF);
            part = (part & (~mask)) | (newValue & mask);

            _device.Set(Device::port::PORTB, part);
        }

        return (0);
    }
    uint8_t ReadRegister(const uint8_t reg) const {
        return (_device.ReadRegister(reg));
    }
    void WriteRegister(const uint8_t reg,const uint8_t value) {
        _device.WriteRegister(reg, value);
    }

private:
    Device _device;
    uint16_t _max;
    const uint8_t _bits;
};

static void handle_freecb(void *handle) { free(handle); }
static const jerry_object_native_info_t handle_info = {.free_cb = handle_freecb};

/* ************************************************************************** */
/*                               MCP23X17 CLASS                               */
/* ************************************************************************** */

/**
 * MCP23X17() constructor
 */
JERRYXX_FUN(ctor_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "bus");
  JERRYXX_CHECK_ARG_NUMBER(1, "ce");
  JERRYXX_CHECK_ARG_NUMBER(2, "address");
  JERRYXX_CHECK_ARG_NUMBER(3, "base");
  JERRYXX_CHECK_ARG_NUMBER(4, "bits");
  JERRYXX_CHECK_ARG_STRING(5, "type");

  // read parameters
  uint8_t bus     = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t ce      = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  uint8_t address = (uint8_t)JERRYXX_GET_ARG_NUMBER(2);
  uint8_t base    = (uint8_t)JERRYXX_GET_ARG_NUMBER(3);
  uint8_t bits    = (uint8_t)JERRYXX_GET_ARG_NUMBER(4);
  JERRYXX_GET_ARG_STRING_AS_CHAR(5, text);

  if ((base < 16) && (bits <= (16 - base))) {
      base = (base | ((bits-1) << 4));
      MCP23X17::trigger_mode trigger;
      bool output;
 
      if (strcmp("INPUT", text) == 0) {
          output = false;
          trigger = MCP23X17::trigger_mode::NONE;
      }
      else if (strcmp("OUTPUT", text) == 0) {
          output = true;
          trigger = MCP23X17::trigger_mode::NONE;
      }
      else if (strcmp("FALLING", text) == 0) {
          output = false;
          trigger = MCP23X17::trigger_mode::FALLING;
      }
      else if (strcmp("RISING", text) == 0) {
          output = false;
          trigger = MCP23X17::trigger_mode::RISING;
      }
      else if (strcmp("BOTH", text) == 0) {
          output = false;
          trigger = MCP23X17::trigger_mode::BOTH;
      }
      else {
          output = false;
          trigger = MCP23X17::trigger_mode::NONE;
      }

      // set native handle
      MCP23X17* object = new MCP23X17(bus, ce, address, base, trigger, 0, output);
      jerry_set_object_native_pointer(this_val, object, &handle_info);
  }

  return jerry_create_undefined();
}

/**
 * MCP23X17.prototype.dump()
 */
JERRYXX_FUN(dump_fn) {
  JERRYXX_GET_NATIVE_HANDLE(object, MCP23X17, handle_info);
  object->Dump();
  return jerry_create_undefined();
}

/**
 * MCP23X17.prototype.readRegister()
 */
JERRYXX_FUN(get_register_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "register");
  JERRYXX_GET_NATIVE_HANDLE(object, MCP23X17, handle_info);
  uint8_t reg = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  return jerry_create_number(object->ReadRegister(reg));
}

/**
 * MCP23X17.prototype.writeRegister()
 */
JERRYXX_FUN(set_register_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "register");
  JERRYXX_CHECK_ARG_NUMBER(1, "value");
  JERRYXX_GET_NATIVE_HANDLE(object, MCP23X17, handle_info);
  uint8_t reg   = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t value = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  object->WriteRegister(reg, value);
  return jerry_create_undefined();
}

/**
 * MCP23X17.prototype.setValue()
 */
JERRYXX_FUN(set_value_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "value");
  JERRYXX_GET_NATIVE_HANDLE(object, MCP23X17, handle_info);
  uint16_t value = (uint16_t)JERRYXX_GET_ARG_NUMBER(0);
  printf("Got value: [%d]\n", value);
  return jerry_create_number(object->Write(value));
}

/**
 * MCP23X17.prototype.getValue()
 */
JERRYXX_FUN(get_value_fn) {
  JERRYXX_GET_NATIVE_HANDLE(object, MCP23X17, handle_info);
  uint16_t value;
  object->Read(value);
  return jerry_create_number(value);
}

/**
 * Initialize MCP23X17 module
 */
jerry_value_t module_MCP23X17_init() {
  /* ADCChannel class */
  jerry_value_t ctor = jerry_create_external_function(ctor_fn);
  jerry_value_t prototype = jerry_create_object();
  jerryxx_set_property(ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_MCP23X17_DUMP,         dump_fn);
  jerryxx_set_property_function(prototype, MSTR_MCP23X17_SET_VALUE,    set_value_fn);
  jerryxx_set_property_function(prototype, MSTR_MCP23X17_GET_VALUE,    get_value_fn);
  jerryxx_set_property_function(prototype, MSTR_MCP23X17_SET_REGISTER, set_register_fn);
  jerryxx_set_property_function(prototype, MSTR_MCP23X17_GET_REGISTER, get_register_fn);

  jerry_release_value(prototype);

  /* ADCChannel module exports */
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property(exports, MSTR_MCP23X17_CONTEXT, ctor);
  jerry_release_value(ctor);

  return exports;
}
