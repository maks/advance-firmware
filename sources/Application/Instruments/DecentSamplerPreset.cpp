/*
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "DecentSamplerPreset.h"

#include "Application/Instruments/WavFile.h"
#include "Application/Instruments/WavHeader.h"
#include "Externals/yxml/yxml.h"
#include "System/FileSystem/FileHandle.h"
#include "System/FileSystem/FileSystem.h"
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace {
constexpr uint32_t MaxPresetBytes = 1024U * 1024U;
constexpr uint8_t MaxXmlDepth = 32;
constexpr size_t MaxAttributeValue = 191;

struct ElementFrame {
  char name_[32]{};
  bool enabled_ = true;
  uint8_t sequenceMode_ = 0; // 0 always, 1 round-robin, 2 unsupported
  uint16_t sequencePosition_ = 1;
  DecentSamplerZone zone_{};
  bool hasZone_ = false;
  bool hasRootNote_ = false;
  bool hasPath_ = false;
  bool unsupported_ = false;
};

struct ParserWorkspace {
  yxml_t parser_{};
  uint8_t parserStack_[1024]{};
  ElementFrame frames_[MaxXmlDepth]{};
  char attribute_[64]{};
  char value_[MaxAttributeValue + 1]{};
  size_t valueLength_ = 0;
};

#if defined(ADV) && !defined(HOST_TEST)
ParserWorkspace parserWorkspace __attribute__((section(".RAM_D3"), aligned(8)));
#else
ParserWorkspace parserWorkspace;
#endif

DecentSamplerPresetResult fail(DecentSamplerPresetError error,
                               const char *message, uint32_t line = 0) {
  return {error, line, message};
}

bool parseUnsigned(const char *text, uint32_t maximum, uint32_t &value) {
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  char *end = nullptr;
  const unsigned long parsed = std::strtoul(text, &end, 10);
  if (*end != '\0' || parsed > maximum) {
    return false;
  }
  value = static_cast<uint32_t>(parsed);
  return true;
}

bool parseBool(const char *text, bool &value) {
  if (std::strcmp(text, "true") == 0 || std::strcmp(text, "1") == 0) {
    value = true;
    return true;
  }
  if (std::strcmp(text, "false") == 0 || std::strcmp(text, "0") == 0) {
    value = false;
    return true;
  }
  return false;
}

bool parseNote(const char *text, uint8_t &note) {
  uint32_t numeric = 0;
  if (parseUnsigned(text, 127, numeric)) {
    note = static_cast<uint8_t>(numeric);
    return true;
  }
  if (text == nullptr || text[0] == '\0') {
    return false;
  }
  static constexpr int8_t semitoneByLetter[7] = {9, 11, 0, 2, 4, 5, 7};
  const char letter = text[0];
  if (letter < 'A' || letter > 'G') {
    return false;
  }
  int32_t semitone = semitoneByLetter[letter - 'A'];
  size_t index = 1;
  if (text[index] == '#') {
    ++semitone;
    ++index;
  } else if (text[index] == 'b') {
    --semitone;
    ++index;
  }
  const bool hasOctave = text[index] != '\0';
  char *end = nullptr;
  long octave = 2;
  if (hasOctave) {
    octave = std::strtol(text + index, &end, 10);
    if (end == text + index || *end != '\0') {
      return false;
    }
  }
  const long midi = ((octave + 1) * 12) + semitone;
  if (midi < 0 || midi > 127) {
    return false;
  }
  note = static_cast<uint8_t>(midi);
  return true;
}

bool endsWith(const char *text, const char *suffix) {
  const size_t textLength = std::strlen(text);
  const size_t suffixLength = std::strlen(suffix);
  return textLength >= suffixLength &&
         std::strcmp(text + textLength - suffixLength, suffix) == 0;
}

DecentSamplerPresetResult resolveSamplePath(const char *presetPath,
                                            const char *samplePath,
                                            char *resolved,
                                            size_t resolvedSize) {
  if (samplePath == nullptr || samplePath[0] == '\0') {
    return fail(DecentSamplerPresetError::MissingSample, "sample has no path");
  }
  char normalizedLeaf[MaxAttributeValue + 1]{};
  const size_t length = std::strlen(samplePath);
  if (length >= sizeof(normalizedLeaf)) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "sample path is too long");
  }
  for (size_t i = 0; i <= length; ++i) {
    normalizedLeaf[i] = samplePath[i] == '\\' ? '/' : samplePath[i];
  }
  if (normalizedLeaf[0] == '/' ||
      std::strstr(normalizedLeaf, "../") != nullptr ||
      endsWith(normalizedLeaf, "/..")) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "sample path must stay inside the preset folder");
  }

  char directory[PFILENAME_SIZE]{};
  const char *slash = std::strrchr(presetPath, '/');
  if (slash == nullptr) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "preset path must be absolute");
  }
  const size_t directoryLength = static_cast<size_t>(slash - presetPath);
  if (directoryLength >= sizeof(directory)) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "preset folder path is too long");
  }
  std::memcpy(directory, presetPath, directoryLength);
  directory[directoryLength] = '\0';
  FileSystemPath::PathString fullPath;
  if (!FileSystemPath::JoinPath(directory, normalizedLeaf, fullPath) ||
      fullPath.size() >= resolvedSize) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "resolved sample path is too long");
  }
  std::memcpy(resolved, fullPath.c_str(), fullPath.size() + 1);
  return {};
}

DecentSamplerPresetResult validateWav(const char *path, uint32_t &frames,
                                      uint8_t &channels, uint32_t &sampleRate,
                                      uint32_t &dataOffset) {
  FileHandle file = FileSystem::GetInstance()->Open(path, "r");
  if (!file) {
    return fail(DecentSamplerPresetError::MissingSample,
                "could not open sample WAV");
  }
  const auto header = WavHeaderWriter::ReadHeader(file.get());
  WavFile wav;
  const auto wavResult = wav.Open(path);
  if (!header || !wavResult || wav.GetSampleRate(-1) <= 0 ||
      (wav.GetChannelCount(-1) != 1 && wav.GetChannelCount(-1) != 2) ||
      wav.GetSize(-1) <= 0) {
    return fail(DecentSamplerPresetError::UnsupportedWav,
                "sample must be a supported mono or stereo WAV");
  }
  frames = static_cast<uint32_t>(wav.GetSize(-1));
  channels = static_cast<uint8_t>(wav.GetChannelCount(-1));
  sampleRate = static_cast<uint32_t>(wav.GetSampleRate(-1));
  dataOffset = header->dataOffset;
  return {};
}

DecentSamplerPresetResult validateZone(DecentSamplerZone &zone,
                                       const char *presetPath) {
  if (!zone.path_[0]) {
    return fail(DecentSamplerPresetError::MissingSample, "sample has no path");
  }
  char samplePath[PFILENAME_SIZE]{};
  auto pathResult =
      resolveSamplePath(presetPath, zone.path_, samplePath, sizeof(samplePath));
  if (!pathResult) {
    return pathResult;
  }
  std::memcpy(zone.path_, samplePath, std::strlen(samplePath) + 1);
  uint32_t frameCount = 0;
  auto wavResult = validateWav(zone.path_, frameCount, zone.channelCount_,
                               zone.sampleRate_, zone.dataOffset_);
  if (!wavResult) {
    return wavResult;
  }
  if (zone.startFrame_ >= frameCount ||
      (zone.endFrame_ != 0 &&
       (zone.endFrame_ <= zone.startFrame_ || zone.endFrame_ > frameCount))) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "sample start/end is outside its WAV");
  }
  zone.frameCount_ = frameCount;
  return {};
}

} // namespace

DecentSamplerPresetResult LoadDecentSamplerPreset(const char *presetPath,
                                                  DecentSamplerPreset &preset) {
  preset.zoneCount_ = 0;
  if (presetPath == nullptr) {
    return fail(DecentSamplerPresetError::OpenFailed, "preset path is empty");
  }
  FileHandle file = FileSystem::GetInstance()->Open(presetPath, "r");
  if (!file) {
    return fail(DecentSamplerPresetError::OpenFailed, "could not open preset");
  }

  parserWorkspace.parser_ = {};
  std::memset(parserWorkspace.parserStack_, 0,
              sizeof(parserWorkspace.parserStack_));
  for (auto &frame : parserWorkspace.frames_) {
    frame = {};
  }
  parserWorkspace.attribute_[0] = '\0';
  parserWorkspace.value_[0] = '\0';
  parserWorkspace.valueLength_ = 0;
  yxml_t &parser = parserWorkspace.parser_;
  auto *frames = parserWorkspace.frames_;
  yxml_init(&parser, reinterpret_cast<char *>(parserWorkspace.parserStack_),
            sizeof(parserWorkspace.parserStack_));
  uint8_t depth = 0;
  char *attribute = parserWorkspace.attribute_;
  char *value = parserWorkspace.value_;
  size_t &valueLength = parserWorkspace.valueLength_;
  bool rootSeen = false;
  bool previousWasQuote = false;
  bool insideAttributeValue = false;
  int pendingCharacter = -1;
  uint64_t bytesRead = 0;

  while (true) {
    int character = pendingCharacter >= 0 ? pendingCharacter : file->GetC();
    pendingCharacter = -1;
    if (character == EOF) {
      break;
    }
    if (bytesRead >= MaxPresetBytes) {
      return fail(DecentSamplerPresetError::MalformedXml,
                  "preset exceeds the 1 MiB import limit", parser.line);
    }
    ++bytesRead;

    // Accept the common hand-authored typo where adjacent attributes have no
    // separating whitespace, while continuing to feed strict XML to yxml.
    bool insertedSeparator = false;
    if (previousWasQuote && !std::isspace(character) && character != '/' &&
        character != '>') {
      pendingCharacter = character;
      character = ' ';
      insertedSeparator = true;
    }
    if (insertedSeparator) {
      previousWasQuote = false;
    } else if (character == '"') {
      insideAttributeValue = !insideAttributeValue;
      previousWasQuote = !insideAttributeValue;
    } else {
      previousWasQuote = false;
    }

    const yxml_ret_t result = yxml_parse(&parser, character);
    if (result < YXML_OK) {
      const char *message = "preset XML is malformed";
      if (result == YXML_ESTACK) {
        message = "preset XML parser stack exhausted";
      } else if (result == YXML_ECLOSE) {
        message = "preset XML close tag does not match";
      } else if (result == YXML_EREF) {
        message = "preset XML contains an invalid entity";
      }
      return fail(DecentSamplerPresetError::MalformedXml, message, parser.line);
    }
    if (result == YXML_ELEMSTART) {
      if (depth >= MaxXmlDepth) {
        return fail(DecentSamplerPresetError::MalformedXml,
                    "preset XML nesting is too deep", parser.line);
      }
      ElementFrame &frame = frames[depth];
      frame = {};
      if (depth > 0) {
        frame.enabled_ = frames[depth - 1].enabled_;
        frame.sequenceMode_ = frames[depth - 1].sequenceMode_;
        frame.unsupported_ = frames[depth - 1].unsupported_;
      }
      const size_t nameLength = std::strlen(parser.elem);
      if (nameLength >= sizeof(frame.name_)) {
        return fail(DecentSamplerPresetError::MalformedXml,
                    "element name is too long", parser.line);
      }
      std::memcpy(frame.name_, parser.elem, nameLength + 1);
      if (depth == 0) {
        rootSeen = std::strcmp(frame.name_, "DecentSampler") == 0;
        if (!rootSeen) {
          return fail(DecentSamplerPresetError::MalformedXml,
                      "root element must be DecentSampler", parser.line);
        }
      }
      if (std::strcmp(frame.name_, "sample") == 0) {
        frame.hasZone_ = true;
        frame.zone_.lowNote_ = 0;
        frame.zone_.highNote_ = 127;
        frame.zone_.lowVelocity_ = 0;
        frame.zone_.highVelocity_ = 127;
      }
      ++depth;
    } else if (result == YXML_ATTRSTART) {
      const size_t nameLength = std::strlen(parser.attr);
      if (nameLength >= sizeof(parserWorkspace.attribute_)) {
        return fail(DecentSamplerPresetError::MalformedXml,
                    "attribute name is too long", parser.line);
      }
      std::memcpy(attribute, parser.attr, nameLength + 1);
      valueLength = 0;
      value[0] = '\0';
    } else if (result == YXML_ATTRVAL) {
      const size_t partLength = std::strlen(parser.data);
      if (valueLength + partLength > MaxAttributeValue) {
        return fail(DecentSamplerPresetError::InvalidMapping,
                    "attribute value is too long", parser.line);
      }
      std::memcpy(value + valueLength, parser.data, partLength);
      valueLength += partLength;
      value[valueLength] = '\0';
    } else if (result == YXML_ATTREND) {
      if (depth == 0) {
        return fail(DecentSamplerPresetError::MalformedXml,
                    "attribute appears outside an element", parser.line);
      }
      ElementFrame &frame = frames[depth - 1];
      if (std::strcmp(attribute, "enabled") == 0) {
        bool enabled = false;
        if (!parseBool(value, enabled)) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "enabled must be true or false", parser.line);
        }
        frame.enabled_ = frame.enabled_ && enabled;
      } else if (std::strcmp(attribute, "seqMode") == 0) {
        if (std::strcmp(value, "always") == 0) {
          frame.sequenceMode_ = 0;
        } else if (std::strcmp(value, "round_robin") == 0) {
          frame.sequenceMode_ = 1;
        } else if (std::strcmp(value, "random") == 0 ||
                   std::strcmp(value, "true_random") == 0) {
          frame.sequenceMode_ = 2;
        } else {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "unknown seqMode value", parser.line);
        }
      } else if (std::strcmp(attribute, "rootNote") == 0 && frame.hasZone_) {
        if (!parseNote(value, frame.zone_.rootNote_)) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "rootNote must be a MIDI note or note name with octave",
                      parser.line);
        }
        frame.hasRootNote_ = true;
      } else if ((std::strcmp(attribute, "loNote") == 0 ||
                  std::strcmp(attribute, "hiNote") == 0) &&
                 frame.hasZone_) {
        uint8_t note = 0;
        if (!parseNote(value, note)) {
          return fail(
              DecentSamplerPresetError::InvalidMapping,
              "loNote/hiNote must be a MIDI note or note name with octave",
              parser.line);
        }
        (std::strcmp(attribute, "loNote") == 0 ? frame.zone_.lowNote_
                                               : frame.zone_.highNote_) = note;
      } else if ((std::strcmp(attribute, "loVel") == 0 ||
                  std::strcmp(attribute, "hiVel") == 0) &&
                 frame.hasZone_) {
        uint32_t velocity = 0;
        if (!parseUnsigned(value, 127, velocity)) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "loVel/hiVel must be between 0 and 127", parser.line);
        }
        (std::strcmp(attribute, "loVel") == 0 ? frame.zone_.lowVelocity_
                                              : frame.zone_.highVelocity_) =
            static_cast<uint8_t>(velocity);
      } else if (std::strcmp(attribute, "path") == 0 && frame.hasZone_) {
        if (std::strlen(value) >= sizeof(frame.zone_.path_)) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "sample path is too long", parser.line);
        }
        std::memcpy(frame.zone_.path_, value, std::strlen(value) + 1);
        frame.hasPath_ = true;
      } else if (std::strcmp(attribute, "loopEnabled") == 0 ||
                 std::strcmp(attribute, "trigger") == 0 ||
                 std::strcmp(attribute, "sampleMode") == 0) {
        if ((std::strcmp(attribute, "loopEnabled") == 0 &&
             std::strcmp(value, "true") == 0) ||
            (std::strcmp(attribute, "trigger") == 0 &&
             std::strcmp(value, "attack") != 0) ||
            (std::strcmp(attribute, "sampleMode") == 0 &&
             std::strcmp(value, "normal") != 0)) {
          frame.unsupported_ = true;
        }
      } else if (frame.hasZone_ && std::strcmp(attribute, "seqPosition") == 0) {
        uint32_t sequencePosition = 0;
        if (!parseUnsigned(value, UINT16_MAX, sequencePosition) ||
            sequencePosition == 0) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "seqPosition must be a positive integer", parser.line);
        }
        frame.sequencePosition_ = static_cast<uint16_t>(sequencePosition);
      } else if (frame.hasZone_ && std::strcmp(attribute, "seqLength") == 0) {
        // Sequence length is metadata when the first sample in a round robin
        // group is selected for this initial playback implementation.
      } else if (frame.hasZone_ &&
                 (std::strcmp(attribute, "releaseTrigger") == 0 ||
                  std::strcmp(attribute, "trigger") == 0 ||
                  std::strcmp(attribute, "loopCrossfade") == 0)) {
        frame.unsupported_ = true;
      } else if (frame.hasZone_ && (std::strcmp(attribute, "start") == 0 ||
                                    std::strcmp(attribute, "end") == 0)) {
        uint32_t framePosition = 0;
        if (!parseUnsigned(value, UINT32_MAX - 1, framePosition)) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "sample start/end must be a nonnegative frame",
                      parser.line);
        }
        if (std::strcmp(attribute, "start") == 0) {
          frame.zone_.startFrame_ = framePosition;
        } else {
          frame.zone_.endFrame_ = framePosition + 1;
        }
      }
    } else if (result == YXML_ELEMEND) {
      if (depth == 0) {
        return fail(DecentSamplerPresetError::MalformedXml,
                    "unexpected element end", parser.line);
      }
      ElementFrame &frame = frames[depth - 1];
      if (frame.hasZone_) {
        if (!frame.enabled_) {
          --depth;
          continue;
        }
        if (frame.sequenceMode_ == 1 && frame.sequencePosition_ != 1) {
          --depth;
          continue;
        }
        if (frame.sequenceMode_ == 2 || frame.unsupported_) {
          return fail(DecentSamplerPresetError::UnsupportedFeature,
                      "sample uses loops, triggers, or sequencing outside the "
                      "supported subset",
                      parser.line);
        }
        if (!frame.hasPath_ || !frame.hasRootNote_ ||
            frame.zone_.lowNote_ > frame.zone_.highNote_ ||
            frame.zone_.lowVelocity_ > frame.zone_.highVelocity_) {
          return fail(DecentSamplerPresetError::InvalidMapping,
                      "sample mapping has a missing path or invalid range",
                      parser.line);
        }
        auto valid = validateZone(frame.zone_, presetPath);
        if (!valid) {
          valid.line_ = parser.line;
          return valid;
        }
        if (preset.zoneCount_ >= DecentSamplerPreset::MaxZones) {
          return fail(DecentSamplerPresetError::TooManyZones,
                      "preset exceeds the 512-zone import limit", parser.line);
        }
        // A preset may contain adjacent velocity bands with an inclusive
        // shared endpoint, or a special articulation over a pitched zone.
        // V1 does not layer audio: MSamplerInstrument resolves any overlap by
        // taking the first matching sample in preset order.
        preset.zones_[preset.zoneCount_++] = frame.zone_;
      }
      --depth;
    }
  }

  if (file->Error() != 0 || !rootSeen || depth != 0 ||
      yxml_eof(&parser) != YXML_OK) {
    return fail(DecentSamplerPresetError::MalformedXml,
                "preset XML ended unexpectedly", parser.line);
  }
  if (preset.zoneCount_ == 0) {
    return fail(DecentSamplerPresetError::InvalidMapping,
                "preset contains no playable sample zones", parser.line);
  }
  return {};
}
