#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreGraphics/CoreGraphics.h>
#include <CoreFoundation/CoreFoundation.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#include "audio.h"
#include "config.h"

// Fixed output format we force every decoded file into, so the audio callback
// never has to deal with sample rate or channel conversions at runtime.
constexpr UInt32 OUTPUT_SAMPLE_RATE = 48000;
constexpr UInt32 OUTPUT_CHANNEL_COUNT = 2;

// Bounded pool size so memory never grows while typing fast.
constexpr int MAXIMUM_SIMULTANEOUS_VOICES = 32;

// Release sounds are physically quieter than press sounds on a real board.
constexpr float RELEASE_SOUND_GAIN = 0.65f;
constexpr float PRESS_SOUND_GAIN = 1.0f;

// How many frames we ask the decoder for per read while loading a file.
constexpr UInt32 FRAMES_PER_DECODER_READ = 4096;

// A fully decoded sound sitting in RAM, ready to be mixed without any I/O.
struct SOUND_CLIP {
  std::vector<float> INTERLEAVED_SAMPLES;
  uint32_t CHANNEL_COUNT = 0;
  uint32_t SAMPLE_RATE = 0;
};

// One currently-playing instance of a clip. Several can overlap at once.
struct VOICE_SLOT {
  const SOUND_CLIP* PLAYING_CLIP = nullptr;
  float PLAYBACK_RATE = 1.0f;
  double CURRENT_FRAME = 0;
  float VOICE_GAIN = 1.0f;
  std::atomic<bool> IS_ACTIVE{false};
};

// Global state: every decoded clip keyed by name, plus a fixed voice pool.
static std::unordered_map<std::string, SOUND_CLIP> LOADED_SOUNDS;
static std::array<VOICE_SLOT, MAXIMUM_SIMULTANEOUS_VOICES> VOICE_POOL;

// Logical keyboard rows, used to pick the right press sample.
enum class KEY_ROW {
  NUMBER_ROW,
  TOP_LETTER_ROW,
  HOME_LETTER_ROW,
  BOTTOM_LETTER_ROW,
  MODIFIER_ROW,
  SPACE_KEY,
  ENTER_KEY,
  BACKSPACE_KEY,
  UNKNOWN_KEY
};

static AudioStreamBasicDescription make_client_format() {
  // AudioToolbox needs this description to hand us float32 stereo at our rate.
  AudioStreamBasicDescription CLIENT_FORMAT{};
  CLIENT_FORMAT.mSampleRate = OUTPUT_SAMPLE_RATE;
  CLIENT_FORMAT.mFormatID = kAudioFormatLinearPCM;
  CLIENT_FORMAT.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
  CLIENT_FORMAT.mChannelsPerFrame = OUTPUT_CHANNEL_COUNT;
  CLIENT_FORMAT.mBitsPerChannel = 32;
  CLIENT_FORMAT.mFramesPerPacket = 1;
  CLIENT_FORMAT.mBytesPerFrame = OUTPUT_CHANNEL_COUNT * sizeof(float);
  CLIENT_FORMAT.mBytesPerPacket = CLIENT_FORMAT.mBytesPerFrame;
  return CLIENT_FORMAT;
}

static ExtAudioFileRef open_audio_file_for(const std::string& FILE_PATH) {
  // Bridge our std::string path into a CFURL, which is what AudioToolbox wants.
  CFURLRef FILE_URL = CFURLCreateFromFileSystemRepresentation(
    nullptr,
    reinterpret_cast<const UInt8*>(FILE_PATH.c_str()),
    FILE_PATH.size(),
    false);
  if (!FILE_URL) return nullptr;

  ExtAudioFileRef AUDIO_FILE = nullptr;
  if (ExtAudioFileOpenURL(FILE_URL, &AUDIO_FILE) != noErr) AUDIO_FILE = nullptr;
  CFRelease(FILE_URL);
  return AUDIO_FILE;
}

static void append_decoded_chunk(ExtAudioFileRef AUDIO_FILE, SOUND_CLIP& CLIP,
                                 std::vector<float>& DECODE_BUFFER) {
  AudioBufferList BUFFER_LIST{};
  BUFFER_LIST.mNumberBuffers = 1;
  BUFFER_LIST.mBuffers[0].mNumberChannels = OUTPUT_CHANNEL_COUNT;
  BUFFER_LIST.mBuffers[0].mDataByteSize =
    FRAMES_PER_DECODER_READ * OUTPUT_CHANNEL_COUNT * sizeof(float);
  BUFFER_LIST.mBuffers[0].mData = DECODE_BUFFER.data();

  UInt32 FRAMES_READ = FRAMES_PER_DECODER_READ;
  if (ExtAudioFileRead(AUDIO_FILE, &FRAMES_READ, &BUFFER_LIST) != noErr) return;
  if (FRAMES_READ == 0) return;

  CLIP.INTERLEAVED_SAMPLES.insert(
    CLIP.INTERLEAVED_SAMPLES.end(),
    DECODE_BUFFER.data(),
    DECODE_BUFFER.data() + FRAMES_READ * OUTPUT_CHANNEL_COUNT);
}

static void load_sound(const std::string& LOOKUP_KEY, const std::string& FILE_PATH) {
  ExtAudioFileRef AUDIO_FILE = open_audio_file_for(FILE_PATH);
  if (!AUDIO_FILE) return;

  AudioStreamBasicDescription CLIENT_FORMAT = make_client_format();
  ExtAudioFileSetProperty(AUDIO_FILE, kExtAudioFileProperty_ClientDataFormat,
                          sizeof(CLIENT_FORMAT), &CLIENT_FORMAT);

  SOUND_CLIP CLIP;
  CLIP.CHANNEL_COUNT = OUTPUT_CHANNEL_COUNT;
  CLIP.SAMPLE_RATE = OUTPUT_SAMPLE_RATE;

  // Reusable scratch buffer, so we allocate once per file, not once per chunk.
  std::vector<float> DECODE_BUFFER(FRAMES_PER_DECODER_READ * OUTPUT_CHANNEL_COUNT);
  while (true) {
    std::size_t BEFORE = CLIP.INTERLEAVED_SAMPLES.size();
    append_decoded_chunk(AUDIO_FILE, CLIP, DECODE_BUFFER);
    if (CLIP.INTERLEAVED_SAMPLES.size() == BEFORE) break;
  }

  ExtAudioFileDispose(AUDIO_FILE);
  LOADED_SOUNDS.emplace(LOOKUP_KEY, std::move(CLIP));
}

static void assign_voice(
  VOICE_SLOT& SLOT,
  const SOUND_CLIP* CLIP_TO_PLAY,
  float GAIN_FOR_THIS_VOICE,
  float PLAYBACK_RATE_FOR_THIS_VOICE
) {
  SLOT.PLAYING_CLIP = CLIP_TO_PLAY;
  SLOT.CURRENT_FRAME = 0;
  SLOT.VOICE_GAIN = GAIN_FOR_THIS_VOICE;
  SLOT.PLAYBACK_RATE = PLAYBACK_RATE_FOR_THIS_VOICE;
}

static void start_voice(const SOUND_CLIP* CLIP_TO_PLAY, float GAIN_FOR_THIS_VOICE) {
  if (!CLIP_TO_PLAY || CLIP_TO_PLAY->INTERLEAVED_SAMPLES.empty()) return;

  float PLAYBACK_RATE = kbmod_audio_next_playback_rate();

  // Claim the first free slot atomically; the audio thread checks the same flag.
  for (auto& SLOT : VOICE_POOL) {
    bool EXPECTED_INACTIVE = false;
    if (SLOT.IS_ACTIVE.compare_exchange_strong(EXPECTED_INACTIVE, true)) {
      assign_voice(SLOT, CLIP_TO_PLAY, GAIN_FOR_THIS_VOICE, PLAYBACK_RATE);
      return;
    }
  }

  assign_voice(VOICE_POOL[0], CLIP_TO_PLAY, GAIN_FOR_THIS_VOICE, PLAYBACK_RATE);
}

/*
 * reads one channel's sample at a fractional frame position
 * lerping between the two nearest real samples
 * 
 * which prevents a crackle when playing a sample on pressing
 * a key
 */
static float sample_at_fractional_frame(
  const SOUND_CLIP* CLIP,
  uint32_t SOURCE_CHANNEL,
  double FRACTIONAL_FRAME,
  std::size_t TOTAL_CLIP_FRAMES
) {
  std::size_t FRAME_LOW = static_cast<std::size_t>(FRACTIONAL_FRAME);
  std::size_t FRAME_HIGH = FRAME_LOW + 1;
  if (FRAME_HIGH >= TOTAL_CLIP_FRAMES) FRAME_HIGH = FRAME_LOW;

  float SAMPLE_LOW = CLIP->INTERLEAVED_SAMPLES[FRAME_LOW * CLIP->CHANNEL_COUNT + SOURCE_CHANNEL];
  float SAMPLE_HIGH = CLIP->INTERLEAVED_SAMPLES[FRAME_HIGH * CLIP->CHANNEL_COUNT + SOURCE_CHANNEL];

  double BLEND = FRACTIONAL_FRAME - static_cast<double>(FRAME_LOW);

  return static_cast<float>(SAMPLE_LOW + (SAMPLE_HIGH - SAMPLE_LOW) * BLEND);
}

static void mix_one_frame(
  const SOUND_CLIP* CLIP,
  double FRACTIONAL_FRAME,
  std::size_t TOTAL_CLIP_FRAMES,
  float* OUTPUT_FRAME,
  uint32_t OUTPUT_CHANNELS,
  float GAIN
) {
  for (uint32_t CHANNEL = 0; CHANNEL < OUTPUT_CHANNELS; ++CHANNEL) {
    uint32_t SOURCE_CHANNEL = (CHANNEL < CLIP->CHANNEL_COUNT) ? CHANNEL : CLIP->CHANNEL_COUNT - 1;
    OUTPUT_FRAME[CHANNEL] += sample_at_fractional_frame(CLIP, SOURCE_CHANNEL, FRACTIONAL_FRAME, TOTAL_CLIP_FRAMES) * GAIN;
  }
}

static void mix_one_voice(VOICE_SLOT& SLOT, float* OUTPUT_SAMPLES,
                          uint32_t OUTPUT_CHANNELS, UInt32 FRAME_COUNT) {
  const SOUND_CLIP* CLIP = SLOT.PLAYING_CLIP;
  const std::size_t TOTAL_CLIP_FRAMES =
    CLIP->INTERLEAVED_SAMPLES.size() / CLIP->CHANNEL_COUNT;

  for (UInt32 FRAME = 0; FRAME < FRAME_COUNT && SLOT.CURRENT_FRAME < TOTAL_CLIP_FRAMES;
       ++FRAME) {
    mix_one_frame(CLIP, SLOT.CURRENT_FRAME, TOTAL_CLIP_FRAMES, OUTPUT_SAMPLES + FRAME * OUTPUT_CHANNELS, OUTPUT_CHANNELS, SLOT.VOICE_GAIN);
    SLOT.CURRENT_FRAME += SLOT.PLAYBACK_RATE;
  }

  if (SLOT.CURRENT_FRAME >= TOTAL_CLIP_FRAMES) SLOT.IS_ACTIVE.store(false);
}

static void soft_clip_output(float* OUTPUT_SAMPLES, std::size_t TOTAL_OUTPUT_SAMPLES) {
  // Hard clamp keeps many overlapping keys from producing digital distortion.
  for (std::size_t SAMPLE_INDEX = 0; SAMPLE_INDEX < TOTAL_OUTPUT_SAMPLES; ++SAMPLE_INDEX) {
    float SAMPLE = OUTPUT_SAMPLES[SAMPLE_INDEX];
    if (SAMPLE > 1.0f) SAMPLE = 1.0f;
    if (SAMPLE < -1.0f) SAMPLE = -1.0f;
    OUTPUT_SAMPLES[SAMPLE_INDEX] = SAMPLE;
  }
}

static void silence_output_buffers(AudioBufferList* OUTPUT_BUFFERS) {
  // Start from silence so we only add voice contributions on top.
  for (UInt32 BUFFER_INDEX = 0; BUFFER_INDEX < OUTPUT_BUFFERS->mNumberBuffers; ++BUFFER_INDEX) {
    std::memset(
      OUTPUT_BUFFERS->mBuffers[BUFFER_INDEX].mData,
      0,
      OUTPUT_BUFFERS->mBuffers[BUFFER_INDEX].mDataByteSize);
  }
}

static OSStatus audio_render_callback(
  void*,
  AudioUnitRenderActionFlags*,
  const AudioTimeStamp*,
  UInt32,
  UInt32 FRAME_COUNT,
  AudioBufferList* OUTPUT_BUFFERS)
{
  silence_output_buffers(OUTPUT_BUFFERS);

  float* OUTPUT_SAMPLES = static_cast<float*>(OUTPUT_BUFFERS->mBuffers[0].mData);
  const uint32_t OUTPUT_CHANNELS = OUTPUT_BUFFERS->mBuffers[0].mNumberChannels;

  for (auto& SLOT : VOICE_POOL) {
    if (!SLOT.IS_ACTIVE.load()) continue;
    mix_one_voice(SLOT, OUTPUT_SAMPLES, OUTPUT_CHANNELS, FRAME_COUNT);
  }

  soft_clip_output(OUTPUT_SAMPLES, std::size_t(FRAME_COUNT) * OUTPUT_CHANNELS);
  return noErr;
}

struct KEYCODE_RANGE {
  uint16_t LOW;
  uint16_t HIGH;
  KEY_ROW ROW;
};

static constexpr KEYCODE_RANGE KEYCODE_ROW_TABLE[] = {
  {0x33, 0x33, KEY_ROW::BACKSPACE_KEY},
  {0x24, 0x24, KEY_ROW::ENTER_KEY},
  {0x4C, 0x4C, KEY_ROW::ENTER_KEY},
  {0x31, 0x31, KEY_ROW::SPACE_KEY},
 
  // Number row: 1..0, minus, equals.
  {0x12, 0x1D, KEY_ROW::NUMBER_ROW},
  {0x1B, 0x1B, KEY_ROW::NUMBER_ROW},
  {0x18, 0x18, KEY_ROW::NUMBER_ROW},
 
  // Top letter row: Q..P, [ ], backslash.
  {0x0C, 0x23, KEY_ROW::TOP_LETTER_ROW},
  {0x2A, 0x2A, KEY_ROW::TOP_LETTER_ROW},
 
  // Home row: A..V, K, L, semicolon, quote.
  {0x00, 0x09, KEY_ROW::HOME_LETTER_ROW},
  {0x25, 0x29, KEY_ROW::HOME_LETTER_ROW},
 
  // Bottom row: Z..V, B, N..M, comma, period, slash.
  {0x0B, 0x0B, KEY_ROW::BOTTOM_LETTER_ROW},
  {0x2B, 0x2F, KEY_ROW::BOTTOM_LETTER_ROW},
 
  // Modifiers + bottom cluster + arrows.
  {0x30, 0x3E, KEY_ROW::MODIFIER_ROW},
  {0x7B, 0x7E, KEY_ROW::MODIFIER_ROW},
};

static KEY_ROW classify_keycode(uint16_t KEYCODE) {
  for (const auto& RANGE : KEYCODE_ROW_TABLE) {
    if (KEYCODE >= RANGE.LOW && KEYCODE <= RANGE.HIGH) return RANGE.ROW;
  } return KEY_ROW::UNKNOWN_KEY;
}

static std::string press_sound_key_for(KEY_ROW ROW) {
  switch (ROW) {
    case KEY_ROW::NUMBER_ROW: return "press_number_row";
    case KEY_ROW::TOP_LETTER_ROW: return "press_top_letter_row";
    case KEY_ROW::HOME_LETTER_ROW: return "press_home_letter_row";
    case KEY_ROW::BOTTOM_LETTER_ROW: return "press_bottom_letter_row";
    case KEY_ROW::MODIFIER_ROW: return "press_modifier_row";
    case KEY_ROW::SPACE_KEY: return "press_space_key";
    case KEY_ROW::ENTER_KEY: return "press_enter_key";
    case KEY_ROW::BACKSPACE_KEY: return "press_backspace_key";
    default: return {};
  }
}

static std::string release_sound_key_for(KEY_ROW ROW) {
  switch (ROW) {
    case KEY_ROW::SPACE_KEY: return "release_space_key";
    case KEY_ROW::ENTER_KEY: return "release_enter_key";
    case KEY_ROW::BACKSPACE_KEY: return "release_backspace_key";
    case KEY_ROW::UNKNOWN_KEY: return {};
    default: return "release_generic";
  }
}

static void play_press_for(KEY_ROW ROW) {
  auto FOUND_SOUND = LOADED_SOUNDS.find(press_sound_key_for(ROW));
  if (FOUND_SOUND == LOADED_SOUNDS.end()) return;
  start_voice(&FOUND_SOUND->second, kbmod_audio_apply_volume(PRESS_SOUND_GAIN));
}

static void play_release_for(KEY_ROW ROW) {
  auto FOUND_SOUND = LOADED_SOUNDS.find(release_sound_key_for(ROW));
  if (FOUND_SOUND == LOADED_SOUNDS.end()) return;
  start_voice(&FOUND_SOUND->second, kbmod_audio_apply_volume(RELEASE_SOUND_GAIN));
}

static void handle_modifier_change(CGEventRef KEYBOARD_EVENT) {
  // Modifier keys report state via flags, not keydown/keyup.
  CGEventFlags FLAGS = CGEventGetFlags(KEYBOARD_EVENT);
  static CGEventFlags PREVIOUS_FLAGS = 0;

  struct { CGEventFlags MASK; KEY_ROW ROW; } MODIFIER_KEYS[] = {
    { kCGEventFlagMaskShift,     KEY_ROW::MODIFIER_ROW },
    { kCGEventFlagMaskControl,   KEY_ROW::MODIFIER_ROW },
    { kCGEventFlagMaskAlternate, KEY_ROW::MODIFIER_ROW },
    { kCGEventFlagMaskCommand,   KEY_ROW::MODIFIER_ROW },
    { kCGEventFlagMaskAlphaShift,KEY_ROW::MODIFIER_ROW },
  };

  for (const auto& MODIFIER : MODIFIER_KEYS) {
    bool WAS_DOWN = (PREVIOUS_FLAGS & MODIFIER.MASK) != 0;
    bool IS_DOWN  = (FLAGS          & MODIFIER.MASK) != 0;
    if (IS_DOWN && !WAS_DOWN) play_press_for(MODIFIER.ROW);
    if (!IS_DOWN && WAS_DOWN) play_release_for(MODIFIER.ROW);
  }

  PREVIOUS_FLAGS = FLAGS;
}

static CGEventRef keyboard_event_callback(
  CGEventTapProxy,
  CGEventType EVENT_TYPE,
  CGEventRef KEYBOARD_EVENT,
  void*)
{
  if (EVENT_TYPE == kCGEventFlagsChanged) {
    handle_modifier_change(KEYBOARD_EVENT);
    return KEYBOARD_EVENT;
  }

  if (EVENT_TYPE != kCGEventKeyDown && EVENT_TYPE != kCGEventKeyUp) return KEYBOARD_EVENT;

  uint16_t KEYCODE = uint16_t(CGEventGetIntegerValueField(
    KEYBOARD_EVENT, kCGKeyboardEventKeycode));
  KEY_ROW ROW = classify_keycode(KEYCODE);

  if (EVENT_TYPE == kCGEventKeyDown) {
    int64_t IS_AUTO_REPEAT = CGEventGetIntegerValueField(
      KEYBOARD_EVENT, kCGKeyboardEventAutorepeat);
    if (!IS_AUTO_REPEAT) play_press_for(ROW);
  } else {
    play_release_for(ROW);
  }

  return KEYBOARD_EVENT;
}

static AURenderCallbackStruct make_render_callback_struct() {
  AURenderCallbackStruct RENDER_CALLBACK{};
  RENDER_CALLBACK.inputProc = audio_render_callback;
  RENDER_CALLBACK.inputProcRefCon = nullptr;
  return RENDER_CALLBACK;
}

static AudioUnit start_audio_output_unit() {
  AudioComponentDescription COMPONENT_DESCRIPTION{};
  COMPONENT_DESCRIPTION.componentType = kAudioUnitType_Output;
  COMPONENT_DESCRIPTION.componentSubType = kAudioUnitSubType_DefaultOutput;
  COMPONENT_DESCRIPTION.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent COMPONENT = AudioComponentFindNext(nullptr, &COMPONENT_DESCRIPTION);
  if (!COMPONENT) std::exit(1);

  AudioUnit AUDIO_UNIT = nullptr;
  if (AudioComponentInstanceNew(COMPONENT, &AUDIO_UNIT) != noErr) std::exit(1);

  AudioStreamBasicDescription STREAM_FORMAT = make_client_format();
  AudioUnitSetProperty(AUDIO_UNIT, kAudioUnitProperty_StreamFormat,
                       kAudioUnitScope_Input, 0, &STREAM_FORMAT, sizeof(STREAM_FORMAT));

  AURenderCallbackStruct RENDER_CALLBACK = make_render_callback_struct();
  AudioUnitSetProperty(AUDIO_UNIT, kAudioUnitProperty_SetRenderCallback,
                       kAudioUnitScope_Input, 0, &RENDER_CALLBACK, sizeof(RENDER_CALLBACK));

  AudioUnitInitialize(AUDIO_UNIT);
  AudioOutputUnitStart(AUDIO_UNIT);
  return AUDIO_UNIT;
}

struct SOUND_FILE_ENTRY {
  const char* LOOKUP_KEY;
  const char* RELATIVE_PATH;
};

static void load_sound_set(const std::string& ASSETS_FOLDER,
                           const SOUND_FILE_ENTRY* ENTRIES,
                           std::size_t ENTRY_COUNT) {
  for (std::size_t INDEX = 0; INDEX < ENTRY_COUNT; ++INDEX) {
    const SOUND_FILE_ENTRY& ENTRY = ENTRIES[INDEX];

    load_sound(
      ENTRY.LOOKUP_KEY,
      ASSETS_FOLDER + "/" + ENTRIES[INDEX].RELATIVE_PATH
    );
  }
}

static void load_missing_sounds_from_fallback(
  const std::string& FALLBACK_FOLDER,
  const SOUND_FILE_ENTRY* ENTRIES,
  std::size_t ENTRY_COUNT
) {
  for (std::size_t INDEX = 0; INDEX < ENTRY_COUNT; ++INDEX) {
    const SOUND_FILE_ENTRY& ENTRY = ENTRIES[INDEX];
    if (LOADED_SOUNDS.count(ENTRY.LOOKUP_KEY) > 0) continue;

    load_sound(ENTRY.LOOKUP_KEY, FALLBACK_FOLDER + "/" + ENTRY.RELATIVE_PATH);
  }
}

static void load_all_sounds_from_disk(const KBMOD_CONFIG& CONFIG) {
  static constexpr SOUND_FILE_ENTRY PRESS_FILES[] = {
    {"press_number_row", "press/GENERIC_R0.mp3"},
    {"press_top_letter_row", "press/GENERIC_R1.mp3"},
    {"press_home_letter_row", "press/GENERIC_R2.mp3"},
    {"press_bottom_letter_row", "press/GENERIC_R3.mp3"},
    {"press_modifier_row", "press/GENERIC_R4.mp3"},
    {"press_space_key", "press/SPACE.mp3"},
    {"press_enter_key", "press/ENTER.mp3"},
    {"press_backspace_key", "press/BACKSPACE.mp3"},
  };

  static constexpr SOUND_FILE_ENTRY RELEASE_FILES[] = {
    {"release_generic", "release/GENERIC.mp3"},
    {"release_space_key", "release/SPACE.mp3"},
    {"release_enter_key", "release/ENTER.mp3"},
    {"release_backspace_key", "release/BACKSPACE.mp3"},
  };

  load_sound_set(
    CONFIG.SOUND_LIBRARY_PATH,
    PRESS_FILES,
    std::size(PRESS_FILES)
  );
  load_sound_set(
    CONFIG.SOUND_LIBRARY_PATH,
    RELEASE_FILES,
    std::size(RELEASE_FILES)
  );

  if (CONFIG.FALLBACK_SOUND_LIBRARY_PATH.empty()) return;

  load_missing_sounds_from_fallback(
    CONFIG.FALLBACK_SOUND_LIBRARY_PATH,
    PRESS_FILES,
    std::size(PRESS_FILES)
  );
  load_missing_sounds_from_fallback(
    CONFIG.FALLBACK_SOUND_LIBRARY_PATH, 
    RELEASE_FILES,
    std::size(RELEASE_FILES)
  );
}

static void install_keyboard_event_tap() {

  CGEventMask EVENT_MASK =
    CGEventMaskBit(kCGEventKeyDown) |
    CGEventMaskBit(kCGEventKeyUp) |
    CGEventMaskBit(kCGEventFlagsChanged);

  CFMachPortRef EVENT_TAP = CGEventTapCreate(
    kCGSessionEventTap,
    kCGHeadInsertEventTap,
    kCGEventTapOptionListenOnly,
    EVENT_MASK,
    keyboard_event_callback,
    nullptr);

  if (!EVENT_TAP) {
    std::cerr << "Grant Accessibility permission to this binary and rerun.\n";
    std::exit(1);
  }

  CFRunLoopSourceRef EVENT_SOURCE = CFMachPortCreateRunLoopSource(nullptr, EVENT_TAP, 0);
  CFRunLoopAddSource(CFRunLoopGetCurrent(), EVENT_SOURCE, kCFRunLoopCommonModes);
  CGEventTapEnable(EVENT_TAP, true);
}

int main() {
  KBMOD_CONFIG CONFIG = kbmod_load_config_from_file(
    kbmod_default_config_path()
  ); kbmod_audio_init(CONFIG.VOLUME, CONFIG.BASE_PITCH, CONFIG.PITCH_VARIATION);
  
  // Decode everything up front so key presses never touch the disk.
  load_all_sounds_from_disk(CONFIG);

  // Bring the audio unit up before the tap, so the first key already has output.
  start_audio_output_unit();

  install_keyboard_event_tap();
  CFRunLoopRun();
  return 0;
}
