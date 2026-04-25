#pragma once
// UpdateChecker - Async GitHub release version checker (non-blocking)
// Uses libcurl for cross-platform HTTP.

#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <chrono>
#include <curl/curl.h>

#include "version.h"

enum class UpdateState {
    Idle,
    Checking,
    UpdateAvailable,
    UpToDate,
    Error
};

class UpdateChecker {
public:
    static UpdateChecker& Instance() {
        static UpdateChecker inst;
        return inst;
    }

    // Launch async check (non-blocking, runs on background thread)
    void CheckForUpdate() {
        UpdateState expected = UpdateState::Checking;
        if (state_.load() == expected) return;

        state_.store(UpdateState::Checking);
        manualCheck_ = true;
        std::thread([this]() { DoCheck(false); }).detach();
    }

    // Launch check silently (for auto-check on startup)
    void CheckForUpdateSilent() {
        manualCheck_ = false;
        if (state_.load() == UpdateState::Checking) return;
        state_.store(UpdateState::Checking);
        std::thread([this]() { DoCheck(true); }).detach();
    }

    UpdateState GetState() const { return state_.load(); }

    std::string GetLatestVersion() {
        std::lock_guard<std::mutex> lock(mutex_);
        return latestVersion_;
    }

    bool IsManualCheck() const { return manualCheck_; }

    // Popup control
    bool ShouldShowPopup() const { return showPopup_; }
    void PopupShown() { showPopup_ = false; }

    void OpenReleasePage() {
#ifdef _WIN32
        ShellExecuteW(nullptr, L"open",
            L"https://github.com/Misaka10571/joycon2-connector/releases/latest",
            nullptr, nullptr, SW_SHOWNORMAL);
#elif defined(__APPLE__)
        system("open https://github.com/Misaka10571/joycon2-connector/releases/latest");
#else
        system("xdg-open https://github.com/Misaka10571/joycon2-connector/releases/latest");
#endif
    }

private:
    UpdateChecker() = default;

    std::atomic<UpdateState> state_{ UpdateState::Idle };
    std::mutex mutex_;
    std::string latestVersion_;
    std::atomic<bool> showPopup_{ false };
    std::atomic<bool> manualCheck_{ false };

    // libcurl write callback — appends received data to a std::string
    static size_t CurlWriteCB(char* ptr, size_t size, size_t nmemb, void* userdata) {
        auto* buf = static_cast<std::string*>(userdata);
        buf->append(ptr, size * nmemb);
        return size * nmemb;
    }

    // Fetch URL via libcurl; returns body or empty string on error
    static std::string FetchURL(const std::string& url) {
        CURL* curl = curl_easy_init();
        if (!curl) return {};
        std::string body;
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "joycon2-connector");
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWriteCB);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        if (res != CURLE_OK) return {};
        return body;
    }

    void DoCheck(bool silent) {
        std::string json = FetchURL("https://api.github.com/repos/Misaka10571/joycon2-connector/releases/latest");
        if (json.empty()) {
            state_.store(silent ? UpdateState::Idle : UpdateState::Error);
            return;
        }
        std::string tagName = ExtractTagName(json);
        if (tagName.empty()) {
            state_.store(silent ? UpdateState::Idle : UpdateState::Error);
            return;
        }
        if (!tagName.empty() && (tagName[0] == 'v' || tagName[0] == 'V'))
            tagName = tagName.substr(1);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            latestVersion_ = tagName;
        }
        if (IsNewerVersion(tagName)) {
            state_.store(UpdateState::UpdateAvailable);
            showPopup_ = true;
        } else {
            state_.store(silent ? UpdateState::Idle : UpdateState::UpToDate);
        }
    }

    // Extract "tag_name" value from GitHub API JSON response
    static std::string ExtractTagName(const std::string& json) {
        const std::string key = "\"tag_name\"";
        auto pos = json.find(key);
        if (pos == std::string::npos) return "";
        pos = json.find(':', pos);
        if (pos == std::string::npos) return "";
        // Find opening quote of value
        auto start = json.find('"', pos + 1);
        if (start == std::string::npos) return "";
        auto end = json.find('"', start + 1);
        if (end == std::string::npos) return "";
        return json.substr(start + 1, end - start - 1);
    }

    // Parse "MAJOR.MINOR.PATCH" into three integers
    static bool ParseVersion(const std::string& ver, int& major, int& minor, int& patch) {
        major = minor = patch = 0;
        std::istringstream iss(ver);
        char dot1, dot2;
        if (!(iss >> major >> dot1 >> minor >> dot2 >> patch)) {
            // Try parsing with fewer components
            std::istringstream iss2(ver);
            if (!(iss2 >> major >> dot1 >> minor)) {
                return false;
            }
            patch = 0;
        }
        return true;
    }

    // Returns true if remoteVer is newer than current APP_VERSION
    static bool IsNewerVersion(const std::string& remoteVer) {
        int rMajor, rMinor, rPatch;
        if (!ParseVersion(remoteVer, rMajor, rMinor, rPatch)) return false;

        if (rMajor != APP_VERSION_MAJOR) return rMajor > APP_VERSION_MAJOR;
        if (rMinor != APP_VERSION_MINOR) return rMinor > APP_VERSION_MINOR;
        return rPatch > APP_VERSION_PATCH;
    }
};
