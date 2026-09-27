/**
 * @file src/ui/credential_cache.h
 * @brief Caches credentials for other discovered Titan instances (never the local/self
 *        instance, which needs none - it's already authenticated in-process). Ported
 *        from config-gui/instance_store.h.
 */
#pragma once

#ifdef _WIN32

// standard includes
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ui {

  struct CachedCredential {
    std::string instance_key;  ///< Matches a discovery::Instance's `name` (its AppData/Local directory name).
    std::string username;
    std::string password;  ///< Plaintext only in memory; encrypted at rest via DPAPI.
  };

  // Local, per-Windows-account cache of credentials for other Titan instances this one
  // has connected to before, so the user only has to type them once. Backed by
  // %LOCALAPPDATA%\Titan\_credentials.json; passwords are encrypted with CryptProtectData
  // (DPAPI, current-user scope) before being written.
  class CredentialCache {
  public:
    CredentialCache();

    std::optional<CachedCredential> find(const std::string &instance_key) const;
    void upsert(const CachedCredential &credential);
    bool save(std::string &error) const;

  private:
    void load();

    std::filesystem::path path_;
    std::vector<CachedCredential> credentials_;
  };

}  // namespace ui

#endif
