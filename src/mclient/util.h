/*
 * Copyright 2016 The Maru OS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef M_UTIL_H
#define M_UTIL_H

#include <stdint.h>

/**
 * This assumes pixel is ARGB8888 word-order (MSB = A, ..., LSB = B).
 */
uint8_t argb8888_get_alpha(uint32_t pixel);

#endif // M_UTIL_H
