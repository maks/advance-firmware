/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef INSTRUMENT_TRANSFER_PROGRESS_H
#define INSTRUMENT_TRANSFER_PROGRESS_H

#include "Application/AppWindow.h"
#include "Application/Utils/DrawUtils.h"
#include "Application/Views/BaseClasses/ModalView.h"
#include <algorithm>
#include <new>

// Draw directly to an overlay so synchronous file operations can refresh it
// without waiting for the normal view event loop.
class InstrumentTransferProgress : public ModalView {
public:
  InstrumentTransferProgress(View &view, const char *title, bool cancellable)
      : ModalView(view), title_(title), cancellable_(cancellable) {}

  static void Begin(View &view, const char *title, bool cancellable = false) {
    Finish();
    alignas(InstrumentTransferProgress) static unsigned char
        storage[sizeof(InstrumentTransferProgress)];
    active_ =
        new (storage) InstrumentTransferProgress(view, title, cancellable);
    Update(0, 1);
  }

  static void Update(uint32_t completed, uint32_t total) {
    if (!active_)
      return;
    const uint32_t percent =
        total ? std::min<uint32_t>(
                    100, (static_cast<uint64_t>(completed) * 100) / total)
              : 0;
    if (active_->lastPercent_ == percent)
      return;
    active_->lastPercent_ = percent;
    auto &window = static_cast<AppWindow &>(active_->w_);
    window.BeginOverlay(active_->GetInstanceId());
    active_->DrawView();
    window.EndOverlay();
  }

  static void Finish() {
    if (!active_)
      return;
    auto &window = static_cast<AppWindow &>(active_->w_);
    window.CloseOverlay(active_->GetInstanceId());
    active_->~InstrumentTransferProgress();
    active_ = nullptr;
  }

  void DrawView() override {
    SetWindow(22, cancellable_ ? 4 : 3);
    GUITextProperties props;
    SetColor(CD_NORMAL);
    DrawString(0, 0, title_, props);
    progressBar_t bar;
    fillProgressBar(lastPercent_, 100, &bar);
    DrawString(5, 2, bar, props);
    if (cancellable_)
      DrawString(0, 3, "NAV+LEFT: Cancel", props);
  }
  void OnPlayerUpdate(PlayerEventType, unsigned int) override {}
  void OnFocus() override {}
  void ProcessButtonMask(unsigned short, bool) override {}
  void AnimationUpdate() override {}

private:
  inline static InstrumentTransferProgress *active_ = nullptr;
  const char *title_;
  bool cancellable_;
  uint32_t lastPercent_ = UINT32_MAX;
};
#endif
