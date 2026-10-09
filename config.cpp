
#include "config.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>

/*
 * translates '~' in some paths to HOME
 * otherwise leaves the path unchanged
 */
static std::string kbmod_expand_home_directory(const std::string& PATH) {
  if (PATH.empty() || PATH[0] != '~') return PATH;

  const char* HOME_DIRECTORY = std::getenv("HOME");
  if (!HOME_DIRECTORY) return PATH;

  return std::string(HOME_DIRECTORY) + PATH.substr(1);
}

std::string kbmod_default_config_path() {
  return kbmod_expand_home_directory("~/.kbmod/config.yaml");
}

static std::string kbmod_trim_whitespace(const std::string& TEXT) {
  std::size_t START = 0;
  while (START < TEXT.size() && std::isspace(static_cast<unsigned char>(TEXT[START]))) ++START;

  std::size_t END = TEXT.size();
  while (END > START && std::isspace(static_cast<unsigned char>(TEXT[END - 1]))) --END;

  return TEXT.substr(START, END - START);
}

/*
 * function responsible for splitting a field into key and value
 * if there is a blank line, return null type nullopt
 * if there is a comment, ignore it
 * if there is a line that does not have a colon, return null
 * ... type nullopt
 * 
 * this is a mini version of a YAML parser [instead of importing
 * a whole library dedicated for this. this is because the config
 * file is very simple and does not need advanced YAML to configure]
 */
static std::optional<std::pair<std::string, std::string>> kbmod_parse_config_line(
  const std::string& RAW_LINE
) {
  std::string LINE = kbmod_trim_whitespace(RAW_LINE);
  if (LINE.empty() || LINE[0] == '#') return std::nullopt;

  std::size_t COLON_POSITION = LINE.find(':');
  if (COLON_POSITION == std::string::npos) return std::nullopt;

  std::string KEY = kbmod_trim_whitespace(LINE.substr(0, COLON_POSITION));
  std::string VALUE = kbmod_trim_whitespace(LINE.substr(COLON_POSITION + 1));
  if (KEY.empty() || VALUE.empty()) return std::nullopt;

  return std::make_pair(KEY, VALUE);
}

static float round_to_two_decimals(float VALUE) {
  return std::round(VALUE * 100.0f) / 100.0f;
}

static float clamp_float(float VALUE, float MINIMUM, float MAXIMUM) {
  if (VALUE < MINIMUM) return MINIMUM;
  if (VALUE > MAXIMUM) return MAXIMUM;
  
  return VALUE;
}

/*
 * function checks if the value to to fallback sound library
 * field is exactly 'nil'
 */
static bool is_nil_token(const std::string& VALUE) {
  if (VALUE.size() != 3) return false;
  return std::tolower(VALUE[0]) == 'n' && std::tolower(VALUE[1]) == 'i' && std::tolower(VALUE[2]) == 'l';
}

static void kbmod_apply_config_field(KBMOD_CONFIG& CONFIG, const std::string& KEY, const std::string& VALUE) {
  try {
    if (KEY == "key_pitch") CONFIG.BASE_PITCH = round_to_two_decimals(clamp_float(std::stof(VALUE), -24.f, 24.f));
    else if (KEY == "key_pitchvar") CONFIG.PITCH_VARIATION = round_to_two_decimals(clamp_float(std::stof(VALUE), 0.0f, 5.0f));
    else if (KEY == "key_volume") CONFIG.VOLUME = clamp_float(std::stof(VALUE), 0.0f, 1.0f);
    else if (KEY == "key_sound_library") CONFIG.SOUND_LIBRARY_PATH = kbmod_expand_home_directory(VALUE);
    else if (KEY == "key_fallback_sound_library") CONFIG.FALLBACK_SOUND_LIBRARY_PATH = is_nil_token(VALUE) ? "" : kbmod_expand_home_directory(VALUE);
    else { std::cerr << "kbmod: unknown config key '" << KEY << "', ignoring...\n"; }
  } catch (const std::exception&) {
    std::cerr << "kbmod: bad value for '" << KEY << "': " << VALUE << ", ignoring...\n";
  }
}

KBMOD_CONFIG kbmod_load_config_from_file(const std::string &PATH) {
  KBMOD_CONFIG CONFIG;

  std::ifstream CONFIG_FILE(PATH);
  if (!CONFIG_FILE.is_open()) return CONFIG;

  std::string LINE;
  while (std::getline(CONFIG_FILE, LINE)) {
    auto PARSED = kbmod_parse_config_line(LINE);
    if (PARSED) kbmod_apply_config_field(CONFIG, PARSED->first, PARSED->second);
  }

  return CONFIG;
}

