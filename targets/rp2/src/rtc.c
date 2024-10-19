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

#include "rtc.h"

#include "pico/aon_timer.h"
#include "pico/stdlib.h"

void km_rtc_init() {
  struct timespec ts = { 0, 0 };
  aon_timer_start(&ts);
}

void km_rtc_cleanup() {
  aon_timer_stop();
}

void km_rtc_set_time(uint64_t time) {
  struct timespec ts = { 0, 0 };
  ts.tv_sec = time / 1000;
  aon_timer_set_time(&ts);
}

uint64_t km_rtc_get_time() {
  struct timespec ts;
  aon_timer_get_time(&ts);
  return ts.tv_sec;
}
