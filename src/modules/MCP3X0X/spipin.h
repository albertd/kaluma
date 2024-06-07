#pragma once

#include <stdio.h>
#include <algorithm>
#include <pico/stdlib.h>
#include <hardware/spi.h>

namespace SPIDevice {

    class SPIPort {
    public:
        SPIPort(
            const uint8_t bus,
            const uint8_t ce,
            const uint8_t mode,
            const bool msb_order,
            const uint32_t speed,
            const uint8_t bitsPerWord)
            : _spi(bus == 0 ? spi0 : (bus == 1 ? spi1 : nullptr))
            , _ce(ce) {

            gpio_init(_ce);
            gpio_set_dir(_ce, GPIO_OUT);
            ASSERT(spi != nullptr);

            spi_cpol_t pol = SPI_CPOL_0;
            spi_cpha_t pha = SPI_CPHA_0;
            spi_order_t order = (msb_order ? SPI_MSB_FIRST : SPI_LSB_FIRST);
            switch (mode) {
                case 0:
                    pol = SPI_CPOL_0;
                    pha = SPI_CPHA_0;
                    break;
                case 1:
                    pol = SPI_CPOL_0;
                    pha = SPI_CPHA_1;
                    break;
                case 2:
                    pol = SPI_CPOL_1;
                    pha = SPI_CPHA_0;
                    break;
                case 3:
                    pol = SPI_CPOL_1;
                    pha = SPI_CPHA_1;
                    break;
            }
            spi_init(_spi, speed);
            spi_set_format(_spi, bitsPerWord, pol, pha, order);
            gpio_set_function(10, GPIO_FUNC_SPI); // CLK
            gpio_set_function(11, GPIO_FUNC_SPI); // MOSI
            gpio_set_function(12, GPIO_FUNC_SPI); // MISO
        }
        ~SPIPort() {
            spi_deinit(_spi);
        }

    public:
        void Exchange (const uint8_t length, uint8_t buffer) {

            // Send out and receive the requested bytes...
            gpio_put(_ce, 0);
            sleep_us(100);
            spi_write_read_blocking(_spi, buffer, buffer, length);
            gpio_put(_ce, 1);
        }

    private:
        spi_inst_t* _spi;
        uint8_t _ce;
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

    public:
        MCP3X0XType() = delete;
        MCP3X0XType(MCP3X0XType<BITS,CHANNELBITS>&&) = delete;
        MCP3X0XType(const MCP3X0XType<BITS, CHANNELBITS>&) = delete;
        MCP3X0XType<BITS,CHANNELBITS>& operator=(MCP3X0XType<BITS,CHANNELBITS>&&) = delete;
        MCP3X0XType<BITS,CHANNELBITS>& operator=(const MCP3X0XType<BITS,CHANNELBITS>&) = delete;

        MCP3X0XType(SPIPort& port, const mode channel, const int16_t min, const int16_t max)
            : _port(port)
            , _channel(channel)
            , _inverse(_min > _max)
            , _min(_inverse ? max : min)
            , _max(_inverse ? min : max) {
            ASSERT((_channel & 0x7) <= Device::Channels);
        }
        ~MCP3X0XType() = default;

    public:
        int16_t Get() const {
            int16_t value;
            if (Value(value) == 0) {
                return (value);
            }
            return (0);
        }
        // Value determination of this element
        uint16_t Value(int16_t& value) const {
            int16_t result = _device.Value(_channel & 0x07, ((_channel & 0x08) != 0));
            result = (_inverse ? -result : result);

            value = ToRange(result);

            return (_device.Error());
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
            if ((_max - _min) != Device::Range) {
                result = ((((value * Device::Range * 2) + (_max - _min)) / (2 * (_max - _min))) + _min);
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

}
