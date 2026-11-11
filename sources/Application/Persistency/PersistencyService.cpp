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

#include "PersistencyService.h"
#include "Application/Instruments/MSamplerInstrument.h"
#include "Foundation/Services/ServiceRegistry.h"

#include "Application/Utils/DrawUtils.h"
#include "Foundation/Types/Types.h"
#include "Persistent.h"
#include "System/Console/Trace.h"
#include "System/FileSystem/FileSystem.h"
#include "System/FileSystem/PathUtils.h"
#include "System/io/Status.h"
#include <cstring>
#include <new>

#define PROJECT_STATE_FILE "/.current"
#define MAX_DELETE_DEPTH 3

namespace {

using PersistencyFileEntries =
    etl::vector<FileSystemEntry, MAX_FILE_INDEX_SIZE>;

#if defined(HOST_TEST)
#define PERSISTENCY_FILE_ENTRIES_ATTRIBUTES __attribute__((aligned(32)))
#else
#define PERSISTENCY_FILE_ENTRIES_ATTRIBUTES                                    \
  __attribute__((section(".SDRAM1_NC"))) __attribute__((aligned(32)))
#endif

alignas(
    PersistencyFileEntries) static unsigned char persistencyFileEntriesBuffer
    [sizeof(PersistencyFileEntries)] PERSISTENCY_FILE_ENTRIES_ATTRIBUTES;

PersistencyFileEntries &GetPersistencyFileEntries() {
  static PersistencyFileEntries *entries = nullptr;
  if (entries == nullptr) {
    entries = new (persistencyFileEntriesBuffer) PersistencyFileEntries();
  }
  return *entries;
}

#undef PERSISTENCY_FILE_ENTRIES_ATTRIBUTES

bool resolveInstrumentPath(const char *name,
                           FileSystemPath::PathString &resolvedPath) {
  if (name == nullptr || name[0] == '\0') {
    return false;
  }

  if (FileSystemPath::IsAbsolutePath(name)) {
    return FileSystemPath::NormalizePath(name, resolvedPath);
  }

  return FileSystemPath::JoinPath(INSTRUMENTS_DIR, name, resolvedPath);
}

bool extractInstrumentLeafName(
    const char *path, etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> &leafName) {
  FileSystemPath::PathString baseName;
  if (!FileSystemPath::BaseName(path, baseName)) {
    leafName.clear();
    return false;
  }

  leafName = baseName.c_str();
  return !leafName.is_truncated() && !leafName.empty();
}

} // namespace

PersistencyService::PersistencyService()
    : Service(FourCC::ServicePersistency),
      fileEntries_(GetPersistencyFileEntries()) {}

PersistencyResult PersistencyService::CreateProject() {
  Trace::Log("APPLICATION", "create new project");

  if (Exists(UNNAMED_PROJECT_NAME) && !PurgeUnnamedProject()) {
    Trace::Error("PERSISTENCYSERVICE: Could not clear unnamed project");
    return PERSIST_ERROR;
  }

  if (CreateProjectDirs_(UNNAMED_PROJECT_NAME) != PERSIST_SAVED) {
    Trace::Error("PERSISTENCYSERVICE: Could not create unnamed project");
    return PERSIST_ERROR;
  }

  return PersistencyService::GetInstance()->Save(UNNAMED_PROJECT_NAME, "",
                                                 false);
};

bool PersistencyService::PurgeUnnamedProject() {
  return DeleteProject(UNNAMED_PROJECT_NAME);
};

bool PersistencyService::DeleteProject(const char *projectName) {
  auto fs = FileSystem::GetInstance();
  FileSystemPath::PathString projectPath;

  Trace::Debug("PERSISTENCYSERVICE", "Deleting project: %s", projectName);

  if (!FileSystemPath::JoinPath(PROJECTS_DIR, projectName, projectPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not resolve project path: %s",
                 projectName);
    return false;
  }

  if (!fs->exists(projectPath.c_str())) {
    Trace::Error("PERSISTENCYSERVICE: Project does not exist: %s",
                 projectPath.c_str());
    return false;
  }

  if (!DeleteDirectoryTree_(projectPath.c_str(), 0)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete project contents");
    return false;
  }

  return true;
}

bool PersistencyService::DeleteDirectoryContents_(const char *directoryPath,
                                                  uint8_t depth) {
  auto fs = FileSystem::GetInstance();
  if (depth > MAX_DELETE_DEPTH) {
    Trace::Error("PERSISTENCYSERVICE: delete depth exceeded");
    return false;
  }

  while (true) {
    fileEntries_.clear();
    if (!fs->ListDirectory(directoryPath, &fileEntries_, "", false, true)) {
      Trace::Error("PERSISTENCYSERVICE: Could not list directory: %s",
                   directoryPath);
      return false;
    }

    if (fileEntries_.empty()) {
      return true;
    }

    // Recursive deletes reuse fileEntries_, so only keep a local copy of the
    // next entry to process and refresh the directory listing afterwards.
    const auto entry = fileEntries_[0];
    FileSystemPath::PathString entryPath;
    if (!FileSystemPath::JoinPath(directoryPath, entry.name.c_str(),
                                  entryPath)) {
      Trace::Error("PERSISTENCYSERVICE: Could not resolve path for %s in %s",
                   entry.name.c_str(), directoryPath);
      return false;
    }

    if (entry.type == PFT_FILE) {
      if (!fs->DeleteFile(entryPath.c_str())) {
        Trace::Error("PERSISTENCYSERVICE: Could not delete file: %s",
                     entryPath.c_str());
        return false;
      }
      continue;
    }

    if (entry.type == PFT_DIR) {
      if (!DeleteDirectoryTree_(entryPath.c_str(), depth + 1)) {
        return false;
      }
      continue;
    }

    Trace::Error("PERSISTENCYSERVICE: Unknown file type for %s",
                 entryPath.c_str());
    return false;
  }
}

bool PersistencyService::DeleteDirectoryTree_(const char *directoryPath,
                                              uint8_t depth) {
  auto fs = FileSystem::GetInstance();

  if (depth > MAX_DELETE_DEPTH) {
    Trace::Error("PERSISTENCYSERVICE: delete depth exceeded");
    return false;
  }

  if (!DeleteDirectoryContents_(directoryPath, depth)) {
    return false;
  }

  if (!fs->DeleteDir(directoryPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not delete dir: %s", directoryPath);
    return false;
  }

  return true;
}

PersistencyResult
PersistencyService::CreateProjectDirs_(const char *projectName) {
  auto fs = FileSystem::GetInstance();

  // create samples sub dir as well as project dir containing it
  etl::vector<const char *, 3> segments = {PROJECTS_DIR, projectName,
                                           PROJECT_SAMPLES_DIR};
  CreatePath(pathBufferA, segments);

  auto result = fs->makeDir(pathBufferA.c_str(), true);
  Trace::Log("PERSISTENCYSERVICE", "created samples subdir: %s [%b]",
             pathBufferA.c_str(), result);

  return result ? PersistencyResult::PERSIST_SAVED
                : PersistencyResult::PERSIST_ERROR;
}

PersistencyResult PersistencyService::Save(const char *projectName,
                                           const char *oldProjectName,
                                           bool saveAs) {
  auto fs = FileSystem::GetInstance();

  if (saveAs && !Exists(projectName)) {
    FileSystemPath::PathString oldProjectPath;
    FileSystemPath::PathString oldSamplesPath;
    CreateProjectDirs_(projectName);

    if (!FileSystemPath::JoinPath(PROJECTS_DIR, oldProjectName,
                                  oldProjectPath) ||
        !FileSystemPath::JoinPath(oldProjectPath.c_str(), PROJECT_SAMPLES_DIR,
                                  oldSamplesPath)) {
      Trace::Error("PERSISTENCYSERVICE: Could not resolve samples path for %s",
                   oldProjectName);
      return PERSIST_ERROR;
    }

    Trace::Debug("get list of samples to copy from old project: %s",
                 oldSamplesPath.c_str());

    if (fs->exists(oldSamplesPath.c_str())) {
      fileEntries_.clear();
      if (!fs->ListDirectory(oldSamplesPath.c_str(), &fileEntries_, ".wav",
                             false)) {
        Trace::Error("PERSISTENCYSERVICE: Could not list samples dir: %s",
                     oldSamplesPath.c_str());
        return PERSIST_ERROR;
      }

      uint32_t sampleCount = 0;
      for (const auto &entry : fileEntries_) {
        if (entry.type == PFT_FILE) {
          sampleCount++;
        }
      }

      uint32_t sampleIndex = 0;
      for (const auto &entry : fileEntries_) {
        if (entry.type != PFT_FILE) {
          continue;
        }
        sampleIndex++;

        etl::vector<const char *, 4> filePathSegments = {
            PROJECTS_DIR, oldProjectName, PROJECT_SAMPLES_DIR,
            entry.name.c_str()};
        CreatePath(pathBufferA, filePathSegments);

        filePathSegments = {PROJECTS_DIR, projectName, PROJECT_SAMPLES_DIR,
                            entry.name.c_str()};
        CreatePath(pathBufferB, filePathSegments);

        progressBar_t progressBar;
        fillProgressBar(sampleIndex, sampleCount, &progressBar);
        Status::SetMultiLine("Saving project\nCopying sample\n%.19s\n%s",
                             entry.name.c_str(), progressBar);

        if (!fs->CopyFile(pathBufferA.c_str(), pathBufferB.c_str())) {
          Trace::Error("PERSISTENCYSERVICE: Could not copy %s to %s",
                       pathBufferA.c_str(), pathBufferB.c_str());
          return PERSIST_ERROR;
        }
      }
    }
  }
  if (saveAs) {
    Status::SetMultiLine("Saving project\nWriting data");
  }
  return SaveProjectData(projectName, false);
};

PersistencyResult
PersistencyService::AutoSaveProjectData(const char *projectName) {
  return SaveProjectData(projectName, true);
};

PersistencyResult PersistencyService::SaveProjectData(const char *projectName,
                                                      bool autosave) {

  const char *filename = autosave ? AUTO_SAVE_FILENAME : PROJECT_DATA_FILE;

  etl::vector<const char *, 3> segments = {PROJECTS_DIR, projectName, filename};
  CreatePath(pathBufferA, segments);

  auto fs = FileSystem::GetInstance();
  auto fp = fs->Open(pathBufferA.c_str(), "w");
  if (!fp) {
    Trace::Error("PERSISTENCYSERVICE: Could not open file for writing: %s",
                 pathBufferA.c_str());
    return PERSIST_ERROR;
  }
  Trace::Log("PERSISTENCYSERVICE", "Opened Proj File: %s", pathBufferA.c_str());
  tinyxml2::XMLPrinter printer(fp.get());

  printer.OpenElement("PICOTRACKER");

  // Loop on all registered persistable subservices
  for (auto *sub : SubServices()) {
    auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
    currentItem->Save(&printer);
  }

  printer.CloseElement();

  // if we are doing an explicit save (ie nto a autosave), then we need to
  // delete the existing autosave file so that this explicit save will be loaded
  // in case subsequent autosave has changes the user doesn't want to keep
  if (!autosave) {
    if (!ClearAutosave(projectName)) {
      Trace::Log("PERSISTENCYSERVICE", "Error Deleting Autosave File: %s",
                 pathBufferA.c_str());
      // the autosave file may not have been created yet, eg. if this is a new
      // project or a project beign "saved as" so just keep going
    }
    Trace::Log("PERSISTENCYSERVICE", "Deleted Autosave File: %s",
               pathBufferA.c_str());
  }

  return PERSIST_SAVED;
};

// return true if existing proj with the given name already exists
bool PersistencyService::Exists(const char *projectName) {
  etl::string<128> projectFilePath(PROJECTS_DIR);
  projectFilePath.append("/");
  projectFilePath.append(projectName);

  auto fs = FileSystem::GetInstance();
  return fs->exists(projectFilePath.c_str());
}

PersistencyResult PersistencyService::Load(const char *projectName) {
  // first check if autosave exists
  etl::string<128> autoSavePath(PROJECTS_DIR);
  autoSavePath.append("/");
  autoSavePath.append(projectName);
  autoSavePath.append("/");
  autoSavePath.append(AUTO_SAVE_FILENAME);

  auto fs = FileSystem::GetInstance();
  bool useAutosave = (fs->exists(autoSavePath.c_str()));

  Trace::Log("PERSISTENCYSERVICE", "using autosave: %b", useAutosave);
  // if autosave exists, then we load it instead of the normal project file
  const char *filename = useAutosave ? AUTO_SAVE_FILENAME : PROJECT_DATA_FILE;

  etl::string<128> projectFilePath(PROJECTS_DIR);
  projectFilePath.append("/");
  projectFilePath.append(projectName);
  projectFilePath.append("/");
  projectFilePath.append(filename);

  PersistencyDocument doc;
  if (!doc.Load(projectFilePath.c_str()))
    return PERSIST_LOAD_FAILED;

  bool elem = doc.FirstChild(); // advance to first child
  if (!elem || strcmp(doc.ElemName(), "PICOTRACKER")) {
    Trace::Error("could not find master node");
    return PERSIST_LOAD_FAILED;
  }

  elem = doc.FirstChild();
  while (elem) {
    for (auto *sub : SubServices()) {
      auto *currentItem = static_cast<Persistent *>(static_cast<void *>(sub));
      if (currentItem->Restore(&doc)) {
        break;
      }
    }
    elem = doc.NextSibling();
  }
  if (doc.HadError()) {
    Trace::Error("XML errors detected while loading project '%s'", projectName);
    return PERSIST_LOAD_FAILED;
  }
  return PERSIST_LOADED;
};

PersistencyResult
PersistencyService::LoadCurrentProjectName(char *projectName) {
  auto fs = FileSystem::GetInstance();
  if (fs->exists(PROJECT_STATE_FILE)) {
    auto current = fs->Open(PROJECT_STATE_FILE, "r");
    if (!current) {
      Trace::Error("PERSISTENCYSERVICE: Could not open project state file");
      return PERSIST_LOAD_FAILED;
    }
    int len = current->Read(projectName, MAX_PROJECT_NAME_LENGTH);
    projectName[len] = '\0';
    Trace::Log("APPLICATION", "read [%d] load proj name: %s", len, projectName);
    if (Exists(projectName)) {
      return PERSIST_LOADED;
    } else {
      Trace::Log("APPLICATION",
                 "Project '%s' not found, loading untitled project",
                 projectName);
      fs->DeleteFile(PROJECT_STATE_FILE);
      strcpy(projectName, UNNAMED_PROJECT_NAME);
      return PERSIST_LOADED;
    }
  } else {
    return PERSIST_LOAD_FAILED;
  }
}

PersistencyResult
PersistencyService::SaveProjectState(const char *projectName) {
  auto fs = FileSystem::GetInstance();
  auto current = fs->Open(PROJECT_STATE_FILE, "w");
  if (!current) {
    return PERSIST_ERROR;
  }
  current->Write(projectName, 1, strlen(projectName));
  return PERSIST_SAVED;
}

void PersistencyService::CreatePath(
    etl::istring &path, const etl::ivector<const char *> &segments) {
  // concatenate path segments into a single path
  path.clear();
  // iterate over segments and concatenate using iterator
  for (auto it = segments.begin(); it != segments.end(); ++it) {
    path.append(*it);
    if (it != segments.end() - 1) {
      path.append("/");
    }
  }
}

bool PersistencyService::ClearAutosave(const char *projectName) {
  auto fs = FileSystem::GetInstance();
  etl::vector<const char *, 3> segments = {PROJECTS_DIR, projectName,
                                           AUTO_SAVE_FILENAME};
  CreatePath(pathBufferA, segments);
  // TODO: check if file exists before deleting and only return false if it does
  // exist and deleting fails but this can only be done once Open() return
  // values are improved and we can implement a Exists() function on top of it
  return fs->DeleteFile(pathBufferA.c_str());
}

PersistencyResult PersistencyService::ExportInstrument(
    I_Instrument *instrument, etl::string<MAX_INSTRUMENT_NAME_LENGTH> name,
    bool overwrite) {
  auto fs = FileSystem::GetInstance();

  // Add .pti extension to the filename
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> filename = name;
  filename.append(INSTRUMENT_FILE_EXTENSION);

  etl::vector<const char *, 2> segments = {INSTRUMENTS_DIR, filename.c_str()};
  CreatePath(pathBufferA, segments);

  // check if file already exists
  if (fs->exists(pathBufferA.c_str())) {
    if (!overwrite) {
      return PERSIST_EXISTS;
    }
    // Delete the existing file if overwrite is true
    if (!fs->DeleteFile(pathBufferA.c_str())) {
      Trace::Error("PERSISTENCYSERVICE: Failed to delete existing file: %s",
                   pathBufferA.c_str());
      return PERSIST_ERROR;
    }
  }

  auto fp = fs->Open(pathBufferA.c_str(), "w");
  if (!fp) {
    Trace::Error("PERSISTENCYSERVICE: Could not open file for writing: %s",
                 pathBufferA.c_str());
    return PERSIST_ERROR;
  }

  tinyxml2::XMLPrinter printer(fp.get());

  // Use the instrument's Persistent interface to save its data
  instrument->Save(&printer);

  return PERSIST_SAVED;
}

InstrumentType PersistencyService::DetectInstrumentType(const char *name) {
  if (name != nullptr) {
    const char *extension = std::strrchr(name, '.');
    if (extension != nullptr && !strcasecmp(extension, ".dspreset")) {
      return IT_MSAMPLER;
    }
  }
  FileSystemPath::PathString instrumentPath;
  if (!resolveInstrumentPath(name, instrumentPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not resolve instrument path: %s",
                 name ? name : "(null)");
    return IT_NONE;
  }

  // Load the XML document
  PersistencyDocument doc;
  if (!doc.Load(instrumentPath.c_str())) {
    Trace::Error("PERSISTENCYSERVICE: Could not parse XML from file: %s",
                 instrumentPath.c_str());
    return IT_NONE;
  }

  // Find the INSTRUMENT element
  bool elem = doc.FirstChild();
  if (!elem || strcmp(doc.ElemName(), "INSTRUMENT")) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not find INSTRUMENT node in file: %s",
        instrumentPath.c_str());
    return IT_NONE;
  }

  // Check for TYPE attribute in the INSTRUMENT element
  InstrumentType importedType = IT_NONE;
  bool hasAttr = doc.NextAttribute();
  while (hasAttr) {
    if (!strcasecmp(doc.attrname_, "TYPE")) {
      Trace::Log("PERSISTENCYSERVICE", "Found instrument type in XML: %s",
                 doc.attrval_);

      // Map the type string to InstrumentType enum
      for (int i = 0; i < IT_LAST; i++) {
        if (!strcasecmp(doc.attrval_, InstrumentTypeNames[i])) {
          importedType = static_cast<InstrumentType>(i);
          Trace::Log("PERSISTENCYSERVICE", "Mapped to instrument type: %d",
                     importedType);
          break;
        }
      }
    }
    hasAttr = doc.NextAttribute();
  }
  return importedType;
}

PersistencyResult PersistencyService::ImportInstrument(I_Instrument *instrument,
                                                       const char *name) {
  FileSystemPath::PathString instrumentPath;
  if (!resolveInstrumentPath(name, instrumentPath)) {
    Trace::Error("PERSISTENCYSERVICE: Could not resolve instrument path: %s",
                 name ? name : "(null)");
    return PERSIST_ERROR;
  }

  if (instrument != nullptr && instrument->GetType() == IT_MSAMPLER) {
    auto *msampler = static_cast<MSamplerInstrument *>(instrument);
    return msampler->LoadPreset(instrumentPath.c_str()) ? PERSIST_LOADED
                                                        : PERSIST_ERROR;
  }

  // Load the XML document
  PersistencyDocument doc;
  if (!doc.Load(instrumentPath.c_str())) {
    Trace::Error("PERSISTENCYSERVICE: Could not parse XML from file: %s",
                 instrumentPath.c_str());
    return PERSIST_ERROR;
  }

  // Find the INSTRUMENT element
  bool elem = doc.FirstChild();
  if (!elem || strcmp(doc.ElemName(), "INSTRUMENT")) {
    Trace::Error(
        "PERSISTENCYSERVICE: Could not find INSTRUMENT node in file: %s",
        instrumentPath.c_str());
    return PERSIST_ERROR;
  }

  // Check for VERSION and TYPE attributes in the INSTRUMENT element
  InstrumentType importedType = IT_NONE;
  bool hasAttr = doc.NextAttribute();
  etl::string<32> versionInfo;

  while (hasAttr) {
    if (!strcasecmp(doc.attrname_, "VERSION")) {
      // Store version information for logging
      versionInfo = doc.attrval_;
      Trace::Log("PERSISTENCYSERVICE", "Instrument file version: %s",
                 doc.attrval_);
    } else if (!strcasecmp(doc.attrname_, "TYPE")) {
      Trace::Log("PERSISTENCYSERVICE", "Found instrument type in XML: %s",
                 doc.attrval_);

      // Map the type string to InstrumentType enum
      for (int i = 0; i < IT_LAST; i++) {
        if (!strcasecmp(doc.attrval_, InstrumentTypeNames[i])) {
          importedType = static_cast<InstrumentType>(i);
          Trace::Log("PERSISTENCYSERVICE", "Mapped to instrument type: %d",
                     importedType);
          break;
        }
      }
    }
    hasAttr = doc.NextAttribute();
  }

  // Log the complete version info if available
  if (versionInfo.length() > 0) {
    Trace::Log("PERSISTENCYSERVICE",
               "Instrument created with firmware version: %s",
               versionInfo.c_str());
  }

  // If we found a valid instrument type and it doesn't match the current
  // instrument
  if (importedType != IT_NONE && importedType != instrument->GetType()) {
    Trace::Log("PERSISTENCYSERVICE",
               "Current instrument type: %d, imported type: %d",
               instrument->GetType(), importedType);

    // We can't directly change the instrument type, so we'll need to
    // create a new instrument of the correct type and copy parameters later
    Trace::Log("PERSISTENCYSERVICE",
               "Instrument type mismatch, will convert after loading");
  }

  // Restore the instrument content
  if (!instrument->Restore(&doc)) {
    Trace::Error(
        "PERSISTENCYSERVICE: Failed to restore instrument from file: %s",
        instrumentPath.c_str());
    return PERSIST_ERROR;
  }

  // Extract instrument name from filename (minus .pti extension)
  etl::string<MAX_INSTRUMENT_NAME_LENGTH> instrumentName;
  etl::string<MAX_INSTRUMENT_FILENAME_LENGTH> leafName;
  if (!extractInstrumentLeafName(instrumentPath.c_str(), leafName)) {
    Trace::Error("PERSISTENCYSERVICE: Failed to extract filename from %s",
                 instrumentPath.c_str());
    return PERSIST_ERROR;
  }

  const char *dotPos = strrchr(leafName.c_str(), '.');
  if (dotPos) {
    // Calculate the length of the name without extension
    size_t nameLength = dotPos - leafName.c_str();
    // Copy only up to MAX_INSTRUMENT_NAME_LENGTH characters
    nameLength = nameLength < MAX_INSTRUMENT_NAME_LENGTH
                     ? nameLength
                     : MAX_INSTRUMENT_NAME_LENGTH - 1;
    instrumentName.assign(leafName.c_str(), nameLength);
  } else {
    // No extension found, use the whole name (up to MAX_INSTRUMENT_NAME_LENGTH)
    instrumentName.assign(leafName.c_str(),
                          leafName.size() < MAX_INSTRUMENT_NAME_LENGTH
                              ? leafName.size()
                              : MAX_INSTRUMENT_NAME_LENGTH - 1);
  }

  // Set the instrument name
  Variable *nameVar = instrument->FindVariable(FourCC::InstrumentName);
  if (nameVar) {
    nameVar->SetString(instrumentName.c_str());
  }

  // Mark the instrument as changed to trigger UI updates
  instrument->SetChanged();
  instrument->NotifyObservers();

  Trace::Log("PERSISTENCYSERVICE", "Successfully imported instrument settings");
  Trace::Log("PERSISTENCYSERVICE", "Set instrument name to: %s",
             instrumentName.c_str());
  return PERSIST_LOADED;
}
