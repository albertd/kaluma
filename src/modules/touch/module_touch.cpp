#include <stdio.h>
#include <stdlib.h>
#include <algorithm>

extern "C" {

#include "jerryscript.h"
#include "jerryxx.h"
#include "spi.h"
#include "gpio.h"
#include "system.h"

#include "module_touch.h"
#include "magic_strings_touch.h"

}

class Touch {
public:
    Touch() = delete;
    Touch(Touch&&) = delete;
    Touch(const Touch&) = delete;
    Touch& operator=(Touch&&) = delete;
    Touch& operator=(const Touch&) = delete;

    Touch(uint8_t bus, const uint8_t ce, const uint8_t irq, const uint16_t inhibition, jerry_value_t report)
        : _bus(bus)
        , _ce(ce)
        , _irq(irq)
        , _delay(inhibition)
        , _lastAquire(0)
        , _pressed(false)
        , _report(report) {
        km_gpio_set_io_mode(_ce, KM_GPIO_IO_MODE_OUTPUT);
        km_gpio_set_io_mode(_irq, KM_GPIO_IO_MODE_INPUT);
        km_gpio_write(_ce, 1);
        _theTouch = this;
    }
    ~Touch() {
        if (_report != JERRY_TYPE_UNDEFINED) {
            jerry_release_value(_report);
        }
        _theTouch = NULL;
    }

public:
    bool GetTouch(uint16_t& X, uint16_t& Y, uint16_t& Z) const {
        X = _Xpos;
        Y = _Ypos;
        Z = _Zpos;
        return (_pressed);
    }
    static void Loop() {
        if (_theTouch != NULL) {
            _theTouch->Process();
        }
    }

private:
    void Process() {
        if (km_gpio_read(_irq) != 0)  {
            if (_pressed == true) {
                uint64_t now = millis();
                if (now > _lastAquire) {
                    Report(0,0,0);
                    _pressed = false;
                }
            }
        }
        else {
            uint64_t now = millis();

            if ((_pressed == false) || (now > _lastAquire)) {
                _lastAquire = now + _delay;
                _pressed = true;
                GetTouch();
                Report(_Xpos, _Ypos, _Zpos);
            }
        }
    }


    void Report(const uint16_t X, const uint16_t Y, const uint16_t Z) {
        if (_report == JERRY_TYPE_FUNCTION) {
            jerry_value_t obj = jerry_create_object();
            jerryxx_set_property_number(obj, MSTR_TOUCH_X_POS, X);
            jerryxx_set_property_number(obj, MSTR_TOUCH_Y_POS, Y);
            jerryxx_set_property_number(obj, MSTR_TOUCH_Z_POS, Z);

            jerry_value_t this_val = jerry_create_undefined();
            jerry_value_t args_p[1] = { obj };
            jerry_call_function(_report, this_val, args_p, 1);
            jerry_release_value(obj);
            jerry_release_value(this_val);
        }
    }

private:
   bool GetTouch() {
        _Zpos = 0;

        // Send out and receive the requested bytes...
        km_gpio_write(_ce, KM_GPIO_LOW);
        km_micro_delay(2);

        uint8_t data[2];

        data[0] = 0x91;

        // Write the control word, so the conversion is triggered
        km_spi_send(_bus, data, 1, 1000);
        km_micro_delay(10);
        km_spi_recv(_bus, 0, data, 2, 1000);
        _Xpos = data[0];
        _Xpos = (_Xpos << 4) | ((data[1]) >> 4) & 0xF;

        data[0] = 0xD0;
        km_spi_send(_bus, data, 1, 1000);
        km_micro_delay(10);
        km_spi_recv(_bus, 0, data, 2, 1000);
        _Ypos = data[0];
        _Ypos = (_Ypos << 4) | ((data[1]) >> 4) & 0xF;

        km_gpio_write(_ce, KM_GPIO_HIGH);

        return (true);
    }
    uint64_t millis() {
        return (km_gettime());
    }

private:
    uint8_t _bus;
    uint8_t _ce;
    uint8_t _irq;
    uint16_t _delay;
    uint64_t _lastAquire;
    uint8_t _pressed;
    uint16_t _Xpos;
    uint16_t _Ypos;
    uint16_t _Zpos;
    jerry_value_t _report;
    static Touch* _theTouch;
};

/* static */ Touch* Touch::_theTouch = NULL;
static void handle_freecb(void *handle) { delete (Touch*) handle; }
static const jerry_object_native_info_t handle_info = {.free_cb = handle_freecb};

/* ************************************************************************** */
/*                              MCP3208 CLASS                             */
/* ************************************************************************** */

/**
 * touch() constructor
 */
JERRYXX_FUN(ctor_touch_fn) {
  JERRYXX_CHECK_ARG_NUMBER(0, "bus");
  JERRYXX_CHECK_ARG_NUMBER(1, "ce");
  JERRYXX_CHECK_ARG_NUMBER(2, "irq");
  JERRYXX_CHECK_ARG_NUMBER(3, "delay");
  JERRYXX_CHECK_ARG_FUNCTION_OPT(4, "report");


  // read parameters
  uint8_t bus  = (uint8_t)JERRYXX_GET_ARG_NUMBER(0);
  uint8_t ce   = (uint8_t)JERRYXX_GET_ARG_NUMBER(1);
  uint8_t irq  = (uint8_t)JERRYXX_GET_ARG_NUMBER(2);
  uint16_t delay = (uint16_t)JERRYXX_GET_ARG_NUMBER(3);
  jerry_value_t callback;

  if (JERRYXX_HAS_ARG(4)) {
      callback = jerry_acquire_value(JERRYXX_GET_ARG(4));
  }
  else {
      callback = jerry_create_undefined();
  }

  // set native handle
  Touch* object = new Touch(bus, ce, irq, delay, callback);
  jerry_set_object_native_pointer(this_val, object, &handle_info);

  return jerry_create_undefined();
}

/**
 * touch.prototype.Touch()
 */
JERRYXX_FUN(get_touch_fn) {
  JERRYXX_GET_NATIVE_HANDLE(object, Touch, handle_info);
  uint16_t X,Y,Z;
  bool pressed = object->GetTouch(X,Y,Z);

  jerry_value_t obj = jerry_create_object();
  jerryxx_set_property_number(obj, MSTR_TOUCH_X_POS, X);
  jerryxx_set_property_number(obj, MSTR_TOUCH_Y_POS, Y);
  jerryxx_set_property_number(obj, MSTR_TOUCH_Z_POS, Z);
  jerryxx_set_property_number(obj, MSTR_TOUCH_PRESSED, pressed);

  return obj;
}

/**
 * Initialize touch module
 */
jerry_value_t module_touch_init() {
  /* Touch class */
  jerry_value_t ctor = jerry_create_external_function(ctor_touch_fn);
  jerry_value_t prototype = jerry_create_object();
  jerryxx_set_property(ctor, "prototype", prototype);
  jerryxx_set_property_function(prototype, MSTR_TOUCH_GET_TOUCH, get_touch_fn);
  jerry_release_value(prototype);

  /* Touch module exports */
  jerry_value_t exports = jerry_create_object();
  jerryxx_set_property(exports, MSTR_TOUCH_CONTEXT, ctor);
  jerry_release_value(ctor);

  return exports;
}

/**
 * Loop touch module
 */
void km_touch_infinite_loop() {
    Touch::Loop();    
}
