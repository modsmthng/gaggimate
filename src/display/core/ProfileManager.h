#pragma once
#ifndef PROFILEMANAGER_H
#define PROFILEMANAGER_H
#include "PluginManager.h"
#include <FS.h>
#include <display/core/RecursiveLock.h>
#include <display/core/Settings.h>
#include <display/core/utils.h>
#include <display/models/profile.h>
#include <functional>

class ProfileManager {
  public:
    ProfileManager(fs::FS *fs, String dir, Settings &settings, PluginManager *plugin_manager);

    void setup();
    std::vector<String> listProfiles();
    std::vector<String> listProfilesSnapshot();
    bool loadProfile(const String &uuid, Profile &outProfile);
    bool saveProfile(Profile &profile);
    bool saveSelectedProfile();
    bool deleteProfile(const String &uuid);
    bool profileExists(const String &uuid);
    void selectProfile(const String &uuid);
    Profile getSelectedProfileSnapshot();
    bool loadSelectedProfile(Profile &outProfile);
    std::vector<String> getFavoritedProfiles(bool validate = false);
    bool mutateSelectedProfile(const std::function<void(Profile &)> &mutator);

    void addFavoritedProfile(String id);
    void removeFavoritedProfile(String id);

  private:
    Profile loadProfileLocked(const String &uuid, bool *ok = nullptr);

    Profile selectedProfile{};
    PluginManager *_plugin_manager;
    Settings &_settings;
    fs::FS *_fs;
    String _dir;
    SemaphoreHandle_t mutex = nullptr;
    bool ensureDirectory() const;
    String profilePath(const String &uuid) const;
    void migrate();
};

#endif // PROFILEMANAGER_H
