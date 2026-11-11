/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */
#ifndef _TLV320AIC3204_
#define _TLV320AIC3204_

#include "Application/Utils/bit.h"
#include <stdbool.h>
#include <stdint.h>

// #define I2C hi2c4
#define CODEC_ADDR (0x18 << 1)
// #define CODEC_RESET_PORT GPIOC
// #define CODEC_RESET_PIN GPIO_PIN_13
#define I2C_MEMADD_SIZE_8BIT (0x00000001U)

#define REFERENCE_PWR_UP_SLOW BIT(2)
#define COMMON_MODE_09V BIT(6)

#ifdef __cplusplus
extern "C" {
#endif

void tlv320_init();

void tlv320_select_output(void);
// Nonblocking request; the output task applies routing and de-pop delays.
void tlv320_set_analog_output_enabled(bool enabled);
void tlv320_set_volume(uint8_t);
uint8_t tlv320_get_volume(void);
bool tlv320_is_initialized(void);
void tlv320_unmute(void);
void tlv320_mute(void);
void tlv320_enable_linein(void);
void tlv320_enable_mic(void);
void tlv320_disable_linein(void);
void tlv320_disable_mic(void);
void tlv320_set_linein_analog_gain_db(int32_t gainDb);
void tlv320_set_mic_digital_gain_db(int32_t gainDb);
void tlv320_sleep(void);

#ifdef __cplusplus
}
#endif

#endif
