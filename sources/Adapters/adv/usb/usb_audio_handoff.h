#ifndef _USB_AUDIO_HANDOFF_H_
#define _USB_AUDIO_HANDOFF_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Producer side is the Advance audio task. The USB task is the sole consumer.
void usbMasterAudioPublish(const int16_t *interleavedStereo,
                           uint16_t frameCount, bool silence);
uint16_t usbMasterAudioRead(int16_t *interleavedStereo, uint16_t maxFrameCount);
void usbMasterAudioClearQueue();
void usbMasterAudioReset();
uint32_t usbMasterAudioDroppedFrames();
uint32_t usbMasterAudioUnderruns();
uint32_t usbMasterAudioDiscardedFrames();
// Queue blocks rejected for capacity. Local losses, unlike lifecycle
// discards; they never reset the stream or add a startup wait.
uint32_t usbMasterAudioOverflowEvents();
// Running-stream packet frames sent as silence because no PCM was queued.
uint32_t usbMasterAudioUnderrunPaddedFrames();
void usbMasterAudioSetStreaming(uint8_t active);
bool usbMasterAudioIsStreaming();
void usbMasterAudioSetPlaybackActive(uint8_t active);
void usbMasterAudioSetOutputSelected(uint8_t selected);
bool usbMasterAudioOutputSelected();
void usbMasterAudioSetSuspended(uint8_t active);
void usbMasterAudioSetConfigured(uint8_t active);
void usbMasterAudioService();

#ifdef __cplusplus
}
#endif

#endif
