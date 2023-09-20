#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct onewire_address_d {
  uint8_t address[8];

  // The first byte [0] is the family, the last byte [7] is the CRC, 
  // bytes [1..6] is the unique address..

} onewire_address_t;

uint8_t onewire_address_crc(const onewire_address_t* address);

uint8_t onewire_create(const uint8_t pin);
void onewire_destroy(const uint8_t busid);

uint8_t onewire_scan(const uint8_t busid);
const onewire_address_t* onewire_link(const uint8_t busid, const uint8_t index);

int onewire_read(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, uint8_t* buffer);
int onewire_write(const uint8_t busid, const onewire_address_t* address, const uint8_t command, const uint8_t length, const uint8_t* buffer);

bool onewire_parasite(const uint8_t busid, const onewire_address_t* address);
void onewire_power(const uint8_t busid, const uint16_t duration_ms);

void km_onewire_init();
void km_onewire_cleanup();

#define KM_MAX_ONEWIRE_BUS 5
