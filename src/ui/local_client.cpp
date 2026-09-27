/**
 * @file src/ui/local_client.cpp
 * @brief Definitions for the embedded UI's in-process Titan API client.
 */
#ifdef _WIN32

// standard includes
#include <algorithm>
#include <sstream>

// local includes
#include "local_client.h"
#include "src/config.h"
#include "src/file_handler.h"
#include "src/process.h"

using namespace std::literals;

namespace ui {

  std::optional<std::map<std::string, std::string>> LocalClient::get_config(std::string &error) {
    try {
      auto vars = config::parse_config(file_handler::read_file(config::sunshine.config_file.c_str()));
      return std::map<std::string, std::string> {vars.begin(), vars.end()};
    } catch (std::exception &e) {
      error = e.what();
      return std::nullopt;
    }
  }

  bool LocalClient::save_config(const std::map<std::string, std::string> &full_config, std::string &error) {
    try {
      // Mirrors confighttp.cpp's saveConfig(): full overwrite, empty values dropped.
      std::stringstream config_stream;
      for (const auto &[k, v] : full_config) {
        if (v.empty()) {
          continue;
        }
        config_stream << k << " = " << v << std::endl;
      }

      const std::scoped_lock lock(config::config_write_mutex);
      file_handler::write_file(config::sunshine.config_file.c_str(), config_stream.str());
      return true;
    } catch (std::exception &e) {
      error = e.what();
      return false;
    }
  }

  std::optional<std::vector<nlohmann::json>> LocalClient::get_apps(std::string &error) {
    try {
      auto content = file_handler::read_file(config::stream.file_apps.c_str());
      auto file_tree = nlohmann::json::parse(content);

      // Mirrors confighttp.cpp's getApps(): convert legacy string booleans/integers.
      const std::vector<std::string> boolean_keys = {"exclude-global-prep-cmd", "elevated", "auto-detach", "wait-all"};
      const std::vector<std::string> integer_keys = {"exit-timeout"};

      for (auto &app : file_tree["apps"]) {
        for (const auto &key : boolean_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = app[key] == "true";
          }
        }
        for (const auto &key : integer_keys) {
          if (app.contains(key) && app[key].is_string()) {
            app[key] = std::stoi(app[key].get<std::string>());
          }
        }
        if (app.contains("prep-cmd")) {
          for (auto &prep : app["prep-cmd"]) {
            if (prep.contains("elevated") && prep["elevated"].is_string()) {
              prep["elevated"] = prep["elevated"] == "true";
            }
          }
        }
      }

      std::vector<nlohmann::json> apps;
      for (auto &app : file_tree["apps"]) {
        apps.push_back(std::move(app));
      }
      return apps;
    } catch (std::exception &e) {
      error = e.what();
      return std::nullopt;
    }
  }

  bool LocalClient::save_app(const nlohmann::json &app, std::string &error) {
    try {
      // Mirrors confighttp.cpp's saveApp(): index -1 appends, otherwise replaces that
      // position, then re-sorts by name. `app` must contain an "index" field.
      nlohmann::json input_tree = app;
      if (input_tree.contains("prep-cmd") && input_tree["prep-cmd"].empty()) {
        input_tree.erase("prep-cmd");
      }
      if (input_tree.contains("detached") && input_tree["detached"].empty()) {
        input_tree.erase("detached");
      }

      const std::scoped_lock lock(config::config_write_mutex);

      auto content = file_handler::read_file(config::stream.file_apps.c_str());
      auto file_tree = nlohmann::json::parse(content);
      auto &apps_node = file_tree["apps"];

      int index = input_tree["index"].get<int>();
      input_tree.erase("index");

      if (index == -1) {
        apps_node.push_back(input_tree);
      } else {
        nlohmann::json new_apps = nlohmann::json::array();
        for (std::size_t i = 0; i < apps_node.size(); ++i) {
          new_apps.push_back(i == static_cast<std::size_t>(index) ? input_tree : apps_node[i]);
        }
        file_tree["apps"] = new_apps;
      }

      std::sort(file_tree["apps"].begin(), file_tree["apps"].end(), [](const nlohmann::json &a, const nlohmann::json &b) {
        return a["name"].get<std::string>() < b["name"].get<std::string>();
      });

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);
      return true;
    } catch (std::exception &e) {
      error = e.what();
      return false;
    }
  }

  bool LocalClient::delete_app(int index, std::string &error) {
    try {
      const std::scoped_lock lock(config::config_write_mutex);

      auto content = file_handler::read_file(config::stream.file_apps.c_str());
      auto file_tree = nlohmann::json::parse(content);
      auto &apps = file_tree["apps"];

      if (index < 0 || static_cast<std::size_t>(index) >= apps.size()) {
        error = "invalid app index";
        return false;
      }

      nlohmann::json new_apps = nlohmann::json::array();
      for (std::size_t i = 0; i < apps.size(); ++i) {
        if (i != static_cast<std::size_t>(index)) {
          new_apps.push_back(apps[i]);
        }
      }
      file_tree["apps"] = new_apps;

      file_handler::write_file(config::stream.file_apps.c_str(), file_tree.dump(4));
      proc::refresh(config::stream.file_apps);
      return true;
    } catch (std::exception &e) {
      error = e.what();
      return false;
    }
  }

}  // namespace ui

#endif
