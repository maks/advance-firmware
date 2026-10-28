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

#include "advAudioDriver.h"
#include "Adapters/adv/usb/usb_audio_handoff.h"
#include "System/Profiler/StreamingPerf.h"

extern "C" {
volatile StreamingPerfCounters streamingPerf = {.version = 1};
}
#include "Application/Mixer/MixerService.h"
#include "Application/Model/Config.h"
#include "Services/Midi/MidiService.h"
#include "System/Profiler/DWTProfiler.h"
#include "System/Profiler/Profiler.h"
#include "System/System/System.h"
#include "advLineInCapture.h"
#include "config/MemorySections.h"
#include "main.h" // has to come before FreeRTOS.h due to linkage of SystemCoreClock
#include "record.h"
#include "sai.h"
// Don't delete this space
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "tlv320aic3204.h"
#include <atomic>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern DMA_HandleTypeDef hdma_sai1_a;

// mini blank buffer for underrun, initialized to 0 (16-bit samples)
const int16_t advAudioDriver::miniBlank_[MINI_BLANK_SIZE] = {0};

advAudioDriver *advAudioDriver::instance_ = NULL;
namespace {
std::atomic<uint8_t> requestedVolume{40};
} // namespace

SemaphoreHandle_t core1_audio;
static StaticSemaphore_t xSemaphoreBuffer;

static volatile unsigned long adv_sound_pausei, adv_exit;

void adv_sound_pause(int yes) { adv_sound_pausei = yes; }

void HAL_SAI_TxCpltCallback(SAI_HandleTypeDef *hsai) {
  if (hsai->Instance == hsai_BlockA1.Instance) {
    advAudioDriver::IRQHandler();
  }
}

// SAI block B receive completion. Route it to whoever currently owns the
// peripheral: the live line input mix or the record path.
void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai) {
  if (hsai->Instance != hsai_BlockB1.Instance) {
    return;
  }
  // The line input mix reads the DMA counter directly, so it has no use for
  // the half transfer signal.
  if (AdvLineInCapture::ownsTransfer()) {
    return;
  }
  RecordRxHalfCplt();
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai) {
  if (hsai->Instance != hsai_BlockB1.Instance) {
    return;
  }
  if (AdvLineInCapture::ownsTransfer()) {
    AdvLineInCapture::RxComplete();
    return;
  }
  RecordRxCplt();
}

void advAudioDriver::IRQHandler() { instance_->OnChunkDone(); }

PICOTRACKER_FAST_CODE("advance_audio_thread")
void AudioThread(void *) {
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->LAR = 0xC5ACCE55;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
  streamingPerf.cpuHz = SystemCoreClock;
  MixerService *mixer = MixerService::GetInstance();

  while (true) {
    xSemaphoreTake(core1_audio, portMAX_DELAY);

    StreamingPerfScope perf(streamingPerf.audio);
    // Prepare the next buffer (queues MIDI for this chunk)
    advAudioDriver::BufferNeeded();

    // Flush MIDI after the buffer has been queued to keep it in sync
    MidiService::GetInstance()->Flush();

    // Rendering (especially stems) can keep this high-priority task runnable
    // continuously. Yield one tick in render modes so UI/input tasks stay
    // responsive for progress updates and cancel.
    if (mixer->GetMode() != MSM_AUDIO) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

void AudioOutput(void *) {
  uint8_t appliedVolume = UINT8_MAX;
  while (true) {
    // This task owns physical routing and playback volume. Route requests
    // and StartDriver never perform codec I/O or wait for de-pop delays.
    tlv320_select_output();
    const uint8_t volume = requestedVolume.load(std::memory_order_acquire);
    if (volume != appliedVolume) {
      tlv320_set_volume(volume);
      appliedVolume = volume;
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

PICOTRACKER_FAST_CODE("advance_audio_buffer_needed")
void advAudioDriver::BufferNeeded() { instance_->OnNewBufferNeeded(); }

advAudioDriver::advAudioDriver(AudioSettings &settings)
    : AudioDriver(settings) {

  isPlaying_ = false;
  adv_exit = 0;
}

advAudioDriver::~advAudioDriver() { adv_exit = 1; }

bool advAudioDriver::InitDriver() {
  instance_ = this;
  Config *config = Config::GetInstance();
  uint8_t initialVolume = 40;
  if (config) {
    if (Variable *outputVolume =
            config->FindVariable(FourCC::VarOutputVolume)) {
      initialVolume = outputVolume->GetInt();
    }
  }

  // Configure codec
  tlv320_init();
  Variable *audioOutput =
      config ? config->FindVariable(FourCC::VarAudioOutput) : nullptr;
  const int outputMode = audioOutput != nullptr
                             ? audioOutput->GetInt()
                             : static_cast<int>(AudioOutputMode::Analog);
  const bool usbSelected =
      outputMode != static_cast<int>(AudioOutputMode::Analog);
  const bool analogEnabled =
      outputMode != static_cast<int>(AudioOutputMode::Usb);
  usbMasterAudioSetOutputSelected(usbSelected);
  tlv320_set_analog_output_enabled(analogEnabled);
  tlv320_select_output();
  requestedVolume.store(initialVolume, std::memory_order_release);
  tlv320_set_volume(initialVolume);
  // Takes some time between configuring HP detect and actually detecting
  vTaskDelay(pdMS_TO_TICKS(250));

  // Run a task that will periodically poll for output change. Cannot do this
  // easily on a timer due to de-pop wait times and timers being single
  // threaded. A delay on a timer can stall other timers.
  static StackType_t AudioOutputStack[1000];
  static StaticTask_t AudioOutputTCB;
  xTaskCreateStatic(AudioOutput, "Audio Output", 1000, NULL, 1,
                    AudioOutputStack, &AudioOutputTCB);

  core1_audio = xSemaphoreCreateCountingStatic(SOUND_BUFFER_COUNT - 1, 0,
                                               &xSemaphoreBuffer);

  __attribute__((section(".DTCMRAM"))) static StackType_t AudioStack[4000];
  static StaticTask_t ProcessEventTCB;
  xTaskCreateStatic(AudioThread, "Audio", 4000, NULL, 6, AudioStack,
                    &ProcessEventTCB);

  return true;
};

void advAudioDriver::SetVolume(int v) {
  Trace::Debug("Setting volume to %d", v);
  requestedVolume.store(static_cast<uint8_t>(v < 0     ? 0
                                             : v > 100 ? 100
                                                       : v),
                        std::memory_order_release);
};

int advAudioDriver::GetVolume() {
  return requestedVolume.load(std::memory_order_acquire);
};

void advAudioDriver::CloseDriver() {
  // Not really used, maybe for sleep?
};

bool advAudioDriver::StartDriver() {
  isPlaying_ = true;
  usbMasterAudioSetPlaybackActive(1);

  // Start filling up as many buffers as we have
  for (int i = 0; i < SOUND_BUFFER_COUNT - 1; i++) {
    xSemaphoreGive(core1_audio);
  }

  // Send first DMA transfer
  HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)miniBlank_, MINI_BLANK_SIZE);

  adv_sound_pause(0);

  startTime_ = HAL_GetTick();

  // SAI/DMA remains the playback clock in either route. The output task
  // applies the requested physical route; startup performs no codec I/O.
  return true;
};

void advAudioDriver::StopDriver() {
  adv_sound_pause(1);
  isPlaying_ = false;
  usbMasterAudioSetPlaybackActive(0);
};

PICOTRACKER_FAST_CODE("advance_audio_publish_usb_master")
void advAudioDriver::AddBuffer(short *buffer, int samplecount,
                               bool silenceOutput) {
  MixerService *mixer = MixerService::GetInstance();
  if (isPlaying_ && mixer != nullptr && mixer->GetMode() == MSM_AUDIO &&
      samplecount > 0 && samplecount <= MAX_SAMPLE_COUNT) {
    usbMasterAudioPublish(reinterpret_cast<const int16_t *>(buffer),
                          static_cast<uint16_t>(samplecount), silenceOutput);
  }
  AudioDriver::AddBuffer(buffer, samplecount, silenceOutput);
}

void advAudioDriver::OnChunkDone() {
  if (isPlaying_) {

    // We got an IRQ so we know we finished playing from poolPlayPosition_
    // We mark it as empty and inspect the next buffer, if the buffer is not
    // empty, it means thread 2 finished processing that buffer and there are no
    // underruns Otherwise, we send a small blank buffer and wait for the other
    // thread to finish
    pool_[poolPlayPosition_].empty_ = true;

    int next = (poolPlayPosition_ + 1) % SOUND_BUFFER_COUNT;
    if (pool_[next].empty_) {
      streamingPerf.audioUnderruns = streamingPerf.audioUnderruns + 1;
      HAL_SAI_Transmit_DMA(&hsai_BlockA1, (uint8_t *)miniBlank_,
                           MINI_BLANK_SIZE);
    } else {
      poolPlayPosition_ = next;
      HAL_SAI_Transmit_DMA(&hsai_BlockA1,
                           (uint8_t *)pool_[poolPlayPosition_].buffer_,
                           pool_[poolPlayPosition_].size_ / 2);
    }

    // Finally we allow core1 to calculate an additional buffer
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(core1_audio, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
  }
}

int advAudioDriver::GetPlayedBufferPercentage() {
  //	return
  // 100-(bufferSize_-bufferPos_-fragSize_)*100/(bufferSize_-fragSize_);
  // TODO: Do this right
  return 0;
};

double advAudioDriver::GetStreamTime() {
  return (HAL_GetTick() - startTime_) / 1000.0;
};
