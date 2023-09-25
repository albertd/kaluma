#ifndef __ONEWIRE_PORTABILITY_H
#define __ONEWIRE_PORTABILITY_H

#include <stdint.h>
#include <stdbool.h>

typedef struct onewire_address_d {
  uint8_t address[8];

  // The first byte [0] is the family, the last byte [7] is the CRC, 
  // bytes [1..6] is the unique address..

} onewire_address_t;

uint8_t onewire_calculate_crc(const uint8_t length, const uint8_t* buffer);

uint8_t onewire_create(const uint8_t pin);
void onewire_destroy(const uint8_t busid);

int  onewire_scan(const uint8_t busid, uint8_t* count);
const onewire_address_t* onewire_link(const uint8_t busid, const uint8_t index);

int onewire_read(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, uint8_t* buffer);
int onewire_write(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, const uint8_t* buffer);

int onewire_parasite(const uint8_t busid, const onewire_address_t* address);
int onewire_power(const uint8_t busid, const bool power);

void km_onewire_init();
void km_onewire_cleanup();

#define ERR_RESET_FAILED      -1
#define ERR_DATA_READ_ERROR   -2
#define ERR_BAD_CRC           -3
#define ERR_PARASITE_POWER    -4
#define ERR_INVALIDBUS        -5
#define ERR_BUS_IS_POWERED    -6
#define ERR_INVALID_REQUEST   -7

#define KM_MAX_ONEWIRE_BUS 5

#endif // __ONEWIRE_PORTABILITY_H
