#ifndef TEST_TUSB_H
#define TEST_TUSB_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t data[720];
  uint16_t count;
  uint16_t capacity;
} tu_fifo_t;

static inline uint16_t tu_fifo_remaining(const tu_fifo_t *fifo) {
  return fifo->capacity - fifo->count;
}

static inline uint16_t tu_fifo_count(const tu_fifo_t *fifo) {
  return fifo->count;
}

typedef enum { TUSB_SPEED_FULL, TUSB_SPEED_HIGH } tusb_speed_t;

extern "C" {
tusb_speed_t tud_speed_get(void);
bool tud_audio_tx_done_pre_load_cb(uint8_t rhport, uint8_t functionId,
                                   uint8_t endpoint, uint8_t alternateSetting);
bool tud_inited(void);
bool tud_audio_n_mounted(uint8_t functionId);
bool tud_audio_n_clear_ep_in_ff(uint8_t functionId);
tu_fifo_t *tud_audio_n_get_ep_in_ff(uint8_t functionId);
uint16_t tud_audio_n_write(uint8_t functionId, const void *data,
                           uint16_t byteCount);
}

#endif
