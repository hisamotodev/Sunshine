/**
 * @file src/ui/win_util.cpp
 * @brief Definitions for small Windows string/encoding helpers. Ported verbatim from
 *        config-gui/win_util.cpp.
 */
#ifdef _WIN32

// local includes
#include "win_util.h"

// clang-format off
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
// clang-format on

namespace ui::win_util {

  std::wstring utf8_to_wide(const std::string &utf8) {
    if (utf8.empty()) {
      return {};
    }
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), needed);
    return wide;
  }

  std::string wide_to_utf8(const std::wstring &wide) {
    if (wide.empty()) {
      return {};
    }
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), utf8.data(), needed, nullptr, nullptr);
    return utf8;
  }

  std::string base64_encode(const std::uint8_t *data, std::size_t size) {
    DWORD out_len = 0;
    CryptBinaryToStringA(data, static_cast<DWORD>(size), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &out_len);
    std::string encoded(out_len, '\0');
    CryptBinaryToStringA(data, static_cast<DWORD>(size), CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, encoded.data(), &out_len);
    // CryptBinaryToStringA includes the terminating NUL in out_len.
    if (!encoded.empty() && encoded.back() == '\0') {
      encoded.pop_back();
    }
    return encoded;
  }

  std::vector<std::uint8_t> base64_decode(const std::string &encoded) {
    DWORD out_len = 0;
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, nullptr, &out_len, nullptr, nullptr)) {
      return {};
    }
    std::vector<std::uint8_t> decoded(out_len);
    if (!CryptStringToBinaryA(encoded.data(), static_cast<DWORD>(encoded.size()), CRYPT_STRING_BASE64, decoded.data(), &out_len, nullptr, nullptr)) {
      return {};
    }
    decoded.resize(out_len);
    return decoded;
  }

}  // namespace ui::win_util

#endif
