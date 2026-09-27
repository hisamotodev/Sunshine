/**
 * @file src/ui/win_util.h
 * @brief Small Windows string/encoding helpers. Ported verbatim from config-gui/win_util.h.
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <cstdint>
#include <string>
#include <vector>

namespace ui::win_util {

  std::wstring utf8_to_wide(const std::string &utf8);
  std::string wide_to_utf8(const std::wstring &wide);

  // Base64 with no embedded line breaks (CRYPT_STRING_NOCRLF), suitable for
  // an HTTP header value or a single JSON string field.
  std::string base64_encode(const std::uint8_t *data, std::size_t size);
  std::vector<std::uint8_t> base64_decode(const std::string &encoded);

}  // namespace ui::win_util

#endif
