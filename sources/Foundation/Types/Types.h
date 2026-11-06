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

#ifndef _APP_TYPES_H_
#define _APP_TYPES_H_

#include "Externals/etl/include/etl/enum_type.h"
#include <stdint.h>

struct TrackerCommand {
  // While the names of the command codes can be changed, their values CANNOT.
  // Values are used as is in save files, so any changes would cause save files
  // to break.
  enum enum_type : uint8_t {
    InstrumentCommandArpeggiator = 0,          // ARPG
    InstrumentCommandChance = 188,             // CHN
    InstrumentCommandCrush = 2,                // CRSH
    InstrumentCommandDelay = 4,                // DLAY
    InstrumentCommandFilterCut = 20,           // FCUT
    InstrumentCommandLowPassFilter = 22,       // FLTR
    InstrumentCommandFilterResonance = 25,     // FRES
    InstrumentCommandOff = 92,                 // REL
    InstrumentCommandGroove = 26,              // GROV
    InstrumentCommandHop = 27,                 // HOP
    InstrumentCommandRetrigger = 52,           // RTRG
    InstrumentCommandInstrumentRetrigger = 29, // IRTG
    InstrumentCommandKill = 30,                // KILL
    InstrumentCommandLegato = 31,              // LEGA
    InstrumentCommandLoopOfset = 36,           // LPOF
    InstrumentCommandMidiCC = 38,              // MDCC
    InstrumentCommandMidiPC = 39,              // MDPG
    InstrumentCommandPan = 42,                 // PAN
    InstrumentCommandPitchFineTune = 44,       // PFIN
    InstrumentCommandPlayOfset = 46,           // PLOF
    InstrumentCommandPitchSlide = 48,          // PTCH
    InstrumentCommandStop = 55,                // STOP
    InstrumentCommandTable = 58,               // TABL
    InstrumentCommandTempo = 62,               // TMPO
    InstrumentCommandVelocity = 66,            // VELM
    InstrumentCommandVolume = 69,              // VOLM
    InstrumentCommandGateLength = 28,          // TLN
    InstrumentCommandNone = 45,                // ----
    InstrumentCommandMidiChord = 143,
    InstrumentCommandRandom = 189,                // RND
    InstrumentCommandDelayTime = 190,             // DLT
    InstrumentCommandDelayFeedback = 191,         // DLF
    InstrumentCommandReverbPreDelay = 192,        // RVP
    InstrumentCommandReverbInputTone = 193,       // RVI
    InstrumentCommandReverbDiffuse = 194,         // RVF
    InstrumentCommandReverbDecay = 195,           // RVD
    InstrumentCommandReverbTailTone = 196,        // RVT
    InstrumentCommandReverbModulationSpeed = 197, // RVS
    InstrumentCommandReverbModulationLevel = 198, // RVL
    Invalid = 255,
  };
  ETL_DECLARE_ENUM_TYPE(TrackerCommand, uint8_t)
  ETL_ENUM_TYPE(InstrumentCommandArpeggiator, "ARP")
  ETL_ENUM_TYPE(InstrumentCommandChance, "CHN")
  ETL_ENUM_TYPE(InstrumentCommandCrush, "CSH")
  ETL_ENUM_TYPE(InstrumentCommandKill, "KIL")
  ETL_ENUM_TYPE(InstrumentCommandLoopOfset, "LOF")
  ETL_ENUM_TYPE(InstrumentCommandVelocity, "VEL")
  ETL_ENUM_TYPE(InstrumentCommandVolume, "VOL")
  ETL_ENUM_TYPE(InstrumentCommandGateLength, "TLN")
  ETL_ENUM_TYPE(InstrumentCommandPitchSlide, "PSL")
  ETL_ENUM_TYPE(InstrumentCommandRandom, "RND")
  ETL_ENUM_TYPE(InstrumentCommandHop, "HOP")
  ETL_ENUM_TYPE(InstrumentCommandLegato, "LEG")
  ETL_ENUM_TYPE(InstrumentCommandRetrigger, "RTG")
  ETL_ENUM_TYPE(InstrumentCommandTempo, "TPO")
  ETL_ENUM_TYPE(InstrumentCommandMidiCC, "MCC")
  ETL_ENUM_TYPE(InstrumentCommandMidiPC, "MPC")
  ETL_ENUM_TYPE(InstrumentCommandPlayOfset, "POF")
  ETL_ENUM_TYPE(InstrumentCommandLowPassFilter, "FLT")
  ETL_ENUM_TYPE(InstrumentCommandTable, "TBL")
  ETL_ENUM_TYPE(InstrumentCommandFilterCut, "FCT")
  ETL_ENUM_TYPE(InstrumentCommandFilterResonance, "FRS")
  ETL_ENUM_TYPE(InstrumentCommandPan, "PAN")
  ETL_ENUM_TYPE(InstrumentCommandOff, "REL")
  ETL_ENUM_TYPE(InstrumentCommandGroove, "GRV")
  ETL_ENUM_TYPE(InstrumentCommandStop, "STP")
  ETL_ENUM_TYPE(InstrumentCommandNone, "---")
  ETL_ENUM_TYPE(InstrumentCommandPitchFineTune, "PFT")
  ETL_ENUM_TYPE(InstrumentCommandDelay, "DLY")
  ETL_ENUM_TYPE(InstrumentCommandDelayTime, "DLT")
  ETL_ENUM_TYPE(InstrumentCommandDelayFeedback, "DLF")
  ETL_ENUM_TYPE(InstrumentCommandReverbPreDelay, "RVP")
  ETL_ENUM_TYPE(InstrumentCommandReverbInputTone, "RVI")
  ETL_ENUM_TYPE(InstrumentCommandReverbDiffuse, "RVF")
  ETL_ENUM_TYPE(InstrumentCommandReverbDecay, "RVD")
  ETL_ENUM_TYPE(InstrumentCommandReverbTailTone, "RVT")
  ETL_ENUM_TYPE(InstrumentCommandReverbModulationSpeed, "RVS")
  ETL_ENUM_TYPE(InstrumentCommandReverbModulationLevel, "RVL")
  ETL_ENUM_TYPE(InstrumentCommandInstrumentRetrigger, "IRT")
  ETL_ENUM_TYPE(InstrumentCommandMidiChord, "MCH")
  ETL_END_ENUM_TYPE
};

struct FourCC {
  enum enum_type : uint16_t {
    Default,

    SampleInstrumentCrushVolume,
    SampleInstrumentVolume,
    SampleInstrumentCrush,
    SampleInstrumentSample,
    SampleInstrumentInterpolation,
    SampleInstrumentDownsample,
    SampleInstrumentRootNote,
    SampleInstrumentFineTune,
    SampleInstrumentPan,
    SampleInstrumentFilterCutOff,
    SampleInstrumentFilterResonance,
    SampleInstrumentFilterType,
    SampleInstrumentFilterMode,
    SampleInstrumentStart,
    SampleInstrumentLoopMode,
    SampleInstrumentLoopStart,
    SampleInstrumentEnd,
    SampleInstrumentTable,
    SampleInstrumentTableAutomation,

    StarLoomInstrumentShape,
    StarLoomInstrumentTimbre,
    StarLoomInstrumentColor,
    StarLoomInstrumentAttack,
    StarLoomInstrumentDecay,
    StarLoomInstrumentSignature,

    MidiInstrumentChannel,
    MidiInstrumentNoteLength,
    MidiInstrumentVolume,
    MidiInstrumentTable,
    MidiInstrumentTableAutomation,
    MidiInstrumentName,
    MidiInstrumentProgram,

    SIDInstrumentWaveform,
    SIDInstrument1FilterCut,
    SIDInstrument2FilterCut,
    SIDInstrument3FilterCut,
    SIDInstrument1FilterResonance,
    SIDInstrument2FilterResonance,
    SIDInstrument3FilterResonance,
    SIDInstrument1FilterMode,
    SIDInstrument2FilterMode,
    SIDInstrument3FilterMode,
    SIDInstrument1Volume,
    SIDInstrument2Volume,
    SIDInstrument3Volume,
    SIDInstrumentPulseWidth,
    SIDInstrumentVSync,
    SIDInstrumentRingModulator,
    SIDInstrumentADSR,
    SIDInstrumentFilterOn,
    SIDInstrumentVoice3Off,
    SIDInstrumentTable,
    SIDInstrumentTableAutomation,
    SIDInstrumentOSCNumber,

    LFO1Target,
    LFO1Shape,
    LFO1Trigger,
    LFO1RateMode,
    LFO1Depth,
    LFO1HertzTenths,
    LFO1Sync,
    LFO2Target,
    LFO2Shape,
    LFO2Trigger,
    LFO2RateMode,
    LFO2Depth,
    LFO2HertzTenths,
    LFO2Sync,

    ServicePersistency,

    TrigTempoTap,
    TrigSeqQueueRow,
    TrigVolumeIncrease,
    TrigVolumeDecrease,
    TrigEventEnter,
    TrigEventEdit,
    TrigEventLeft,
    TrigEventRight,
    TrigEventUp,
    TrigEventDown,
    TrigEventAlt,
    TrigEventNav,
    TrigEventPlay,

    VarTempo,
    VarMasterVolume,
    VarPreviewVolume,
    VarWrap,
    VarTranspose,
    VarScale,
    VarScaleRoot,
    VarProjectName,
    VarMidiDevice,
    VarMidiControlSurface,
    VarLineOut,
    VarFGColor,
    VarBGColor,
    VarHI1Color,
    VarHI2Color,
    VarConsoleColor,
    VarCursorColor,
    VarInfoColor,
    VarWarnColor,
    VarErrorColor,
    VarAccentColor,
    VarAccentAltColor,
    VarEmphasisColor,
    VarReserved1Color,
    VarReserved2Color,
    VarReserved3Color,
    VarReserved4Color,
    VarMidiSync,
    VarMidiInputDevice,
    VarMidiClockSync,
    VarRemoteUI,

    VarChannel1Volume,
    VarChannel2Volume,
    VarChannel3Volume,
    VarChannel4Volume,
    VarChannel5Volume,
    VarChannel6Volume,
    VarChannel7Volume,
    VarChannel8Volume,
    VarChannel1FX1Send,
    VarChannel2FX1Send,
    VarChannel3FX1Send,
    VarChannel4FX1Send,
    VarChannel5FX1Send,
    VarChannel6FX1Send,
    VarChannel7FX1Send,
    VarChannel8FX1Send,
    VarChannel1FX2Send,
    VarChannel2FX2Send,
    VarChannel3FX2Send,
    VarChannel4FX2Send,
    VarChannel5FX2Send,
    VarChannel6FX2Send,
    VarChannel7FX2Send,
    VarChannel8FX2Send,
    VarChannel1FX3Send,
    VarChannel2FX3Send,
    VarChannel3FX3Send,
    VarChannel4FX3Send,
    VarChannel5FX3Send,
    VarChannel6FX3Send,
    VarChannel7FX3Send,
    VarChannel8FX3Send,
    VarChannel1FX4Send,
    VarChannel2FX4Send,
    VarChannel3FX4Send,
    VarChannel4FX4Send,
    VarChannel5FX4Send,
    VarChannel6FX4Send,
    VarChannel7FX4Send,
    VarChannel8FX4Send,
    VarFX1Volume,
    VarFX2Volume,
    VarFX3Volume,
    VarFX4Volume,
    VarFX1ToFX2Send,
    VarFX1ToFX3Send,
    VarFX1ToFX4Send,
    VarFX2ToFX3Send,
    VarFX2ToFX4Send,
    VarFX3ToFX4Send,
    VarThemeName, // Variable for storing the current theme name

    VarInstrumentType,

    ActionTempoChanged,
    ActionPurge,
    ActionPurgeInstrument,
    ActionProjectRename,
    ActionBrowse,
    ActionSave,
    ActionLoadAndSave,
    ActionCancel,
    ActionNewProject,
    ActionRandomName,
    ActionBootSelect,
    ActionEdit,
    ActionExport,
    ActionImport,
    ActionOK,
    InstrumentName,
    ActionRenderMixdown,
    ActionRenderStems,
    ActionShowTheme,
    ActionShowCredits,
    ActionThemeName,
    ActionResetTheme,
    SampleInstrumentSlices,
    VarBacklightLevel,
    ActionShowSampleEditor,
    ActionShowSampleSlices,
    VarRecordSource,
    VarSampleEditStart,
    VarSampleEditEnd,
    VarSampleEditOperation,
    VarRecordLineGain,
    VarRecordMicGain,
    VarOutputVolume,
    VarImportResampler,
    ActionAutoSlice,
    ActionShowRecordView,
    VoiceOverdrive,
    VoiceBandpassCutoff,
    VoiceBandpassResonance,
    VoiceBandpassMix,
    VoiceFilterCutoff,
    VoiceFilterResonance,
    VoiceFilterMix,
    VoiceAmp,
    VoicePan,
    VoiceInsertType,
    VoiceAmpAttack,
    VoiceAmpDecay,
    VoiceAmpSustain,
    VoiceAmpRelease,
    VoiceFilterAttack,
    VoiceFilterDecay,
    VoiceFilterSustain,
    VoiceFilterRelease,
    VoiceFilterEnvAmount,
    VoiceInsertType2,
    VoiceInsertAmount,
    VoiceInsertAmount2,
    VoiceInsertPosition,
    VoiceInsertPosition2,
    VoiceTriggerLength,
    VoiceFilterType,
    SampleSourceInstrumentSample,
    SampleSourceInstrumentRootNote,
    SampleSourceInstrumentStart,
    SampleSourceInstrumentLoopMode,
    SampleSourceInstrumentLoopStart,
    SampleSourceInstrumentEnd,
    SampleSourceInstrumentTable,
    SampleSourceInstrumentTableAutomation,
    SampleSourceInstrumentLevel,

    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    SIDInstrument2Waveform,
    SIDInstrument3Waveform,
    SIDInstrument2PulseWidth,
    SIDInstrument3PulseWidth,
    SIDInstrument2VSync,
    SIDInstrument3VSync,
    SIDInstrument2RingModulator,
    SIDInstrument3RingModulator,
    SIDInstrument2ADSR,
    SIDInstrument3ADSR,
    SIDInstrument2FilterOn,
    SIDInstrument3FilterOn,
    SIDInstrumentPitchOffset,
    SIDInstrument2PitchOffset,
    SIDInstrument3PitchOffset,
    SIDInstrumentChipModel,
    FM6InstrumentProgram,
    // Retains the removed FM6 instrument transpose ID's persisted ordinal.
    FM6InstrumentReserved,
    FM6InstrumentTable,
    FM6InstrumentTableAutomation,
    ActionShowFM6Presets,
    // FM6 patch storage: one hex-encoded slice of the packed DX7 voice
    // record per operator plus one for the global block. The operator IDs
    // are contiguous so they can be computed as FM6InstrumentOp1Params +
    // opIndex. The slices follow the record's physical order: DX7 packed
    // records store operator 6 first, so op1params holds DX7 operator 6 and
    // op6params holds DX7 operator 1.
    FM6InstrumentOp1Params,
    FM6InstrumentOp2Params,
    FM6InstrumentOp3Params,
    FM6InstrumentOp4Params,
    FM6InstrumentOp5Params,
    FM6InstrumentOp6Params,
    FM6InstrumentGlobalParams,
    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    StarLoomInstrumentOscillator1Level,
    StarLoomInstrumentOscillator2Shape,
    StarLoomInstrumentOscillator2Timbre,
    StarLoomInstrumentOscillator2Color,
    StarLoomInstrumentOscillator2Level,
    StarLoomInstrumentOscillator2PitchOffset,
    StarLoomInstrumentInteractionMode,
    StarLoomInstrumentMix,
    StarLoomInstrumentOscillator1PitchOffset,
    StarLoomInstrumentTable,
    StarLoomInstrumentTableAutomation,
    picoSwarmInstrumentDetune,
    picoSwarmInstrumentMix,
    picoSwarmInstrumentTable,
    picoSwarmInstrumentTableAutomation,
    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    VarInstrumentAudition,
    VarDelayTime,
    VarDelayFeedback,
    VarPlatePreDelay,
    VarPlateSize,
    VarPlateDiffusion,
    VarPlateDecay,
    VarPlateInputLowCut,
    VarPlateInputHighCut,
    VarPlateTankLowCut,
    VarPlateTankHighCut,
    VarPlateModulationRate,
    VarPlateModulationDepth,
    VarPlateModulationShape,
    VarPlatePitchMode,
    VarPlateInputDiffusion,
    VarPlateFreeze,
    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    VarDattorroPreDelay,
    VarDattorroBandwidth,
    VarDattorroInputDiffusion1,
    VarDattorroInputDiffusion2,
    VarDattorroDecay,
    VarDattorroDecayDiffusion1,
    VarDattorroDecayDiffusion2,
    VarDattorroDamping,
    VarDattorroExcursionRate,
    VarDattorroExcursionDepth,
    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    VarReverbPreDelay,
    VarReverbInputTone,
    VarReverbPreDiffusion1,
    VarReverbPreDiffusion2,
    VarReverbDecay,
    VarReverbDecayDiffusion,
    VarReverbTailTone,
    VarReverbModulationRate,
    VarReverbModulationDepth,
    VarReverbDiffuse,
    VarAudioOutput,
    // Keep new persisted IDs at the end so existing numeric IDs do not move.
    VarLineInMixLevel,
  };
  ETL_DECLARE_ENUM_TYPE(FourCC, uint16_t)
  // Not all enums need reflection. Only cases where we need reflection is the
  // FourCC codes that need to be converted to text in order to display on
  // screen
  ETL_ENUM_TYPE(VarLineOut, "LINEOUT")
  ETL_ENUM_TYPE(VarMidiDevice, "MIDIDEVICE")
  ETL_ENUM_TYPE(VarMidiControlSurface, "MIDICTRLSURF")
  ETL_ENUM_TYPE(VarMidiSync, "MIDISYNC")
  ETL_ENUM_TYPE(VarMidiInputDevice, "MIDIINPUTDEV")
  ETL_ENUM_TYPE(VarMidiClockSync, "MIDICLOCKSYNC")
  ETL_ENUM_TYPE(VarRemoteUI, "REMOTEUI")
  ETL_ENUM_TYPE(VarInstrumentAudition, "INSTAUDITION")
  ETL_ENUM_TYPE(VarThemeName, "THEMENAME")
  ETL_ENUM_TYPE(VarScaleRoot, "scaleroot")
  ETL_ENUM_TYPE(StarLoomInstrumentShape, "shape")
  ETL_ENUM_TYPE(StarLoomInstrumentTimbre, "timbre")
  ETL_ENUM_TYPE(StarLoomInstrumentColor, "color")
  ETL_ENUM_TYPE(StarLoomInstrumentAttack, "Attack")
  ETL_ENUM_TYPE(StarLoomInstrumentDecay, "Decay")
  ETL_ENUM_TYPE(StarLoomInstrumentSignature, "Signature")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator1Level, "osc1level")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator2Shape, "osc2shape")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator2Timbre, "osc2timbre")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator2Color, "osc2color")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator2Level, "osc2level")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator2PitchOffset, "osc2pitch")
  ETL_ENUM_TYPE(StarLoomInstrumentInteractionMode, "oscinteract")
  ETL_ENUM_TYPE(StarLoomInstrumentMix, "mix")
  ETL_ENUM_TYPE(StarLoomInstrumentOscillator1PitchOffset, "osc1pitch")
  ETL_ENUM_TYPE(StarLoomInstrumentTable, "table")
  ETL_ENUM_TYPE(StarLoomInstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(SampleInstrumentSample, "sample")
  ETL_ENUM_TYPE(SampleInstrumentVolume, "volume")
  ETL_ENUM_TYPE(SampleInstrumentInterpolation, "interpol")
  ETL_ENUM_TYPE(SampleInstrumentCrush, "crush")
  ETL_ENUM_TYPE(SampleInstrumentCrushVolume, "crushdrive")
  ETL_ENUM_TYPE(SampleInstrumentDownsample, "downsample")
  ETL_ENUM_TYPE(SampleInstrumentRootNote, "root note")
  ETL_ENUM_TYPE(SampleInstrumentFineTune, "fine tune")
  ETL_ENUM_TYPE(SampleInstrumentPan, "pan")
  ETL_ENUM_TYPE(SampleInstrumentFilterCutOff, "filter cut")
  ETL_ENUM_TYPE(SampleInstrumentFilterResonance, "filter res")
  ETL_ENUM_TYPE(SampleInstrumentFilterType, "filter type")
  ETL_ENUM_TYPE(SampleInstrumentFilterMode, "filter mode")
  ETL_ENUM_TYPE(SampleInstrumentStart, "start")
  ETL_ENUM_TYPE(SampleInstrumentLoopMode, "loopmode")
  ETL_ENUM_TYPE(SampleInstrumentLoopStart, "loopstart")
  ETL_ENUM_TYPE(SampleInstrumentEnd, "end")
  ETL_ENUM_TYPE(SampleInstrumentTable, "table")
  ETL_ENUM_TYPE(SampleInstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(MidiInstrumentChannel, "channel")
  ETL_ENUM_TYPE(InstrumentName, "name")
  ETL_ENUM_TYPE(MidiInstrumentName, "midi name")
  ETL_ENUM_TYPE(MidiInstrumentNoteLength, "note length")
  ETL_ENUM_TYPE(MidiInstrumentVolume, "volume")
  ETL_ENUM_TYPE(MidiInstrumentTable, "table")
  ETL_ENUM_TYPE(MidiInstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(MidiInstrumentProgram, "program")
  ETL_ENUM_TYPE(SIDInstrumentWaveform, "VWF")
  ETL_ENUM_TYPE(SIDInstrument1FilterCut, "FILTCUT1")
  ETL_ENUM_TYPE(SIDInstrument1FilterResonance, "RES1")
  ETL_ENUM_TYPE(SIDInstrument1FilterMode, "FMODE1")
  ETL_ENUM_TYPE(SIDInstrument1Volume, "DIP_VOLUME1")
  ETL_ENUM_TYPE(SIDInstrument2FilterCut, "FILTCUT2")
  ETL_ENUM_TYPE(SIDInstrument2FilterResonance, "RES2")
  ETL_ENUM_TYPE(SIDInstrument2FilterMode, "FMODE2")
  ETL_ENUM_TYPE(SIDInstrument2Volume, "DIP_VOLUME2")
  ETL_ENUM_TYPE(SIDInstrumentPulseWidth, "VPW")
  ETL_ENUM_TYPE(SIDInstrumentVSync, "VSYNC")
  ETL_ENUM_TYPE(SIDInstrumentRingModulator, "VRING")
  ETL_ENUM_TYPE(SIDInstrumentADSR, "VADSR")
  ETL_ENUM_TYPE(SIDInstrumentFilterOn, "VFON")
  ETL_ENUM_TYPE(SIDInstrumentTable, "table")
  ETL_ENUM_TYPE(SIDInstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(SIDInstrumentOSCNumber, "OSCNUM")

  ETL_ENUM_TYPE(LFO1Target, "lfo1 target")
  ETL_ENUM_TYPE(LFO1Shape, "lfo1 shape")
  ETL_ENUM_TYPE(LFO1Trigger, "lfo1 trigger")
  ETL_ENUM_TYPE(LFO1RateMode, "lfo1 rate mode")
  ETL_ENUM_TYPE(LFO1Depth, "lfo1 depth")
  ETL_ENUM_TYPE(LFO1HertzTenths, "lfo1 hz")
  ETL_ENUM_TYPE(LFO1Sync, "lfo1 sync")
  ETL_ENUM_TYPE(LFO2Target, "lfo2 target")
  ETL_ENUM_TYPE(LFO2Shape, "lfo2 shape")
  ETL_ENUM_TYPE(LFO2Trigger, "lfo2 trigger")
  ETL_ENUM_TYPE(LFO2RateMode, "lfo2 rate mode")
  ETL_ENUM_TYPE(LFO2Depth, "lfo2 depth")
  ETL_ENUM_TYPE(LFO2HertzTenths, "lfo2 hz")
  ETL_ENUM_TYPE(LFO2Sync, "lfo2 sync")

  ETL_ENUM_TYPE(VarFGColor, "FOREGROUND")
  ETL_ENUM_TYPE(VarBGColor, "BACKGROUND")
  ETL_ENUM_TYPE(VarHI1Color, "HICOLOR1")
  ETL_ENUM_TYPE(VarHI2Color, "HICOLOR2")
  ETL_ENUM_TYPE(VarConsoleColor, "CONSOLECOLOR")
  ETL_ENUM_TYPE(VarCursorColor, "CURSORCOLOR")
  ETL_ENUM_TYPE(VarInfoColor, "INFOCOLOR")
  ETL_ENUM_TYPE(VarWarnColor, "WARNCOLOR")
  ETL_ENUM_TYPE(VarErrorColor, "ERRORCOLOR")
  ETL_ENUM_TYPE(VarAccentColor, "ACCENTCOLOR")
  ETL_ENUM_TYPE(VarAccentAltColor, "ACCENTALTCOLOR")
  ETL_ENUM_TYPE(VarEmphasisColor, "EMPHASISCOLOR")
  ETL_ENUM_TYPE(VarReserved1Color, "RESERVED1COLOR")
  ETL_ENUM_TYPE(VarReserved2Color, "RESERVED2COLOR")
  ETL_ENUM_TYPE(VarReserved3Color, "RESERVED3COLOR")
  ETL_ENUM_TYPE(VarReserved4Color, "RESERVED4COLOR")
  ETL_ENUM_TYPE(VarTempo, "tempo")
  ETL_ENUM_TYPE(VarMasterVolume, "mastervolume")
  ETL_ENUM_TYPE(VarPreviewVolume, "preview")
  ETL_ENUM_TYPE(VarWrap, "wrap")
  ETL_ENUM_TYPE(VarTranspose, "transpose")
  ETL_ENUM_TYPE(VarScale, "scale")
  ETL_ENUM_TYPE(VarProjectName, "projectname")
  ETL_ENUM_TYPE(VarInstrumentType, "INSTRUMENTTYPE")
  ETL_ENUM_TYPE(VarChannel1Volume, "channel1volume")
  ETL_ENUM_TYPE(VarChannel2Volume, "channel2volume")
  ETL_ENUM_TYPE(VarChannel3Volume, "channel3volume")
  ETL_ENUM_TYPE(VarChannel4Volume, "channel4volume")
  ETL_ENUM_TYPE(VarChannel5Volume, "channel5volume")
  ETL_ENUM_TYPE(VarChannel6Volume, "channel6volume")
  ETL_ENUM_TYPE(VarChannel7Volume, "channel7volume")
  ETL_ENUM_TYPE(VarChannel8Volume, "channel8volume")
  ETL_ENUM_TYPE(VarChannel1FX1Send, "channel1fx1send")
  ETL_ENUM_TYPE(VarChannel2FX1Send, "channel2fx1send")
  ETL_ENUM_TYPE(VarChannel3FX1Send, "channel3fx1send")
  ETL_ENUM_TYPE(VarChannel4FX1Send, "channel4fx1send")
  ETL_ENUM_TYPE(VarChannel5FX1Send, "channel5fx1send")
  ETL_ENUM_TYPE(VarChannel6FX1Send, "channel6fx1send")
  ETL_ENUM_TYPE(VarChannel7FX1Send, "channel7fx1send")
  ETL_ENUM_TYPE(VarChannel8FX1Send, "channel8fx1send")
  ETL_ENUM_TYPE(VarChannel1FX2Send, "channel1fx2send")
  ETL_ENUM_TYPE(VarChannel2FX2Send, "channel2fx2send")
  ETL_ENUM_TYPE(VarChannel3FX2Send, "channel3fx2send")
  ETL_ENUM_TYPE(VarChannel4FX2Send, "channel4fx2send")
  ETL_ENUM_TYPE(VarChannel5FX2Send, "channel5fx2send")
  ETL_ENUM_TYPE(VarChannel6FX2Send, "channel6fx2send")
  ETL_ENUM_TYPE(VarChannel7FX2Send, "channel7fx2send")
  ETL_ENUM_TYPE(VarChannel8FX2Send, "channel8fx2send")
  ETL_ENUM_TYPE(VarChannel1FX3Send, "channel1fx3send")
  ETL_ENUM_TYPE(VarChannel2FX3Send, "channel2fx3send")
  ETL_ENUM_TYPE(VarChannel3FX3Send, "channel3fx3send")
  ETL_ENUM_TYPE(VarChannel4FX3Send, "channel4fx3send")
  ETL_ENUM_TYPE(VarChannel5FX3Send, "channel5fx3send")
  ETL_ENUM_TYPE(VarChannel6FX3Send, "channel6fx3send")
  ETL_ENUM_TYPE(VarChannel7FX3Send, "channel7fx3send")
  ETL_ENUM_TYPE(VarChannel8FX3Send, "channel8fx3send")
  ETL_ENUM_TYPE(VarChannel1FX4Send, "channel1fx4send")
  ETL_ENUM_TYPE(VarChannel2FX4Send, "channel2fx4send")
  ETL_ENUM_TYPE(VarChannel3FX4Send, "channel3fx4send")
  ETL_ENUM_TYPE(VarChannel4FX4Send, "channel4fx4send")
  ETL_ENUM_TYPE(VarChannel5FX4Send, "channel5fx4send")
  ETL_ENUM_TYPE(VarChannel6FX4Send, "channel6fx4send")
  ETL_ENUM_TYPE(VarChannel7FX4Send, "channel7fx4send")
  ETL_ENUM_TYPE(VarChannel8FX4Send, "channel8fx4send")
  ETL_ENUM_TYPE(VarFX1Volume, "fx1vol")
  ETL_ENUM_TYPE(VarFX2Volume, "fx2vol")
  ETL_ENUM_TYPE(VarFX3Volume, "fx3vol")
  ETL_ENUM_TYPE(VarFX4Volume, "fx4vol")
  ETL_ENUM_TYPE(VarFX1ToFX2Send, "fx1to2send")
  ETL_ENUM_TYPE(VarFX1ToFX3Send, "fx1to3send")
  ETL_ENUM_TYPE(VarFX1ToFX4Send, "fx1to4send")
  ETL_ENUM_TYPE(VarFX2ToFX3Send, "fx2to3send")
  ETL_ENUM_TYPE(VarFX2ToFX4Send, "fx2to4send")
  ETL_ENUM_TYPE(VarFX3ToFX4Send, "fx3to4send")
  ETL_ENUM_TYPE(VarDelayTime, "delaytime")
  ETL_ENUM_TYPE(VarDelayFeedback, "delayfeedback")
  ETL_ENUM_TYPE(VarPlatePreDelay, "platepre")
  ETL_ENUM_TYPE(VarPlateSize, "platesize")
  ETL_ENUM_TYPE(VarPlateDiffusion, "platedif")
  ETL_ENUM_TYPE(VarPlateDecay, "platedec")
  ETL_ENUM_TYPE(VarPlateInputLowCut, "plateilow")
  ETL_ENUM_TYPE(VarPlateInputHighCut, "plateihigh")
  ETL_ENUM_TYPE(VarPlateTankLowCut, "platetlow")
  ETL_ENUM_TYPE(VarPlateTankHighCut, "platethigh")
  ETL_ENUM_TYPE(VarPlateModulationRate, "platerate")
  ETL_ENUM_TYPE(VarPlateModulationDepth, "platedepth")
  ETL_ENUM_TYPE(VarPlateModulationShape, "plateshape")
  ETL_ENUM_TYPE(VarPlatePitchMode, "platepitch")
  ETL_ENUM_TYPE(VarPlateInputDiffusion, "plateearly")
  ETL_ENUM_TYPE(VarPlateFreeze, "platefreeze")
  ETL_ENUM_TYPE(VarDattorroPreDelay, "dattpre")
  ETL_ENUM_TYPE(VarDattorroBandwidth, "dattbw")
  ETL_ENUM_TYPE(VarDattorroInputDiffusion1, "dattindif1")
  ETL_ENUM_TYPE(VarDattorroInputDiffusion2, "dattindif2")
  ETL_ENUM_TYPE(VarDattorroDecay, "dattdec")
  ETL_ENUM_TYPE(VarDattorroDecayDiffusion1, "dattdecdif1")
  ETL_ENUM_TYPE(VarDattorroDecayDiffusion2, "dattdecdif2")
  ETL_ENUM_TYPE(VarDattorroDamping, "dattdamp")
  ETL_ENUM_TYPE(VarDattorroExcursionRate, "dattrate")
  ETL_ENUM_TYPE(VarDattorroExcursionDepth, "dattdepth")
  ETL_ENUM_TYPE(VarReverbPreDelay, "reverbpre")
  ETL_ENUM_TYPE(VarReverbInputTone, "reverbintone")
  ETL_ENUM_TYPE(VarReverbPreDiffusion1, "reverbprediff1")
  ETL_ENUM_TYPE(VarReverbPreDiffusion2, "reverbprediff2")
  ETL_ENUM_TYPE(VarReverbDecay, "reverbdecay")
  ETL_ENUM_TYPE(VarReverbDecayDiffusion, "reverbdecdiff")
  ETL_ENUM_TYPE(VarReverbTailTone, "reverbtone")
  ETL_ENUM_TYPE(VarReverbModulationRate, "reverbmodrate")
  ETL_ENUM_TYPE(VarReverbModulationDepth, "reverbmoddepth")
  ETL_ENUM_TYPE(VarReverbDiffuse, "reverbdiffuse")

  ETL_ENUM_TYPE(ActionEdit, "edit")
  ETL_ENUM_TYPE(ActionExport, "export")
  ETL_ENUM_TYPE(ActionImport, "import")
  ETL_ENUM_TYPE(ActionThemeName, "themename")
  ETL_ENUM_TYPE(ActionResetTheme, "resettheme")
  ETL_ENUM_TYPE(VarBacklightLevel, "backlightLevel")
  ETL_ENUM_TYPE(VarRecordSource, "recordsource")
  ETL_ENUM_TYPE(VarRecordLineGain, "recordlinegain")
  ETL_ENUM_TYPE(VarRecordMicGain, "recordmicgain")
  ETL_ENUM_TYPE(VarOutputVolume, "outputvolume")
  ETL_ENUM_TYPE(VarImportResampler, "IMPORTRESAMP")
  ETL_ENUM_TYPE(VarAudioOutput, "AUDIOOUTPUT")
  ETL_ENUM_TYPE(VarLineInMixLevel, "lineinmix")
  ETL_ENUM_TYPE(VoiceOverdrive, "voice drive")
  ETL_ENUM_TYPE(VoiceBandpassCutoff, "voice base")
  ETL_ENUM_TYPE(VoiceBandpassResonance, "voice bw res")
  ETL_ENUM_TYPE(VoiceBandpassMix, "voice wdth")
  ETL_ENUM_TYPE(VoiceFilterCutoff, "voice filter cut")
  ETL_ENUM_TYPE(VoiceFilterResonance, "voice filter res")
  ETL_ENUM_TYPE(VoiceFilterMix, "voice filter mix")
  ETL_ENUM_TYPE(VoiceAmp, "voice amp")
  ETL_ENUM_TYPE(VoicePan, "voice pan")
  ETL_ENUM_TYPE(VoiceInsertType, "voice insert")
  ETL_ENUM_TYPE(VoiceAmpAttack, "voice amp atk")
  ETL_ENUM_TYPE(VoiceAmpDecay, "voice amp dec")
  ETL_ENUM_TYPE(VoiceAmpSustain, "voice amp sus")
  ETL_ENUM_TYPE(VoiceAmpRelease, "voice amp rel")
  ETL_ENUM_TYPE(VoiceFilterAttack, "voice filter atk")
  ETL_ENUM_TYPE(VoiceFilterDecay, "voice filter dec")
  ETL_ENUM_TYPE(VoiceFilterSustain, "voice filter sus")
  ETL_ENUM_TYPE(VoiceFilterRelease, "voice filter rel")
  ETL_ENUM_TYPE(VoiceFilterEnvAmount, "voice filter env")
  ETL_ENUM_TYPE(VoiceInsertType2, "voice insert 2")
  ETL_ENUM_TYPE(VoiceInsertAmount, "voice insert amt")
  ETL_ENUM_TYPE(VoiceInsertAmount2, "voice insert amt2")
  ETL_ENUM_TYPE(VoiceInsertPosition, "voice insert pos")
  ETL_ENUM_TYPE(VoiceInsertPosition2, "voice insert pos2")
  ETL_ENUM_TYPE(VoiceTriggerLength, "voice trig len")
  ETL_ENUM_TYPE(VoiceFilterType, "voice filter type")
  ETL_ENUM_TYPE(SampleSourceInstrumentSample, "sample")
  ETL_ENUM_TYPE(SampleSourceInstrumentRootNote, "root note")
  ETL_ENUM_TYPE(SampleSourceInstrumentStart, "start")
  ETL_ENUM_TYPE(SampleSourceInstrumentLoopMode, "loopmode")
  ETL_ENUM_TYPE(SampleSourceInstrumentLoopStart, "loopstart")
  ETL_ENUM_TYPE(SampleSourceInstrumentEnd, "end")
  ETL_ENUM_TYPE(SampleSourceInstrumentTable, "table")
  ETL_ENUM_TYPE(SampleSourceInstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(SampleSourceInstrumentLevel, "level")
  ETL_ENUM_TYPE(SIDInstrument2Waveform, "VWF2")
  ETL_ENUM_TYPE(SIDInstrument3Waveform, "VWF3")
  ETL_ENUM_TYPE(SIDInstrument2PulseWidth, "VPW2")
  ETL_ENUM_TYPE(SIDInstrument3PulseWidth, "VPW3")
  ETL_ENUM_TYPE(SIDInstrument2VSync, "VSYNC2")
  ETL_ENUM_TYPE(SIDInstrument3VSync, "VSYNC3")
  ETL_ENUM_TYPE(SIDInstrument2RingModulator, "VRING2")
  ETL_ENUM_TYPE(SIDInstrument3RingModulator, "VRING3")
  ETL_ENUM_TYPE(SIDInstrument2ADSR, "VADSR2")
  ETL_ENUM_TYPE(SIDInstrument3ADSR, "VADSR3")
  ETL_ENUM_TYPE(SIDInstrument2FilterOn, "VFON2")
  ETL_ENUM_TYPE(SIDInstrument3FilterOn, "VFON3")
  ETL_ENUM_TYPE(SIDInstrumentPitchOffset, "VPITCH")
  ETL_ENUM_TYPE(SIDInstrument2PitchOffset, "VPITCH2")
  ETL_ENUM_TYPE(SIDInstrument3PitchOffset, "VPITCH3")
  ETL_ENUM_TYPE(SIDInstrumentChipModel, "SIDCHIP")
  ETL_ENUM_TYPE(FM6InstrumentProgram, "program")
  ETL_ENUM_TYPE(FM6InstrumentTable, "table")
  ETL_ENUM_TYPE(FM6InstrumentTableAutomation, "table automation")
  ETL_ENUM_TYPE(FM6InstrumentOp1Params, "op1params")
  ETL_ENUM_TYPE(FM6InstrumentOp2Params, "op2params")
  ETL_ENUM_TYPE(FM6InstrumentOp3Params, "op3params")
  ETL_ENUM_TYPE(FM6InstrumentOp4Params, "op4params")
  ETL_ENUM_TYPE(FM6InstrumentOp5Params, "op5params")
  ETL_ENUM_TYPE(FM6InstrumentOp6Params, "op6params")
  ETL_ENUM_TYPE(FM6InstrumentGlobalParams, "patchglobals")
  ETL_ENUM_TYPE(picoSwarmInstrumentDetune, "detune")
  ETL_ENUM_TYPE(picoSwarmInstrumentMix, "mix")
  ETL_ENUM_TYPE(picoSwarmInstrumentTable, "table")
  ETL_ENUM_TYPE(picoSwarmInstrumentTableAutomation, "table automation")

  ETL_ENUM_TYPE(Default, "   ")
  ETL_END_ENUM_TYPE
};

static_assert(sizeof(TrackerCommand) == sizeof(uint8_t),
              "Tracker commands are persisted as one byte");
static_assert(sizeof(FourCC) == sizeof(uint16_t),
              "Runtime IDs are expected to be two bytes");

static_assert(FourCC::VarChannel8Volume == FourCC::VarChannel1Volume + 7,
              "Channel volume IDs must remain contiguous");
static_assert(FourCC::VarChannel8FX4Send == FourCC::VarChannel1FX1Send + 31,
              "Channel FX send IDs must remain contiguous");
static_assert(FourCC::VarFX4Volume == FourCC::VarFX1Volume + 3,
              "FX return volume IDs must remain contiguous");
typedef unsigned short ushort;
typedef unsigned int uint;
typedef unsigned char uchar;

typedef uint32_t stereosample;

#endif
