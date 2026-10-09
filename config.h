
#pragma once

#include <string>

/*
 * config params
 */
struct KBMOD_CONFIG {
  float BASE_PITCH = 0.0f;
  float PITCH_VARIATION = 0.0f;
  float VOLUME = 1.0f;
  std::string SOUND_LIBRARY_PATH = "/usr/local/share/kbmod/turquoise";
  // empty for now as this can have either a valid path
  // or none ['nil']
  std::string FALLBACK_SOUND_LIBRARY_PATH;
};

// highlights the path where the config
// file [config.yaml] lives
std::string kbmod_default_config_path();

// function below reads the config file and interprets as
// a KBMOD_CONFIG struct
//
// if the config file is invalid or missing then it will
// be assigned the default values of KBMOD_CONFIG [see above
// the default values and paths]
KBMOD_CONFIG kbmod_load_config_from_file(const std::string& PATH);
