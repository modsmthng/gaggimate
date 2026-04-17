#pragma once
#ifndef SETTINGS_H
#define SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>
#include <display/core/RecursiveLock.h>
#include <display/core/constants.h>
#include <display/core/utils.h>
#include <vector>

#define PREFERENCES_KEY "controller"

struct AutoWakeupSchedule {
    String time;    // HH:MM format
    bool days[7]{}; // [Mon, Tue, Wed, Thu, Fri, Sat, Sun]

    AutoWakeupSchedule() : time("07:00") {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    explicit AutoWakeupSchedule(const String &timeStr) : time(timeStr) {
        // Default to all days enabled
        for (int i = 0; i < 7; i++) {
            days[i] = true;
        }
    }

    [[nodiscard]] bool isDayEnabled(const int dayOfWeek) const {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek < 1 || dayOfWeek > 7)
            return false;
        return days[dayOfWeek - 1];
    }

    void setDayEnabled(const int dayOfWeek, const bool enabled) {
        // dayOfWeek: 1=Monday, 2=Tuesday, ..., 7=Sunday
        if (dayOfWeek >= 1 && dayOfWeek <= 7) {
            days[dayOfWeek - 1] = enabled;
        }
    }
};

class Settings;
using SettingsCallback = std::function<void(Settings *)>;

class Settings {
  public:
    Settings();

    void batchUpdate(const SettingsCallback &callback);
    void save(bool noDelay = false);

    // Getters and setters
    int getTargetSteamTemp() const {
        RecursiveLockGuard lock(mutex);
        return targetSteamTemp;
    }
    int getTargetWaterTemp() const {
        RecursiveLockGuard lock(mutex);
        return targetWaterTemp;
    }
    int getTemperatureOffset() const {
        RecursiveLockGuard lock(mutex);
        return temperatureOffset;
    }
    float getPressureScaling() const {
        RecursiveLockGuard lock(mutex);
        return pressureScaling;
    }
    double getTargetGrindVolume() const {
        RecursiveLockGuard lock(mutex);
        return targetGrindVolume;
    }
    int getTargetGrindDuration() const {
        RecursiveLockGuard lock(mutex);
        return targetGrindDuration;
    }
    int getStartupMode() const {
        RecursiveLockGuard lock(mutex);
        return startupMode;
    }
    int getStandbyTimeout() const {
        RecursiveLockGuard lock(mutex);
        return standbyTimeout;
    }
    double getBrewDelay() const {
        RecursiveLockGuard lock(mutex);
        return brewDelay;
    }
    double getGrindDelay() const {
        RecursiveLockGuard lock(mutex);
        return grindDelay;
    }
    bool isDelayAdjust() const {
        RecursiveLockGuard lock(mutex);
        return delayAdjust;
    }
    String getPid() const {
        RecursiveLockGuard lock(mutex);
        return pid;
    }
    String getPumpModelCoeffs() const {
        RecursiveLockGuard lock(mutex);
        return pumpModelCoeffs;
    }
    String getWifiSsid() const {
        RecursiveLockGuard lock(mutex);
        return wifiSsid;
    }
    String getWifiPassword() const {
        RecursiveLockGuard lock(mutex);
        return wifiPassword;
    }
    String getMdnsName() const {
        RecursiveLockGuard lock(mutex);
        return mdnsName;
    }
    bool isHomekit() const {
        RecursiveLockGuard lock(mutex);
        return homekit;
    }
    bool isVolumetricTarget() const {
        RecursiveLockGuard lock(mutex);
        return volumetricTarget;
    }
    String getOTAChannel() const {
        RecursiveLockGuard lock(mutex);
        return otaChannel;
    }
    String getSavedScale() const {
        RecursiveLockGuard lock(mutex);
        return savedScale;
    }
    bool isBoilerFillActive() const {
        RecursiveLockGuard lock(mutex);
        return boilerFillActive;
    }
    int getStartupFillTime() const {
        RecursiveLockGuard lock(mutex);
        return startupFillTime;
    }
    int getSteamFillTime() const {
        RecursiveLockGuard lock(mutex);
        return steamFillTime;
    }
    bool isSmartGrindActive() const {
        RecursiveLockGuard lock(mutex);
        return smartGrindActive;
    }
    int getSmartGrindMode() const {
        RecursiveLockGuard lock(mutex);
        return smartGrindMode;
    }
    String getSmartGrindIp() const {
        RecursiveLockGuard lock(mutex);
        return smartGrindIp;
    }
    bool isHomeAssistant() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistant;
    }
    String getHomeAssistantIP() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistantIP;
    }
    String getHomeAssistantUser() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistantUser;
    }
    String getHomeAssistantPassword() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistantPassword;
    }
    int getHomeAssistantPort() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistantPort;
    }
    String getHomeAssistantTopic() const {
        RecursiveLockGuard lock(mutex);
        return homeAssistantTopic;
    }
    bool isMomentaryButtons() const {
        RecursiveLockGuard lock(mutex);
        return momentaryButtons;
    }
    String getTimezone() const {
        RecursiveLockGuard lock(mutex);
        return timezone;
    }
    bool isClock24hFormat() const {
        RecursiveLockGuard lock(mutex);
        return clock24hFormat;
    }
    String getSelectedProfile() const {
        RecursiveLockGuard lock(mutex);
        return selectedProfile;
    }
    std::vector<String> getFavoritedProfiles() const {
        RecursiveLockGuard lock(mutex);
        return favoritedProfiles;
    }
    std::vector<String> getProfileOrder() const {
        RecursiveLockGuard lock(mutex);
        return profileOrder;
    }
    int getMainBrightness() const {
        RecursiveLockGuard lock(mutex);
        return mainBrightness;
    }
    int getStandbyBrightness() const {
        RecursiveLockGuard lock(mutex);
        return standbyBrightness;
    }
    int getStandbyBrightnessTimeout() const {
        RecursiveLockGuard lock(mutex);
        return standbyBrightnessTimeout;
    }
    int getWifiApTimeout() const {
        RecursiveLockGuard lock(mutex);
        return wifiApTimeout;
    }
    float getSteamPumpPercentage() const {
        RecursiveLockGuard lock(mutex);
        return steamPumpPercentage;
    }
    float getSteamPumpCutoff() const {
        RecursiveLockGuard lock(mutex);
        return steamPumpCutoff;
    }
    int getThemeMode() const {
        RecursiveLockGuard lock(mutex);
        return themeMode;
    }
    int getHistoryIndex() const {
        RecursiveLockGuard lock(mutex);
        return historyIndex;
    }
    int getSunriseR() const {
        RecursiveLockGuard lock(mutex);
        return sunriseR;
    }
    int getSunriseG() const {
        RecursiveLockGuard lock(mutex);
        return sunriseG;
    }
    int getSunriseB() const {
        RecursiveLockGuard lock(mutex);
        return sunriseB;
    }
    int getSunriseW() const {
        RecursiveLockGuard lock(mutex);
        return sunriseW;
    }
    int getSunriseExtBrightness() const {
        RecursiveLockGuard lock(mutex);
        return sunriseExtBrightness;
    }
    int getEmptyTankDistance() const {
        RecursiveLockGuard lock(mutex);
        return emptyTankDistance;
    }
    int getFullTankDistance() const {
        RecursiveLockGuard lock(mutex);
        return fullTankDistance;
    }
    int getAltRelayFunction() const {
        RecursiveLockGuard lock(mutex);
        return altRelayFunction;
    }
    bool isAutoWakeupEnabled() const {
        RecursiveLockGuard lock(mutex);
        return autowakeupEnabled;
    }
    std::vector<AutoWakeupSchedule> getAutoWakeupSchedules() const {
        RecursiveLockGuard lock(mutex);
        return autowakeupSchedules;
    }
    void setTargetSteamTemp(int target_steam_temp);
    void setTargetWaterTemp(int target_water_temp);
    void setTemperatureOffset(int temperature_offset);
    void setPressureScaling(float pressure_scaling);
    void setTargetGrindVolume(double target_grind_volume);
    void setTargetGrindDuration(int target_duration);
    void setStartupMode(int startup_mode);
    void setStandbyTimeout(int standby_timeout);
    void setBrewDelay(double brewDelay);
    void setGrindDelay(double grindDelay);
    void setDelayAdjust(bool delay_adjust);
    void setPid(const String &pid);
    void setPumpModelCoeffs(const String &pumpModelCoeffs);
    void setWifiSsid(const String &wifiSsid);
    void setWifiPassword(const String &wifiPassword);
    void setMdnsName(const String &mdnsName);
    void setHomekit(bool homekit);
    void setVolumetricTarget(bool volumetric_target);
    void setOTAChannel(const String &otaChannel);
    void setSavedScale(const String &savedScale);
    void setBoilerFillActive(bool boiler_fill_active);
    void setStartupFillTime(int startup_fill_time);
    void setSteamFillTime(int steam_fill_time);
    void setSmartGrindActive(bool smart_grind_active);
    void setSmartGrindIp(String smart_grind_ip);
    void setSmartGrindMode(int smart_grind_mode);
    void setHomeAssistant(bool homeAssistant);
    void setHomeAssistantUser(const String &homeAssistantUser);
    void setHomeAssistantPassword(const String &homeAssistantPassword);
    void setHomeAssistantIP(const String &homeAssistantIP);
    void setHomeAssistantPort(int homeAssistantPort);
    void setHomeAssistantTopic(const String &homeAssistantTopic);
    void setMomentaryButtons(bool momentary_buttons);
    void setTimezone(String timezone);
    void setClockFormat(bool format_24h);
    void setSelectedProfile(String selected_profile);
    void setFavoritedProfiles(std::vector<String> favorited_profiles);
    void addFavoritedProfile(String profile);
    void removeFavoritedProfile(String profile);
    void setProfileOrder(std::vector<String> profile_order);
    void setMainBrightness(int main_brightness);
    void setStandbyBrightness(int standby_brightness);
    void setStandbyBrightnessTimeout(int standby_brightness_timeout);
    void setWifiApTimeout(int timeout);
    void setSteamPumpPercentage(float steam_pump_percentage);
    void setSteamPumpCutoff(float steam_pump_cutoff);
    void setThemeMode(int theme_mode);
    void setHistoryIndex(int history_index);
    void setSunriseR(int sunrise_r);
    void setSunriseG(int sunrise_g);
    void setSunriseB(int sunrise_b);
    void setSunriseW(int sunrise_w);
    void setSunriseExtBrightness(int sunrise_ext_brightness);
    void setEmptyTankDistance(int empty_tank_distance);
    void setFullTankDistance(int full_tank_distance);
    void setAltRelayFunction(int alt_relay_function);
    void setAutoWakeupEnabled(bool enabled);
    void setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules);

  private:
    Preferences preferences;
    bool dirty = false;
    int batchDepth = 0;
    mutable SemaphoreHandle_t mutex = nullptr;

    String selectedProfile;
    int targetSteamTemp = 155;
    int targetWaterTemp = 80;
    int temperatureOffset = DEFAULT_TEMPERATURE_OFFSET;
    float pressureScaling = DEFAULT_PRESSURE_SCALING;
    double targetGrindVolume = 18;
    int targetGrindDuration = 25000;
    double brewDelay = 1000.0;
    double grindDelay = 1000.0;
    bool delayAdjust = true;
    int startupMode = MODE_STANDBY;
    bool autowakeupEnabled = false;
    std::vector<AutoWakeupSchedule> autowakeupSchedules;
    int standbyTimeout = DEFAULT_STANDBY_TIMEOUT_MS;
    String pid = DEFAULT_PID;
    String pumpModelCoeffs = DEFAULT_PUMP_MODEL_COEFFS;
    String wifiSsid = "";
    String wifiPassword = "";
    String mdnsName = DEFAULT_MDNS_NAME;
    String savedScale = "";
    bool homekit = false;
    bool volumetricTarget = false;
    bool boilerFillActive = false;
    int startupFillTime = 0;
    int steamFillTime = 0;
    bool smartGrindActive = false;
    bool smartGrindToggle = false;
    int smartGrindMode = 0;
    String smartGrindIp = "";
    bool homeAssistant = false;
    String homeAssistantUser = "";
    String homeAssistantPassword = "";
    String homeAssistantIP = "";
    int homeAssistantPort = 1883;
    String homeAssistantTopic = DEFAULT_HOME_ASSISTANT_TOPIC;
    bool momentaryButtons = false;
    String timezone = DEFAULT_TIMEZONE;
    bool clock24hFormat = true;
    String otaChannel = DEFAULT_OTA_CHANNEL;
    std::vector<String> favoritedProfiles;
    std::vector<String> profileOrder; // persisted profile ordering
    float steamPumpPercentage = DEFAULT_STEAM_PUMP_PERCENTAGE;
    float steamPumpCutoff = DEFAULT_STEAM_PUMP_CUTOFF;
    int historyIndex = 0;

    // Display settings
    int mainBrightness = 16;
    int standbyBrightness = 8;
    int standbyBrightnessTimeout = 60000; // 60 seconds default
    int wifiApTimeout = DEFAULT_WIFI_AP_TIMEOUT_MS;
    int themeMode = 0;

    // Sunrise settings
    int sunriseR = 0;
    int sunriseG = 0;
    int sunriseB = 255;
    int sunriseW = 50;
    int sunriseExtBrightness = 255;
    int emptyTankDistance = 200;
    int fullTankDistance = 50;
    int altRelayFunction = ALT_RELAY_GRIND; // Default to grind

    void doSave();
    void markDirtyLocked(bool noDelay);
    xTaskHandle taskHandle;
    static void loopTask(void *arg);
};

#endif // SETTINGS_H
