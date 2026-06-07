#include "LocaleConfigManager.h"
#include "GlobalConfigManager.h"
#include <tinyxml2.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#if defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif
#include <spdlog/spdlog.h>

LocaleConfigManager::LocaleConfigManager() = default;

// Helper: check if a file exists using std::filesystem
static bool FileExists(const std::string &path) {
#if defined(__EMSCRIPTEN__)
    return access(path.c_str(), F_OK) == 0;
#else
    std::error_code ec;
    return std::filesystem::exists(path, ec);
#endif
}

// Helper: read entire file to string
static std::string ReadFileToString(const std::string &path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return "";
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

std::string LocaleConfigManager::GetFilePath() {
    constexpr const char *kPathPrefix = "locale/";
    const std::string requested = std::string(kPathPrefix) + currentLangCode + ".xml";
    if (FileExists(requested)) {
        return requested;
    }

    // Fallback to default locale once.
    const std::string fallback = std::string(kPathPrefix) + "en_us.xml";
    if (FileExists(fallback)) {
        currentLangCode = "en_us";
        return fallback;
    }

    return "";
}

LocaleConfigManager *LocaleConfigManager::GetInstance() {
    static LocaleConfigManager instance;
    return &instance;
}

const std::string &LocaleConfigManager::GetText(const std::string &tid) {
    auto it = AllConfig.find(tid);
    if (it == AllConfig.end()) {
        AllConfig[tid] = tid;
        return AllConfig[tid];
    }
    return it->second;
}

void LocaleConfigManager::Initialize(const std::string &sysLang) {
    // override by global configured lang
    currentLangCode = GlobalConfigManager::GetInstance()->GetValue<std::string>(
        "user_language", "");
    if (currentLangCode.empty())
        currentLangCode = sysLang;
    AllConfig.clear();
    AllConfig.reserve(128);

    // Built-in English defaults so that UI text is readable even when
    // the locale XML file is missing.
    AllConfig["msgbox_ok"]            = "OK";
    AllConfig["msgbox_yes"]           = "Yes";
    AllConfig["msgbox_no"]            = "No";
    AllConfig["cancel"]               = "Cancel";
    AllConfig["retry"]                = "Retry";
    AllConfig["err_no_memory"]        = "Insufficient memory";
    AllConfig["err_occured"]          = "Error";
    AllConfig["crash_report"]         = "Crash Report";
    AllConfig["crash_report_msg"]     = "An unexpected error has occurred. Would you like to send a crash report?";
    AllConfig["startup_patch_fail"]   = "Failed to apply startup patch.";
    AllConfig["browse_patch_lib"]     = "Browse Patch Library";
    AllConfig["use_internal_path"]    = "Use Internal Path";
    AllConfig["continue_run"]         = "Continue";
    AllConfig["readonly_storage"]     = "Read-only Storage";

    tinyxml2::XMLDocument doc;
    const std::string filePath = GetFilePath();
    if (filePath.empty()) {
        // No locale file found; use built-in defaults above.
        return;
    }
    std::string xmlData = ReadFileToString(filePath);
    if (xmlData.empty()) {
        return;
    }
    doc.Parse(xmlData.c_str(), xmlData.size());
    tinyxml2::XMLElement *rootElement = doc.RootElement();
    if (rootElement) {
        for (tinyxml2::XMLElement *item = rootElement->FirstChildElement(); item;
             item = item->NextSiblingElement()) {
            const char *key = item->Attribute("id");
            const char *val = item->Attribute("text");
            if (key && val) {
                AllConfig[key] = val;
            }
        }
    }
}
