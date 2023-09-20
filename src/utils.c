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

#include "utils.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

void km_list_init(km_list_t *list) {
  list->head = NULL;
  list->tail = NULL;
}

void km_list_append(km_list_t *list, km_list_node_t *node) {
  if (list->tail == NULL && list->head == NULL) {
    list->head = node;
    list->tail = node;
    node->next = NULL;
    node->prev = NULL;
  } else {
    list->tail->next = node;
    node->prev = list->tail;
    node->next = NULL;
    list->tail = node;
  }
}

void km_list_remove(km_list_t *list, km_list_node_t *node) {
  if (list->head == node) {
    list->head = node->next;
  }
  if (list->tail == node) {
    list->tail = node->prev;
  }
  if (node->prev != NULL) {
    node->prev->next = node->next;
  }
  if (node->next != NULL) {
    node->next->prev = node->prev;
  }
}

uint8_t km_hex1(char hex) {
  if (hex >= 'a') {
    return (hex - 'a' + 10);
  } else if (hex >= 'A') {
    return (hex - 'A' + 10);
  } else {
    return (hex - '0');
  }
}

uint8_t km_hex2bin(const unsigned char *hex) {
  uint8_t hh = km_hex1(hex[0]);
  uint8_t hl = km_hex1(hex[1]);
  return hh << 4 | hl;
}

void km_bytes_to_string(const uint8_t* input, uint8_t len, char* buffer) {
  static const char hex_array[] = "0123456789ABCDEF";
  for (uint8_t index = 0; index < len; index++) {
    buffer[(index * 3) + 0] = hex_array[input[index] >> 4];
    buffer[(index * 3) + 1] = hex_array[input[index] & 0x0F];
    buffer[(index * 3) + 2] = ':';
  }
  buffer[(len * 3) - 1 ] = '\0';
}

uint8_t km_string_to_bytes(const char* text, uint8_t* input, const uint8_t len) {
  uint8_t index  = 0;
  uint8_t loaded = 0;
  while ((text[index] != '\0') && (text[index+1] != '\0') && (loaded < len)) {
    input[loaded++] = km_hex2bin((const unsigned char*)&text[index]);
    index += ( ((text[index+2] == '\0') || (isxdigit((uint8_t) text[index+2]))) ? 2 : 3 );
  }
  return (loaded);
}

