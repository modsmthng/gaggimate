#include "ProfileManager.h"
#include <ArduinoJson.h>

#include <utility>

ProfileManager::ProfileManager(fs::FS *fs, String dir, Settings &settings, PluginManager *plugin_manager)
    : _plugin_manager(plugin_manager), _settings(settings), _fs(fs), _dir(std::move(dir)) {
    mutex = xSemaphoreCreateRecursiveMutex();
}

void ProfileManager::setup() {
    ensureDirectory();
    auto profiles = listProfiles();
    Profile loadedProfile;
    if (getFavoritedProfiles().empty() || profiles.empty() || _settings.getSelectedProfile() == "" || !loadSelectedProfile(loadedProfile)) {
        migrate();
        loadSelectedProfile(loadedProfile);
    }
    {
        RecursiveLockGuard lock(mutex);
        selectedProfile = std::move(loadedProfile);
    }
    _settings.setFavoritedProfiles(getFavoritedProfiles(true));
}

bool ProfileManager::ensureDirectory() const {
    if (!_fs->exists(_dir)) {
        return _fs->mkdir(_dir);
    }
    return true;
}

String ProfileManager::profilePath(const String &uuid) const { return _dir + "/" + uuid + ".json"; }

void ProfileManager::migrate() {
    Profile profile{};
    profile.id = generateShortID();
    profile.label = "Default";
    profile.description = "Default profile";
    profile.temperature = 93;
    profile.type = "standard";
    Phase brewPhase{};
    brewPhase.name = "Brew";
    brewPhase.phase = PhaseType::PHASE_TYPE_BREW;
    brewPhase.valve = 1;
    brewPhase.duration = 28;
    brewPhase.pumpIsSimple = true;
    brewPhase.pumpSimple = 100;
    Target target{};
    target.type = TargetType::TARGET_TYPE_VOLUMETRIC;
    target.operator_ = TargetOperator::GTE;
    target.value = 36;
    brewPhase.targets.push_back(target);
    profile.phases.push_back(brewPhase);
    saveProfile(profile);
    _settings.setSelectedProfile(profile.id);
    for (String id : listProfiles()) {
        addFavoritedProfile(id);
    }
}

std::vector<String> ProfileManager::listProfiles() {
    RecursiveLockGuard lock(mutex);
    return listProfilesSnapshot();
}

std::vector<String> ProfileManager::listProfilesSnapshot() {
    RecursiveLockGuard lock(mutex);
    std::vector<String> uuids;
    File root = _fs->open(_dir);
    if (!root || !root.isDirectory())
        return uuids;

    File file = root.openNextFile();
    while (file) {
        String name = file.name();
        if (name.endsWith(".json")) {
            int start = name.lastIndexOf('/') + 1;
            int end = name.lastIndexOf('.');
            uuids.push_back(name.substring(start, end));
        }
        file = root.openNextFile();
    }

    std::vector<String> ordered;
    auto stored = _settings.getProfileOrder();
    for (auto const &id : stored) {
        if (std::find(uuids.begin(), uuids.end(), id) != uuids.end() &&
            std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
            ordered.push_back(id);
        }
    }
    for (auto const &id : uuids) {
        if (std::find(ordered.begin(), ordered.end(), id) == ordered.end()) {
            ordered.push_back(id);
        }
    }
    return ordered;
}

bool ProfileManager::loadProfile(const String &uuid, Profile &outProfile) {
    RecursiveLockGuard lock(mutex);
    bool ok = false;
    outProfile = loadProfileLocked(uuid, &ok);
    return ok;
}

Profile ProfileManager::loadProfileLocked(const String &uuid, bool *ok) {
    File file = _fs->open(profilePath(uuid), "r");
    if (!file) {
        if (ok != nullptr) {
            *ok = false;
        }
        return Profile{};
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, file);
    file.close();
    if (err) {
        if (ok != nullptr) {
            *ok = false;
        }
        return Profile{};
    }

    Profile outProfile;
    if (!parseProfile(doc.as<JsonObject>(), outProfile)) {
        if (ok != nullptr) {
            *ok = false;
        }
        return Profile{};
    }
    outProfile.selected = outProfile.id == _settings.getSelectedProfile();
    auto favoritedProfiles = _settings.getFavoritedProfiles();
    outProfile.favorite = std::find(favoritedProfiles.begin(), favoritedProfiles.end(), outProfile.id) != favoritedProfiles.end();
    if (ok != nullptr) {
        *ok = true;
    }
    return outProfile;
}

bool ProfileManager::saveProfile(Profile &profile) {
    bool isNew = false;
    bool shouldReloadSelected = false;

    {
        RecursiveLockGuard lock(mutex);
        if (!ensureDirectory())
            return false;
        if (profile.id == nullptr || profile.id.isEmpty()) {
            profile.id = generateShortID();
            isNew = true;
        }

        ESP_LOGI("ProfileManager", "Saving profile %s", profile.id.c_str());

        File file = _fs->open(profilePath(profile.id), "w");
        if (!file)
            return false;

        JsonDocument doc;
        JsonObject obj = doc.to<JsonObject>();
        writeProfile(obj, profile);

        bool ok = serializeJson(doc, file) > 0;
        file.close();
        if (!ok) {
            return false;
        }

        shouldReloadSelected = profile.id == _settings.getSelectedProfile();
        if (shouldReloadSelected) {
            selectedProfile = profile;
            selectedProfile.selected = true;
            auto favoritedProfiles = _settings.getFavoritedProfiles();
            selectedProfile.favorite =
                std::find(favoritedProfiles.begin(), favoritedProfiles.end(), selectedProfile.id) != favoritedProfiles.end();
        }
    }

    if (isNew) {
        addFavoritedProfile(profile.id);
    } else if (shouldReloadSelected) {
        _plugin_manager->trigger("profiles:profile:select", "id", profile.id);
    }

    _plugin_manager->trigger("profiles:profile:save", "id", profile.id);
    return true;
}

bool ProfileManager::saveSelectedProfile() {
    Profile profile = getSelectedProfileSnapshot();
    if (profile.id.isEmpty()) {
        return false;
    }
    return saveProfile(profile);
}

bool ProfileManager::mutateSelectedProfile(const std::function<void(Profile &)> &mutator) {
    RecursiveLockGuard lock(mutex);
    if (selectedProfile.id.isEmpty()) {
        bool ok = false;
        selectedProfile = loadProfileLocked(_settings.getSelectedProfile(), &ok);
        if (!ok) {
            return false;
        }
    }
    mutator(selectedProfile);
    return true;
}

bool ProfileManager::deleteProfile(const String &uuid) {
    bool removed = false;
    {
        RecursiveLockGuard lock(mutex);
        if (selectedProfile.id == uuid) {
            selectedProfile = Profile{};
        }
        removed = _fs->remove(profilePath(uuid));
    }
    if (removed) {
        removeFavoritedProfile(uuid);
    }
    return removed;
}

bool ProfileManager::profileExists(const String &uuid) {
    RecursiveLockGuard lock(mutex);
    return _fs->exists(profilePath(uuid));
}

void ProfileManager::selectProfile(const String &uuid) {
    ESP_LOGI("ProfileManager", "Selecting profile %s", uuid.c_str());
    {
        RecursiveLockGuard lock(mutex);
        _settings.setSelectedProfile(uuid);
        bool ok = false;
        selectedProfile = loadProfileLocked(uuid, &ok);
        if (!ok) {
            selectedProfile = Profile{};
        }
    }
    _plugin_manager->trigger("profiles:profile:select", "id", uuid);
}

Profile ProfileManager::getSelectedProfileSnapshot() {
    RecursiveLockGuard lock(mutex);
    if (selectedProfile.id.isEmpty() && !_settings.getSelectedProfile().isEmpty()) {
        bool ok = false;
        selectedProfile = loadProfileLocked(_settings.getSelectedProfile(), &ok);
    }
    return selectedProfile;
}

bool ProfileManager::loadSelectedProfile(Profile &outProfile) {
    RecursiveLockGuard lock(mutex);
    bool ok = false;
    outProfile = loadProfileLocked(_settings.getSelectedProfile(), &ok);
    if (ok && outProfile.id == _settings.getSelectedProfile()) {
        selectedProfile = outProfile;
    }
    return ok;
}

std::vector<String> ProfileManager::getFavoritedProfiles(bool validate) {
    RecursiveLockGuard lock(mutex);

    auto rawFavorites = _settings.getFavoritedProfiles();
    std::vector<String> result;

    auto storedProfileOrder = _settings.getProfileOrder();
    for (const auto &id : storedProfileOrder) {
        if (std::find(rawFavorites.begin(), rawFavorites.end(), id) != rawFavorites.end()) {
            if (!validate || _fs->exists(profilePath(id))) {
                if (std::find(result.begin(), result.end(), id) == result.end()) {
                    result.push_back(id);
                }
            }
        }
    }

    for (const auto &fav : rawFavorites) {
        if (std::find(result.begin(), result.end(), fav) == result.end()) {
            if (!validate || _fs->exists(profilePath(fav))) {
                result.push_back(fav);
            }
        }
    }

    if (result.empty()) {
        String sel = _settings.getSelectedProfile();
        bool selValid = (!validate) || _fs->exists(profilePath(sel));
        if (selValid) {
            result.push_back(sel);
        }
    }
    return result;
}

void ProfileManager::removeFavoritedProfile(String id) {
    _settings.removeFavoritedProfile(id);
    _plugin_manager->trigger("profiles:profile:unfavorite", "id", id);
}

void ProfileManager::addFavoritedProfile(String id) {
    _settings.addFavoritedProfile(id);
    _plugin_manager->trigger("profiles:profile:favorite", "id", id);
}
