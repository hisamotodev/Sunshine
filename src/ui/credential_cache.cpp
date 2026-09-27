/**
 * @file src/ui/credential_cache.cpp
 * @brief Definitions for the cross-instance credential cache. Ported from
 *        config-gui/instance_store.cpp.
 */
#ifdef _WIN32

// standard includes
#include <fstream>
#include <sstream>

// lib includes
#include <nlohmann/json.hpp>

// clang-format off
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wincrypt.h>
// clang-format on

// local includes
#include "credential_cache.h"
#include "win_util.h"

namespace ui {

  namespace {

    std::filesystem::path credentials_file_path() {
      WCHAR local_appdata[MAX_PATH];
      auto len = GetEnvironmentVariableW(L"LOCALAPPDATA", local_appdata, _countof(local_appdata));
      if (len == 0 || len >= _countof(local_appdata)) {
        return std::filesystem::path(L"_credentials.json");
      }
      std::filesystem::path dir = std::filesystem::path {local_appdata} / L"Titan";
      std::error_code ec;
      std::filesystem::create_directories(dir, ec);
      return dir / L"_credentials.json";
    }

    std::string encrypt_password(const std::string &plaintext) {
      if (plaintext.empty()) {
        return {};
      }
      DATA_BLOB in {};
      in.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(plaintext.data()));
      in.cbData = static_cast<DWORD>(plaintext.size());

      DATA_BLOB out {};
      if (!CryptProtectData(&in, L"Titan cross-instance credential", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
      }
      const auto encoded = win_util::base64_encode(out.pbData, out.cbData);
      LocalFree(out.pbData);
      return encoded;
    }

    std::string decrypt_password(const std::string &encoded) {
      if (encoded.empty()) {
        return {};
      }
      const auto bytes = win_util::base64_decode(encoded);
      if (bytes.empty()) {
        return {};
      }

      DATA_BLOB in {};
      in.pbData = const_cast<BYTE *>(bytes.data());
      in.cbData = static_cast<DWORD>(bytes.size());

      DATA_BLOB out {};
      // Encrypted by a different Windows account/machine (or corrupted) -> fail closed
      // and just leave the password blank rather than crash.
      if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        return {};
      }
      std::string plaintext(reinterpret_cast<char *>(out.pbData), out.cbData);
      LocalFree(out.pbData);
      return plaintext;
    }

  }  // namespace

  CredentialCache::CredentialCache() {
    path_ = credentials_file_path();
    load();
  }

  void CredentialCache::load() {
    credentials_.clear();

    std::ifstream file(path_, std::ios::binary);
    if (!file) {
      return;
    }
    std::ostringstream contents;
    contents << file.rdbuf();

    try {
      const auto parsed = nlohmann::json::parse(contents.str());
      for (const auto &entry : parsed) {
        CachedCredential credential;
        credential.instance_key = entry.value("instance_key", "");
        credential.username = entry.value("username", "");
        credential.password = decrypt_password(entry.value("encrypted_password", ""));
        credentials_.push_back(std::move(credential));
      }
    } catch (const std::exception &) {
      credentials_.clear();
    }
  }

  std::optional<CachedCredential> CredentialCache::find(const std::string &instance_key) const {
    for (const auto &credential : credentials_) {
      if (credential.instance_key == instance_key) {
        return credential;
      }
    }
    return std::nullopt;
  }

  void CredentialCache::upsert(const CachedCredential &credential) {
    for (auto &existing : credentials_) {
      if (existing.instance_key == credential.instance_key) {
        existing = credential;
        return;
      }
    }
    credentials_.push_back(credential);
  }

  bool CredentialCache::save(std::string &error) const {
    nlohmann::json array = nlohmann::json::array();
    for (const auto &credential : credentials_) {
      nlohmann::json entry;
      entry["instance_key"] = credential.instance_key;
      entry["username"] = credential.username;
      entry["encrypted_password"] = encrypt_password(credential.password);
      array.push_back(entry);
    }

    std::ofstream file(path_, std::ios::binary | std::ios::trunc);
    if (!file) {
      error = "Failed to open _credentials.json for writing";
      return false;
    }
    file << array.dump(2);
    return true;
  }

}  // namespace ui

#endif
