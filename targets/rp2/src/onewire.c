#include <stdlib.h>
#include <string.h>
#include <pico/time.h>
#include <hardware/gpio.h>
#include <port/onewire.h>
#include <stdio.h>

const uint8_t SkipROMCommand         = 0xCC;
const uint8_t SearchROMCommand       = 0xF0;
const uint8_t ReadPowerSupplyCommand = 0xB4;
const uint8_t MatchROMCommand        = 0x55;

const uint8_t ParasitePowerTested    = 0x80;
const uint8_t ParasitePowerRequired  = 0x40;
const uint8_t ParasitePowered        = 0x10;

typedef struct onewire_link_d {
  onewire_address_t address;
  uint8_t flags;
  struct onewire_link_d* next;
} onewire_link_t;

typedef struct onewire_bus_d {
  uint8_t pin;
  uint8_t flags;
  onewire_link_t* links;
} onewire_bus_t;

static onewire_bus_t onewire_bus[KM_MAX_ONEWIRE_BUS];

static uint8_t crc_byte(const uint8_t input, const uint8_t added) {
  uint8_t crc = input;
  uint8_t byte = added;

  for (uint8_t n = 0; n < 8; n++) {
    if ((byte & 0x01) ^ (crc & 0x01)) {
      // DATA ^ LSB CRC = 1
      crc = crc >> 1;
      // Set the MSB to 1
      crc = (uint8_t) (crc | 0x80);
      // Check bit 3
      if (crc & 0x04) {
        crc = (uint8_t) (crc & 0xFB);// Bit 3 is set, so clear it
      }
      else {
        crc = (uint8_t) (crc | 0x04);// Bit 3 is clear, so set it
      }
      // Check bit 4
      if (crc & 0x08) {
        crc = (uint8_t) (crc & 0xF7);// Bit 4 is set, so clear it
      }
      else {
        crc = (uint8_t) (crc | 0x08);// Bit 4 is clear, so set it
      }
    } 
    else {
      // DATA ^ LSB CRC = 0
      crc = crc >> 1;
      // clear MSB
      crc = (uint8_t) (crc & 0x7F);
      // No need to check bits, with DATA ^ LSB CRC = 0, they will remain unchanged
    }
    byte = byte >> 1;
  }
  return crc;
}

static uint8_t calculate_crc(const uint8_t length, const uint8_t* address) {
  uint8_t crc = 0x00;
  for (uint8_t n = 0; n < length; n++) {
    crc = crc_byte(crc, address[n]);
  }
  return (crc);
}

static bool reset_check_for_device(const uint8_t pin) {
  // This will return false if no devices are present on the data bus
  bool presence = false;
  // Done by the create..
  // gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, false); // bring low for 480us
  sleep_us(480);
  gpio_set_dir(pin, GPIO_IN); // let the data line float high
  sleep_us(70); // wait 70us
  if (!gpio_get(pin)) {
  // see if any devices are pulling the data line low
    presence = true;
  }
  sleep_us(410);
  return presence;
}

static void onewire_bit_out(const uint8_t pin, const bool bit_data) {
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, false);
  sleep_us(3);// (spec 1-15us)
  if (bit_data) {
    gpio_put(pin, true);
    sleep_us(55);
  } else {
    sleep_us(60);// (spec 60-120us)
    gpio_put(pin, true);
    sleep_us(5);// allow bus to float high before next bit_out
  }
}

static void onewire_byte_out(const uint8_t pin, const uint8_t data) {
  uint8_t send = data;
  for (uint8_t n = 0; n < 8; n++) {
    onewire_bit_out(pin, (bool) (send & 0x01));
    send = send >>  1;
  }
}

static bool onewire_bit_in(const uint8_t pin) {
  bool answer;
  gpio_set_dir(pin, GPIO_OUT);
  gpio_put(pin, false);
  sleep_us(3);// (spec 1-15us)
  gpio_set_dir(pin, GPIO_IN);
  sleep_us(3);// (spec read within 15us)
  answer = gpio_get(pin);
  sleep_us(45);
  return answer;
}

static uint8_t onewire_byte_in(const uint8_t pin) {
  uint8_t answer = 0x00;
  for (uint8_t n = 0; n < 8; n++) {
    answer = answer >> 1;// shift over to make room for the next bit
    if (onewire_bit_in(pin)) {
      answer = (uint8_t) (answer | 0x80);// if the data port is high, make this bit a 1
    }
  }
  return answer;
}

static int skip_rom(const uint8_t pin) {
  if (reset_check_for_device(pin)) {
    onewire_byte_out(pin, SkipROMCommand);
    return (0);
  } 
  return(ERR_RESET_FAILED);
}

static int match_rom(const uint8_t pin, const onewire_address_t* address) {
  if (reset_check_for_device(pin)) {
    onewire_byte_out(pin, MatchROMCommand);
    for (uint8_t n = 0; n < 8; n++) {
      onewire_byte_out(pin, address->address[n]);
    }
    return (0);
  }
  return(ERR_RESET_FAILED);
}

static uint8_t _search_ROM[8];
static uint8_t _last_discrepancy;

static int search_rom_find_next(const uint8_t pin) {
  uint8_t result = ERR_RESET_FAILED;

  if (reset_check_for_device(pin)) {
    uint8_t rom_bit_index = 1;
    uint8_t discrepancy_marker = 0;
    uint8_t byte_counter = 0;
    uint8_t bit_mask = 0x01;

    onewire_byte_out(pin, SearchROMCommand);

    while (rom_bit_index <= 64) {
      bool bitA = onewire_bit_in(pin);
      bool bitB = onewire_bit_in(pin);
      if (bitA & bitB) {
        discrepancy_marker = 0;// data read error, this should never happen
        rom_bit_index = 0xFF;
      } 
      else {
        if (bitA | bitB) {
          // Set ROM bit to Bit_A
          if (bitA) {
            _search_ROM[byte_counter] = _search_ROM[byte_counter] | bit_mask;// Set ROM bit to one
          } 
          else {
            _search_ROM[byte_counter] = _search_ROM[byte_counter] & ~bit_mask;// Set ROM bit to zero
          }
        } 
        else {
          // both bits A and B are low, so there are two or more devices present
          if (rom_bit_index == _last_discrepancy) {
            _search_ROM[byte_counter] = _search_ROM[byte_counter] | bit_mask;// Set ROM bit to one
          } else {
            if (rom_bit_index > _last_discrepancy) {
              _search_ROM[byte_counter] = _search_ROM[byte_counter] & ~bit_mask;// Set ROM bit to zero
              if (discrepancy_marker == 0) {
                discrepancy_marker = rom_bit_index;
              }
            } else if ((_search_ROM[byte_counter] & bit_mask) == 0x00) {
              discrepancy_marker = rom_bit_index;
            }
          }
        }
        onewire_bit_out(pin, _search_ROM[byte_counter] & bit_mask);
        rom_bit_index++;
        if (bit_mask & 0x80) {
          byte_counter++;
          bit_mask = 0x01;
        } 
        else {
          bit_mask = bit_mask << 1;
        }
      }
    }

    _last_discrepancy = discrepancy_marker;

    if (rom_bit_index == 0xFF) {
      result = ERR_DATA_READ_ERROR;
    }
    else if (calculate_crc(7, _search_ROM) != _search_ROM[7]) {// Check the CRC
      result = ERR_BAD_CRC;
    }
    else {
      result = 0;
    }
  }

  return (result);
}

static int bus_status(const uint8_t busid) {
  int result;

  if ( (busid >= (sizeof(onewire_bus)/sizeof(onewire_bus_t))) || (onewire_bus[busid].pin == ~0) ) {
    result = ERR_INVALIDBUS;
  }
  else if ((onewire_bus[busid].flags & ParasitePowered) != 0) {
    result = ERR_BUS_IS_POWERED;
  }
  else {
    result = 0;
  }
  return (result);
}

uint8_t onewire_calculate_crc(const uint8_t length, const uint8_t* buffer) {
  return (calculate_crc(length, buffer));
}

uint8_t onewire_create(const uint8_t pin) {
  uint8_t busid = 0;

  // See if we have a slot (read bus) left..
  while ((busid < (sizeof(onewire_bus)/sizeof(onewire_bus_t))) && (onewire_bus[busid].pin != 0xFF)) {
    busid++;
  }

  if (busid >= (sizeof(onewire_bus)/sizeof(onewire_bus_t))) {
    busid = ~0;
  }
  else {
    onewire_bus[busid].links    = NULL;
    onewire_bus[busid].pin      = pin;
    onewire_bus[busid].flags    = 0x00;

    gpio_init(onewire_bus[busid].pin);
  }

  return (busid);
}

void onewire_destroy(const uint8_t busid) {
  if (busid < (sizeof(onewire_bus)/sizeof(onewire_bus_t))) {
    if (onewire_bus[busid].pin != ~0) {
      // If there is still something left from the previous run, kill it.
      onewire_link_t* destructor = onewire_bus[busid].links;
      while (destructor != NULL) {
        onewire_link_t* dispose = destructor;
        destructor = destructor->next;
        free(dispose);
      }
      onewire_bus[busid].pin = ~0;
    }
  }
}

int onewire_scan(const uint8_t busid, uint8_t* report) {
  int result = bus_status(busid);

  if ( result == 0 ) {
    uint8_t count = 0;
    uint8_t pin = onewire_bus[busid].pin;
    onewire_link_t** locator = &(onewire_bus[busid].links);

    int result = 0;
    _last_discrepancy = 0;

    do {
      result = search_rom_find_next(pin);
      if (result == 0) {
        // Found a new address, store it..
        if (*locator == NULL) {
          *locator = (onewire_link_t*) malloc(sizeof(onewire_link_t));
          (*locator)->next = NULL;
        }
        memcpy((*locator)->address.address, _search_ROM, sizeof(_search_ROM));
        (*locator)->flags = 0x00;

        locator = &((*locator)->next);
        count++;
      }
    } while ((_last_discrepancy != 0) && ((result == 0) || (result == ERR_BAD_CRC)));

    // If there is still something left from the previous run, kill it.
    onewire_link_t* destructor = *locator;
    while (destructor != NULL) {
      onewire_link_t* dispose = destructor;
      destructor = destructor->next;
      free(dispose);
    }
    *locator = NULL;
    *report = count;
  }

  return (result);
}

const onewire_address_t* onewire_link(const uint8_t busid, const uint8_t index) {
  const onewire_address_t* result = NULL;

  if ( (busid < (sizeof(onewire_bus)/sizeof(onewire_bus_t))) && (onewire_bus[busid].pin != ~0) ) {
    uint8_t current = 0;
    onewire_link_t* loop = onewire_bus[busid].links;
    while ((index != current) && (loop != NULL)) {
      loop = loop->next;
      current++;
    }
    result = (loop != NULL ? &(loop->address) : NULL);
  }

  return (result);
}

int onewire_read(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, uint8_t* buffer) {
  int result = bus_status(busid);

  if (result == 0) {
    uint8_t pin = onewire_bus[busid].pin;

    if (address == NULL) {
      result = skip_rom(pin);
    }
    else {
      result = match_rom(pin, address);
    }

    if (result == 0) {
      onewire_byte_out(pin, command);

      for (uint8_t n = 0; n < length; n++) {
        buffer[n] = onewire_byte_in(pin);
      }
    }
  }
  return (result);
}

int onewire_write(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, const uint8_t* buffer) {
  int result = bus_status(busid);

  if (result == 0) {
    uint8_t pin = onewire_bus[busid].pin;

    if (address == NULL) {
      result = skip_rom(pin);
    }
    else {
      result = match_rom(pin, address);
    }

    if (result == 0) {
      onewire_byte_out(pin, command);

      for (uint8_t n = 0; n < length; n++) {
        onewire_byte_out(pin, buffer[n]);
      }
    }
  }
  return (result);
}

int onewire_parasite(const uint8_t busid, const onewire_address_t* address) {
  int result = bus_status(busid);

  if (result == 0) {
    uint8_t pin = onewire_bus[busid].pin;
    onewire_link_t* entry = NULL;
    int error;

    if (address == NULL) {
      if ((onewire_bus[busid].flags & ParasitePowerTested) == 0) {
        error = skip_rom(pin);
      }
      else {
        error = -1;
        result = ((onewire_bus[busid].flags & ParasitePowerRequired) ? ERR_PARASITE_POWER : 0);
      }
    } else {
      entry = onewire_bus[busid].links;
      while ((entry != NULL) && (memcmp(entry->address.address, address->address, sizeof(onewire_address_t)) != 0)) {
        entry = entry->next;
      }
      if ((entry == NULL) || ((entry->flags & ParasitePowerTested) == 0)) {
        error = match_rom(pin, address);
      }
      else {
        error = -1;
        result = ((entry->flags & ParasitePowerRequired) ? ERR_PARASITE_POWER : 0);
      }
    }

    if (error == 0) {
      onewire_byte_out(pin, ReadPowerSupplyCommand);

      bool outcome = onewire_bit_in(pin);

      if (address == NULL) {
        onewire_bus[busid].flags |= (ParasitePowerTested | (outcome ? ParasitePowerRequired : 0));
      }
      else if (entry != NULL) {
        entry->flags |= (ParasitePowerTested | (outcome ? ParasitePowerRequired : 0));
      }
      result = (outcome ? ERR_PARASITE_POWER : 0);
    }
  }

  return (result);
}

int onewire_power(const uint8_t busid, const bool powered) {
  int result = bus_status(busid);

  if ((result == ERR_BUS_IS_POWERED) && (powered == false)) {
    uint8_t pin = onewire_bus[busid].pin;
    gpio_set_dir(pin, GPIO_IN);
    onewire_bus[busid].flags &= (~ParasitePowered);
    result = 0;
  }
  else if ((result == 0) && (powered == true)) {
    onewire_bus[busid].flags |= ParasitePowered;
    uint8_t pin = onewire_bus[busid].pin;
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, true);
  }
  else {
    result = ERR_INVALID_REQUEST;
  }
  return (result);
}

void km_onewire_init() {
  for (uint8_t index = 0; index < (sizeof(onewire_bus)/sizeof(onewire_bus_t)); index++) {
    onewire_bus[index].pin      = 0xFF;
  }
}

void km_onewire_cleanup() {
  for (uint8_t index = 0; index < (sizeof(onewire_bus)/sizeof(onewire_bus_t)); index++) {
    if (onewire_bus[index].pin != 0xFF) {
      onewire_link_t* destructor = onewire_bus[index].links;
      while (destructor != NULL) {
        onewire_link_t* dispose = destructor;
        destructor = destructor->next;
        free(dispose);
      }
    }
  }
}
