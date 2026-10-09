
#ifndef KBMOD_AUDIO_H
#define KBMOD_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

// function initializes audio, and this will be
// called only once at startup, includes the
// starting volume or the volume set in the config
// file and the pitch_variation variable which also has
// a value set in the config or its default 0.0f. pitchvar
// is measured in semitones.
//
// however soon to be set in half-semitones [demisemitones]
// ... akin to a half-flat / half-sharp in music theory. this
// however will be experimented on to see if normal semitones
// distort the audio of the keys
void kbmod_audio_init(
  float volume,
  float base_pitch_semitones,
  float pitch_variation_semitones);

// base gain multiplied by the volume set in the config.yaml file
float kbmod_audio_apply_volume(float base_gain);

// this function uses rand() so NEVER call from the audio render
// callback due to unbounded timing
//
// this function is to be called when a key is struck before the
// audio/voice [mapped to the key] reaches the mixer
//
// function below rolls a random playback-rate multiplier for one
// new voice, which is dependent on the configured key_pitchvar
// inside of the config.yaml file
// ... where 1.0 plays the voice at normal pitch
float kbmod_audio_next_playback_rate(void);

#ifdef __cplusplus
}
#endif

#endif
