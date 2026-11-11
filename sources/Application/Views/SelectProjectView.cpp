/*
 * SPDX-License-Identifier: BSD-3-Clause AND GPL-3.0-only
 *
 * Copyright (c) 2018 Discodirt
 * Copyright (c) 2024 xiphonics, inc.
 *
 * This file is part of the picoTracker firmware
 *
 * Inherited BSD-licensed material remains BSD-3-Clause.
 * New original contributions by xiphonics, inc. are GPL-3.0-only.
 * Previously granted BSD permissions are preserved. See LICENSING.md.
 */

#include "SelectProjectView.h"
#include "Application/AppWindow.h"
#include "Application/Persistency/PersistencyService.h"
#include "Application/Utils/DrawUtils.h"
#include "Application/Views/BaseClasses/BrowserEntrySort.h"
#include "Application/Views/BaseClasses/BrowserPaging.h"
#include "Application/Views/ModalDialogs/MessageBox.h"
#include "BaseClasses/ViewEvent.h"
#include "Externals/etl/include/etl/string.h"
#include "Externals/etl/include/etl/to_string.h"
#include "Foundation/Constants/SpecialCharacters.h"
#include "System/System/System.h"
#include <cstring>
#include <new>

#define LIST_PAGE_SIZE (SCREEN_HEIGHT - 4)
#define LIST_WIDTH 26
#define INVALID_PROJECT_NAME "INVALID NAME"
#define DELETE_HOLD_MASK (EPBM_ALT | EPBM_PLAY | EPBM_EDIT)
#define DELETE_HOLD_DURATION_MS 2000

namespace {

bool isDirectoryListTruncated(FileSystem *fs) {
  return fs != nullptr &&
         fs->GetLastListDirectoryStatus() == DirectoryListStatus::Truncated;
}

void showDirectoryListTruncatedDialog(View &view) {
  etl::string<24> line("Showing first ");
  etl::to_string(MAX_FILE_INDEX_SIZE, line, true);
  MessageBox *mb =
      MessageBox::Create(view, "Directory too large", line.c_str(), MBBF_OK);
  view.DoModal(mb);
}

} // namespace

class DeleteProjectConfirmModal : public ModalView {
public:
  static DeleteProjectConfirmModal *Create(View &view, const char *projectName);
  virtual ~DeleteProjectConfirmModal();
  virtual void Destroy() override;
  virtual void DrawView() override;
  virtual void OnPlayerUpdate(PlayerEventType, unsigned int) override {}
  virtual void OnFocus() override {}
  virtual void ProcessButtonMask(unsigned short mask, bool pressed) override;
  virtual void AnimationUpdate() override;

private:
  DeleteProjectConfirmModal(View &view, const char *projectName);
  void UpdateProgress_();
  static bool inUse_;
  static void *storage_;
  etl::string<MAX_PROJECT_NAME_LENGTH + 12> projectLine_;
  unsigned short currentMask_ = 0;
  unsigned long holdStartMs_ = 0;
  uint16_t holdProgressMs_ = 0;
  bool holdingCombo_ = false;
};

bool DeleteProjectConfirmModal::inUse_ = false;
alignas(
    DeleteProjectConfirmModal) static unsigned char DeleteProjectConfirmModalStorage
    [sizeof(DeleteProjectConfirmModal)];
void *DeleteProjectConfirmModal::storage_ = DeleteProjectConfirmModalStorage;

DeleteProjectConfirmModal *DeleteProjectConfirmModal::Create(View &view,
                                                             const char *name) {
  if (inUse_) {
    auto *existing = reinterpret_cast<DeleteProjectConfirmModal *>(storage_);
    existing->~DeleteProjectConfirmModal();
    inUse_ = false;
  }
  inUse_ = true;
  return new (storage_) DeleteProjectConfirmModal(view, name);
}

DeleteProjectConfirmModal::DeleteProjectConfirmModal(View &view,
                                                     const char *projectName)
    : ModalView(view), projectLine_("Delete \"") {
  projectLine_.append(projectName);
  projectLine_.append("\"");
}

DeleteProjectConfirmModal::~DeleteProjectConfirmModal() {}

void DeleteProjectConfirmModal::Destroy() {
  this->~DeleteProjectConfirmModal();
  inUse_ = false;
}

void DeleteProjectConfirmModal::UpdateProgress_() {
  const bool comboHeld = (currentMask_ & DELETE_HOLD_MASK) == DELETE_HOLD_MASK;
  const unsigned long now = System::GetInstance()->GetClock();

  if (!comboHeld) {
    if (holdingCombo_ || (holdProgressMs_ != 0)) {
      holdingCombo_ = false;
      holdProgressMs_ = 0;
      isDirty_ = true;
      static_cast<AppWindow &>(w_).SetDirty();
    }
    return;
  }

  if (!holdingCombo_) {
    holdingCombo_ = true;
    holdStartMs_ = now;
    holdProgressMs_ = 0;
    isDirty_ = true;
    static_cast<AppWindow &>(w_).SetDirty();
    return;
  }

  unsigned long elapsed = now - holdStartMs_;
  if (elapsed > DELETE_HOLD_DURATION_MS) {
    elapsed = DELETE_HOLD_DURATION_MS;
  }

  if (holdProgressMs_ != elapsed) {
    holdProgressMs_ = elapsed;
    isDirty_ = true;
    static_cast<AppWindow &>(w_).SetDirty();
  }

  if (holdProgressMs_ >= DELETE_HOLD_DURATION_MS) {
    EndModal(MBL_YES);
  }
}

void DeleteProjectConfirmModal::AnimationUpdate() { UpdateProgress_(); }

void DeleteProjectConfirmModal::ProcessButtonMask(unsigned short mask,
                                                  bool pressed) {
  currentMask_ = mask;

  if (pressed && (mask & EPBM_ENTER)) {
    EndModal(MBL_CANCEL);
    return;
  }

  UpdateProgress_();
}

void DeleteProjectConfirmModal::DrawView() {
  SetWindow(26, 6);

  GUITextProperties props;
  SetColor(CD_NORMAL);
  props.invert_ = false;

  const int projectLineX = (26 - projectLine_.size()) / 2;
  DrawString(projectLineX, 0, projectLine_.c_str(), props);
  DrawString(0, 1, "Press & hold ALT+PLAY+EDIT", props);

  if (holdingCombo_ || holdProgressMs_ > 0) {
    progressBar_t progressBar;
    fillProgressBar(holdProgressMs_, DELETE_HOLD_DURATION_MS, &progressBar);
    DrawString((26 - 12) / 2, 3, progressBar, props);
  }

  const char *cancelButton = "[ Cancel ]";
  SetColor(CD_HILITE2);
  props.invert_ = true;
  DrawString((26 - strlen(cancelButton)) / 2, 5, cancelButton, props);
}

static void LoadProjectCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {

    // User accepted losing changes; clear autosave for the current project.
    ((SelectProjectView &)v).ClearAutoSave();

    ((SelectProjectView &)v).LoadProject();
  }
}

static void DeleteProjectCallback(View &v, ModalView &dialog) {
  if (dialog.GetReturnCode() == MBL_YES) {
    SelectProjectView &view = (SelectProjectView &)v;

    PersistencyService *ps = PersistencyService::GetInstance();
    char buffer[MAX_PROJECT_NAME_LENGTH + 1];
    view.getHighlightedProjectName(buffer);
    if (!ps->DeleteProject(buffer)) {
      MessageBox *mb =
          MessageBox::Create(view, "Project could not be deleted", MBBF_OK);
      view.DoModal(mb);
      return;
    }

    // reload list
    view.setCurrentFolder();
  }
}

SelectProjectView::SelectProjectView(GUIWindow &w, ViewData *viewData)
    : ScreenView(w, viewData), fileEntries_(GetProjectBrowserFileEntries()) {}

SelectProjectView::~SelectProjectView() {}

void SelectProjectView::Reset() {
  topIndex_ = 0;
  currentIndex_ = 0;
  selection_[0] = '\0';
  currentDir_.clear();
  fileEntries_.clear();
}

void SelectProjectView::DrawView() {
  Clear();

  GUITextProperties props;

  // Draw title
  setTitle(etl::string<23>("Browse Projects"));

  // Draw projects
  int x = 1;
  int y = 1;

  auto var = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = var->GetString();
  const char *currentProject = projectName.c_str();

  for (size_t i = topIndex_;
       i < topIndex_ + LIST_PAGE_SIZE && (i < fileEntries_.size()); i++) {
    if (i == currentIndex_) {
      SetColor(CD_HILITE2);
      props.invert_ = true;
    } else {
      SetColor(CD_NORMAL);
      props.invert_ = false;
    }

    char buffer[MAX_PROJECT_NAME_LENGTH + 1];
    memset(buffer, '\0', sizeof(buffer));
    const auto &entry = fileEntries_[i];

    if (entry.name.size() <= MAX_PROJECT_NAME_LENGTH) {
      std::strncpy(buffer, entry.name.c_str(), sizeof(buffer) - 1);
    }
    // SDFat lib doesn't truncate if filename longer than buffer as per docs but
    // instead returns empty string in buffer
    if (strlen(buffer) == 0) {
      strcpy(buffer, INVALID_PROJECT_NAME);
    }

    if (strcmp(buffer, currentProject) == 0) {
      // mark currently loaded project
      DrawRegionString(TEXT_REGION_BODY, x - 1, y, "*", props);
    }

    DrawRegionString(TEXT_REGION_BODY, x, y, buffer, props);
    y += 1;
  };

  // load/delete selection buttons
  const char *buttons[numButtons_] = {"Load", "Delete"};

  int bx = x;

  for (int n = 0; n < numButtons_; n++) {
    bool selected = selectedButton_ == n;
    props.invert_ = selected;
    SetColor(selected ? CD_HILITE2 : CD_HILITE1);
    DrawRegionString(TEXT_REGION_BODY, x, SCREEN_HEIGHT - 2, buttons[n], props);

    x += 2 + strlen(buttons[n]);
  }

  // scroll bar
  DrawScrollBar();
};

void SelectProjectView::DrawScrollBar() {
  int totalItems = fileEntries_.size();
  if (totalItems <= LIST_PAGE_SIZE) {
    return; // no scrollbar needed
  }

  GUITextProperties props;
  GUITextProperties inv;
  inv.invert_ = true;
  SetColor(CD_NORMAL);

  // Thumb size represents the ratio of visible items to total items
  int thumbSize = std::max(1, (LIST_PAGE_SIZE * LIST_PAGE_SIZE) / totalItems);

  // Thumb position: map topIndex (0 to maxScroll) onto available scrollbar
  // space
  int maxScroll = totalItems - LIST_PAGE_SIZE;
  int availableSpace = LIST_PAGE_SIZE - thumbSize;
  int thumbPos = (topIndex_ * availableSpace) / maxScroll;

  Trace::Error("%d total, %d thumb size, %d maxScroll, %d thumbPos", totalItems,
               thumbSize, maxScroll, thumbPos);
  for (int y = 0; y < LIST_PAGE_SIZE; y++) {
    bool thumb = y >= thumbPos && y < thumbPos + thumbSize;
    DrawRegionString(TEXT_REGION_BODY, SCREEN_WIDTH - 1, 1 + y,
                     thumb ? char_block_full_s : char_border_single_vertical_s,
                     props);
  }
}

void SelectProjectView::OnPlayerUpdate(PlayerEventType,
                                       unsigned int currentTick){};

void SelectProjectView::OnFocus() {
  selectedButton_ = 0; // Always default to "Load" when entering this view.
  setCurrentFolder();
};

void SelectProjectView::ProcessButtonMask(unsigned short mask, bool pressed) {
  if (!pressed)
    return;

  if (mask & EPBM_EDIT) {
    // EDIT+ENTER -> hotkey to delete
    if (mask & EPBM_ENTER)
      AttemptDeletingSelectedProject();
    if (mask & EPBM_UP)
      warpToNextProject(true, true);
    if (mask & EPBM_DOWN)
      warpToNextProject(false, true);
  } else {
    // A modifier
    if (mask & EPBM_ENTER) {
      switch (selectedButton_) {
      case 0:
        // load project
        AttemptLoadingProject();
        break;
      case 1:
        AttemptDeletingSelectedProject();
        break;
      }
    } else {
      // R Modifier
      if ((mask & EPBM_NAV) && (mask & EPBM_LEFT)) {
        // Go to back "left" to Project Screen
        ViewType vt = VT_PROJECT;
        ViewEvent ve(VET_SWITCH_VIEW, &vt);
        SetChanged();
        NotifyObservers(&ve);
        return;
      } else {
        // No modifier
        if (mask == EPBM_UP)
          warpToNextProject(true);
        if (mask == EPBM_DOWN)
          warpToNextProject(false);
        if (mask == EPBM_LEFT)
          SelectButton(-1);
        if (mask == EPBM_RIGHT)
          SelectButton(1);
      }
    }
  }
}

void SelectProjectView::warpToNextProject(bool goUp, bool page) {
  BrowserPaging::Warp(currentIndex_, topIndex_, fileEntries_.size(),
                      LIST_PAGE_SIZE, goUp, page);
  isDirty_ = true;
}

void SelectProjectView::setCurrentFolder() {
  auto fs = FileSystem::GetInstance();
  currentDir_ = PROJECTS_DIR;
  fileEntries_.clear();
  const bool listed =
      fs->ListDirectory(currentDir_.c_str(), &fileEntries_, "", true);
  const bool truncated = isDirectoryListTruncated(fs);
  if (!listed && !truncated) {
    topIndex_ = 0;
    currentIndex_ = 0;
    isDirty_ = true;
    return;
  }
  if (truncated) {
    Trace::Error("SELECTPROJECTVIEW: Truncated listing for %s",
                 currentDir_.c_str());
    showDirectoryListTruncatedDialog(*this);
  }

  // Filter out "." and ".." along with the hidden default project entry
  for (auto it = fileEntries_.begin(); it != fileEntries_.end();) {
    const bool isUntitled = (it->name == UNNAMED_PROJECT_NAME);

    if (isUntitled) {
      Trace::Log("SELECTPROJECTVIEW", "skipping untitled project on Index:%d",
                 static_cast<int>(it - fileEntries_.begin()));
      it = fileEntries_.erase(it);
    } else {
      // Only show directories that contain ptsav.dat (Advance project marker).
      // This prevents pico projects from being opened on the Advance.
      FileSystemPath::PathString projectPath;
      if (!FileSystemPath::JoinPath(PROJECTS_DIR, it->name.c_str(),
                                    projectPath)) {
        Trace::Log("SELECTPROJECTVIEW",
                   "skipping project with unresolvable path on Index:%d",
                   static_cast<int>(it - fileEntries_.begin()));
        it = fileEntries_.erase(it);
      } else {
        FileSystemPath::PathString savePath;
        if (!FileSystemPath::JoinPath(projectPath.c_str(), PROJECT_DATA_FILE,
                                      savePath)) {
          Trace::Log("SELECTPROJECTVIEW",
                     "skipping project with unresolvable save path on Index:%d",
                     static_cast<int>(it - fileEntries_.begin()));
          it = fileEntries_.erase(it);
        } else if (!fs->exists(savePath.c_str())) {
          Trace::Log("SELECTPROJECTVIEW",
                     "skipping pico project (no ptsav.dat): %s on Index:%d",
                     it->name.c_str(),
                     static_cast<int>(it - fileEntries_.begin()));
          it = fileEntries_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  SortBrowserEntries(fileEntries_);

  // reset & redraw screen
  if (!fileEntries_.empty()) {
    currentIndex_ = std::min(currentIndex_, fileEntries_.size() - 1);
  }
  topIndex_ = 0;
  currentIndex_ = 0;
  isDirty_ = true;
}

void SelectProjectView::getSelectedProjectName(char *name) {
  strcpy(name, selection_);
}

void SelectProjectView::getHighlightedProjectName(char *name) {
  name[0] = '\0';
  if (currentIndex_ >= fileEntries_.size()) {
    return;
  }

  const auto &entry = fileEntries_[currentIndex_];
  if (entry.name.size() > MAX_PROJECT_NAME_LENGTH) {
    return;
  }
  std::strncpy(name, entry.name.c_str(), MAX_PROJECT_NAME_LENGTH);
  name[MAX_PROJECT_NAME_LENGTH] = '\0';
}

void SelectProjectView::SelectButton(int direction) {
  selectedButton_ = (numButtons_ + selectedButton_ + direction) % numButtons_;
  isDirty_ = true;
}

void SelectProjectView::LoadProject() {
  if (currentIndex_ >= fileEntries_.size()) {
    return;
  }

  // all subdirs directly inside /project are expected to be projects
  const auto &entry = fileEntries_[currentIndex_];
  if (entry.name.size() > MAX_PROJECT_NAME_LENGTH) {
    Trace::Log("SELECTPROJECTVIEW",
               "skipping too long project name on Index:%d",
               static_cast<int>(currentIndex_));
    return;
  }
  std::strncpy(selection_, entry.name.c_str(), MAX_PROJECT_NAME_LENGTH);
  selection_[MAX_PROJECT_NAME_LENGTH] = '\0';

  Trace::Log("SELECTPROJECTVIEW", "Select Project:%s", selection_);

  ViewEvent ve(VET_LOAD_PROJECT, selection_);
  SetChanged();
  NotifyObservers(&ve);
}

bool SelectProjectView::WarnPlayerRunning() {
  if (Player::GetInstance()->IsRunning()) {
    MessageBox *mb = MessageBox::Create(*this, "Not while running!", MBBF_OK);
    DoModal(mb);
    return true;
  }
  return false;
}

bool SelectProjectView::SelectionIsCurrentProject() {
  char selected[MAX_PROJECT_NAME_LENGTH + 1];
  getHighlightedProjectName(selected);

  auto var = viewData_->project_->FindVariable(FourCC::VarProjectName);
  etl::string<MAX_PROJECT_NAME_LENGTH> projectName = var->GetString();
  const char *current = projectName.c_str();

  return strcmp(current, selected) == 0;
}

void SelectProjectView::AttemptDeletingSelectedProject() {
  if (currentIndex_ >= fileEntries_.size()) {
    return;
  }

  if (WarnPlayerRunning()) {
    return;
  }

  if (SelectionIsCurrentProject()) {
    MessageBox *mb = MessageBox::Create(*this, "Cannot delete the active",
                                        "project.", MBBF_OK);
    DoModal(mb);
    return;
  }

  char selected[MAX_PROJECT_NAME_LENGTH + 1];
  getHighlightedProjectName(selected);

  ModalView *mb = DeleteProjectConfirmModal::Create(*this, selected);
  DoModal(mb, ModalViewCallback::create<&DeleteProjectCallback>());
}

void SelectProjectView::AttemptLoadingProject() {
  if (WarnPlayerRunning()) {
    return;
  }

  MessageBox *mb = MessageBox::Create(*this, "Load song & lose changes ?",
                                      MBBF_YES | MBBF_NO);
  DoModal(mb, ModalViewCallback::create<&LoadProjectCallback>());
}

void SelectProjectView::ClearAutoSave() {
  PersistencyService *ps = PersistencyService::GetInstance();
  // Clear the autosave for the original directory to prevent orphaning it
  // if the user had modified the project name without saving.
  const auto &originalName = AppWindow::GetInstance()->originalProjectName_;
  if (!originalName.empty()) {
    if (!ps->ClearAutosave(originalName.c_str())) {
      Trace::Log("SELECTPROJECTVIEW",
                 "Autosave clear failed or missing for project: %s",
                 originalName.c_str());
    }
  }
}
