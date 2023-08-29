#ifndef __GC_16BITS_PRIMS_H
#define __GC_16BITS_PRIMS_H

#include "font.h"
#include "gc.h"

// Creating a graphics context object for 16-bit color LCD
uint16_t gc_prim_16bit_setup(gc_handle_t *handle, jerry_value_t options);

#endif /* __GC_16BITS_PRIMS_H */
