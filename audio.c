
#include "audio.h"

#include <math.h>
#include <stdlib.h>
#include <time.h>

// read-only after being set by kbmod_audio_unit() [see audio.h]
static float CONFIGURED_VOLUME = 1.0f;
static float CONFIGURED_BASE_PITCH_SEMITONES = 0.f;
static float CONFIGURED_PITCH_VARIATION_SEMITONES = 0.0f;

float kbmod_audio_apply_volume(float base_gain) {
  return base_gain * CONFIGURED_VOLUME;
}

// a semitone has a factor of 2^{\frac{1}{12}} as pitch
// is logarithmic
static float kbmod_semitones_to_playback_rate(float semitones) {
  return powf(2.0f, semitones / 12.0f);
}

static float kbmod_random_unit_offset(void) {
  return ((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f;
}

float kbmod_audio_next_playback_rate(void) {
  float variation_offset = 0.f;
  if (CONFIGURED_PITCH_VARIATION_SEMITONES > 0.0f)
    variation_offset = kbmod_random_unit_offset() * CONFIGURED_PITCH_VARIATION_SEMITONES;

  float total_semitones = CONFIGURED_BASE_PITCH_SEMITONES + variation_offset;
  return kbmod_semitones_to_playback_rate(total_semitones);
}

void kbmod_audio_init(
  float volume,
  float base_pitch_semitones,
  float pitch_variation_semitones) {
  CONFIGURED_VOLUME = volume;
  CONFIGURED_BASE_PITCH_SEMITONES = base_pitch_semitones;
  CONFIGURED_PITCH_VARIATION_SEMITONES = pitch_variation_semitones;
  srand((unsigned int)time(NULL));
}

