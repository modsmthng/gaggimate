#include "Settings.h"

#include <algorithm>
#include <utility>

Settings::Settings() {
    mutex = xSemaphoreCreateRecursiveMutex();
    RecursiveLockGuard lock(mutex);
    preferences.begin(PREFERENCES_KEY, true);
    startupMode = preferences.getInt("sm", MODE_STANDBY);
    targetSteamTemp = preferences.getInt("ts", 145);
    targetWaterTemp = preferences.getInt("tw", 80);
    targetGrindVolume = preferences.getDouble("tgv", 18.0);
    targetGrindDuration = preferences.getInt("tgd", 25000);
    brewDelay = preferences.getDouble("del_br", 800.0);
    grindDelay = preferences.getDouble("del_gd", 1000.0);
    delayAdjust = preferences.getBool("del_ad", true);
    temperatureOffset = preferences.getInt("to", DEFAULT_TEMPERATURE_OFFSET);
    pressureScaling = preferences.getFloat("ps", DEFAULT_PRESSURE_SCALING);
    pid = preferences.getString("pid", DEFAULT_PID);
    pumpModelCoeffs = preferences.getString("pmc", DEFAULT_PUMP_MODEL_COEFFS);
    wifiSsid = preferences.getString("ws", "");
    wifiPassword = preferences.getString("wp", "");
    mdnsName = preferences.getString("mn", DEFAULT_MDNS_NAME);
    homekit = preferences.getBool("hk", false);
    volumetricTarget = preferences.getBool("vt", false);
    otaChannel = preferences.getString("oc", DEFAULT_OTA_CHANNEL);
    savedScale = preferences.getString("ssc", "");
    momentaryButtons = preferences.getBool("mb", false);
    boilerFillActive = preferences.getBool("bf_a", false);
    startupFillTime = preferences.getInt("bf_su", 5000);
    steamFillTime = preferences.getInt("bf_st", 5000);
    smartGrindActive = preferences.getBool("sg_a", false);
    smartGrindIp = preferences.getString("sg_i", "");
    smartGrindToggle = preferences.getBool("sg_t", false);
    smartGrindMode = preferences.getInt("sg_m", smartGrindToggle ? 1 : 0);
    homeAssistant = preferences.getBool("ha_a", false);
    homeAssistantIP = preferences.getString("ha_i", "");
    homeAssistantPort = preferences.getInt("ha_p", 1883);
    homeAssistantTopic = preferences.getString("ha_t", DEFAULT_HOME_ASSISTANT_TOPIC);
    homeAssistantUser = preferences.getString("ha_u", "");
    homeAssistantPassword = preferences.getString("ha_pw", "");
    standbyTimeout = preferences.getInt("sbt", DEFAULT_STANDBY_TIMEOUT_MS);
    timezone = preferences.getString("tz", DEFAULT_TIMEZONE);
    clock24hFormat = preferences.getBool("clk_24h", true);
    selectedProfile = preferences.getString("sp", "");
    favoritedProfiles = explode(preferences.getString("fp", ""), ',');
    profileOrder = explode(preferences.getString("po", ""), ',');
    steamPumpPercentage = preferences.getFloat("spp", DEFAULT_STEAM_PUMP_PERCENTAGE);
    steamPumpCutoff = preferences.getFloat("spc", DEFAULT_STEAM_PUMP_CUTOFF);
    historyIndex = preferences.getInt("hi", 0);
    autowakeupEnabled = preferences.getBool("ab_en", false);

    // Load schedule format: "time1|days1;time2|days2" where days is 7-bit string (e.g., "1111100" for weekdays only)
    String schedulesStr = preferences.getString("ab_schedules", "");
    autowakeupSchedules.clear();

    if (schedulesStr.length() > 0) {
        int start = 0;
        int end = schedulesStr.indexOf(';');

        while (end != -1 || start < schedulesStr.length()) {
            String scheduleStr = (end != -1) ? schedulesStr.substring(start, end) : schedulesStr.substring(start);

            int pipePos = scheduleStr.indexOf('|');
            if (pipePos != -1) {
                String timeStr = scheduleStr.substring(0, pipePos);
                String daysStr = scheduleStr.substring(pipePos + 1);

                AutoWakeupSchedule schedule;
                schedule.time = timeStr;

                if (daysStr.length() == 7) {
                    for (int i = 0; i < 7; i++) {
                        schedule.days[i] = (daysStr.charAt(i) == '1');
                    }
                }

                autowakeupSchedules.push_back(schedule);
            }

            if (end == -1)
                break;
            start = end + 1;
            end = schedulesStr.indexOf(';', start);
        }
    }

    if (autowakeupSchedules.empty()) {
        autowakeupSchedules.emplace_back(AutoWakeupSchedule("07:00"));
    }

    // Display settings
    mainBrightness = preferences.getInt("main_b", 16);
    standbyBrightness = preferences.getInt("standby_b", 8);
    standbyBrightnessTimeout = preferences.getInt("standby_bt", 60000);
    wifiApTimeout = preferences.getInt("wifi_apt", DEFAULT_WIFI_AP_TIMEOUT_MS);
    themeMode = preferences.getInt("theme", 0);

    // Sunrise settings
    sunriseR = preferences.getInt("sr_r", 0);
    sunriseG = preferences.getInt("sr_g", 0);
    sunriseB = preferences.getInt("sr_b", 255);
    sunriseW = preferences.getInt("sr_w", 50);
    sunriseExtBrightness = preferences.getInt("sr_exb", 255);
    emptyTankDistance = preferences.getInt("sr_ed", 200);
    fullTankDistance = preferences.getInt("sr_fd", 50);
    altRelayFunction = preferences.getInt("alt_relay", ALT_RELAY_GRIND);

    preferences.end();

    xTaskCreate(loopTask, "Settings::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle);
}

void Settings::batchUpdate(const SettingsCallback &callback) {
    {
        RecursiveLockGuard lock(mutex);
        batchDepth++;
        callback(this);
        batchDepth--;
        if (batchDepth == 0 && dirty) {
            doSave();
        }
    }
}

void Settings::save(bool noDelay) {
    RecursiveLockGuard lock(mutex);
    markDirtyLocked(noDelay);
}

void Settings::setTargetSteamTemp(const int target_steam_temp) {
    RecursiveLockGuard lock(mutex);
    targetSteamTemp = target_steam_temp;
    markDirtyLocked(false);
}

void Settings::setTargetWaterTemp(const int target_water_temp) {
    RecursiveLockGuard lock(mutex);
    targetWaterTemp = target_water_temp;
    markDirtyLocked(false);
}

void Settings::setTemperatureOffset(const int temperature_offset) {
    RecursiveLockGuard lock(mutex);
    temperatureOffset = temperature_offset;
    markDirtyLocked(false);
}

void Settings::setPressureScaling(const float pressure_scaling) {
    RecursiveLockGuard lock(mutex);
    pressureScaling = pressure_scaling;
    markDirtyLocked(false);
}

void Settings::setTargetGrindVolume(double target_grind_volume) {
    RecursiveLockGuard lock(mutex);
    targetGrindVolume = target_grind_volume;
    markDirtyLocked(false);
}

void Settings::setTargetGrindDuration(const int target_duration) {
    RecursiveLockGuard lock(mutex);
    targetGrindDuration = target_duration;
    markDirtyLocked(false);
}

void Settings::setBrewDelay(double brew_Delay) {
    RecursiveLockGuard lock(mutex);
    brewDelay = std::clamp(brew_Delay, 0.0, 4000.0);
    markDirtyLocked(false);
}

void Settings::setGrindDelay(double grind_Delay) {
    RecursiveLockGuard lock(mutex);
    grindDelay = std::clamp(grind_Delay, 0.0, 4000.0);
    markDirtyLocked(false);
}

void Settings::setDelayAdjust(bool delay_adjust) {
    RecursiveLockGuard lock(mutex);
    delayAdjust = delay_adjust;
    markDirtyLocked(false);
}

void Settings::setStartupMode(const int startup_mode) {
    RecursiveLockGuard lock(mutex);
    startupMode = startup_mode;
    markDirtyLocked(false);
}

void Settings::setStandbyTimeout(int standby_timeout) {
    RecursiveLockGuard lock(mutex);
    standbyTimeout = standby_timeout;
    markDirtyLocked(false);
}

void Settings::setPid(const String &pid) {
    RecursiveLockGuard lock(mutex);
    this->pid = pid;
    markDirtyLocked(false);
}

void Settings::setPumpModelCoeffs(const String &pumpModelCoeffs) {
    RecursiveLockGuard lock(mutex);
    this->pumpModelCoeffs = pumpModelCoeffs;
    markDirtyLocked(false);
}

void Settings::setWifiSsid(const String &wifiSsid) {
    RecursiveLockGuard lock(mutex);
    this->wifiSsid = wifiSsid;
    markDirtyLocked(false);
}

void Settings::setWifiPassword(const String &wifiPassword) {
    RecursiveLockGuard lock(mutex);
    this->wifiPassword = wifiPassword;
    markDirtyLocked(false);
}

void Settings::setMdnsName(const String &mdnsName) {
    RecursiveLockGuard lock(mutex);
    this->mdnsName = mdnsName;
    markDirtyLocked(false);
}

void Settings::setHomekit(const bool homekit) {
    RecursiveLockGuard lock(mutex);
    this->homekit = homekit;
    markDirtyLocked(false);
}

void Settings::setVolumetricTarget(bool volumetric_target) {
    RecursiveLockGuard lock(mutex);
    this->volumetricTarget = volumetric_target;
    markDirtyLocked(false);
}

void Settings::setOTAChannel(const String &otaChannel) {
    RecursiveLockGuard lock(mutex);
    this->otaChannel = otaChannel;
    markDirtyLocked(false);
}

void Settings::setSavedScale(const String &savedScale) {
    RecursiveLockGuard lock(mutex);
    this->savedScale = savedScale;
    markDirtyLocked(false);
}

void Settings::setBoilerFillActive(bool boiler_fill_active) {
    RecursiveLockGuard lock(mutex);
    boilerFillActive = boiler_fill_active;
    markDirtyLocked(false);
}

void Settings::setStartupFillTime(int startup_fill_time) {
    RecursiveLockGuard lock(mutex);
    startupFillTime = startup_fill_time;
    markDirtyLocked(false);
}

void Settings::setSteamFillTime(int steam_fill_time) {
    RecursiveLockGuard lock(mutex);
    steamFillTime = steam_fill_time;
    markDirtyLocked(false);
}

void Settings::setSmartGrindActive(bool smart_grind_active) {
    RecursiveLockGuard lock(mutex);
    smartGrindActive = smart_grind_active;
    markDirtyLocked(false);
}

void Settings::setSmartGrindIp(String smart_grind_ip) {
    RecursiveLockGuard lock(mutex);
    this->smartGrindIp = std::move(smart_grind_ip);
    markDirtyLocked(false);
}

void Settings::setSmartGrindMode(int smart_grind_mode) {
    RecursiveLockGuard lock(mutex);
    this->smartGrindMode = smart_grind_mode;
    markDirtyLocked(false);
}

void Settings::setHomeAssistant(const bool homeAssistant) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistant = homeAssistant;
    markDirtyLocked(false);
}

void Settings::setHomeAssistantIP(const String &homeAssistantIP) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistantIP = homeAssistantIP;
    markDirtyLocked(false);
}

void Settings::setHomeAssistantPort(const int homeAssistantPort) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistantPort = homeAssistantPort;
    markDirtyLocked(false);
}
void Settings::setHomeAssistantTopic(const String &homeAssistantTopic) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistantTopic = homeAssistantTopic;
    markDirtyLocked(false);
}
void Settings::setHomeAssistantUser(const String &homeAssistantUser) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistantUser = homeAssistantUser;
    markDirtyLocked(false);
}
void Settings::setHomeAssistantPassword(const String &homeAssistantPassword) {
    RecursiveLockGuard lock(mutex);
    this->homeAssistantPassword = homeAssistantPassword;
    markDirtyLocked(false);
}

void Settings::setMomentaryButtons(bool momentary_buttons) {
    RecursiveLockGuard lock(mutex);
    momentaryButtons = momentary_buttons;
    markDirtyLocked(false);
}

void Settings::setTimezone(String timezone) {
    RecursiveLockGuard lock(mutex);
    this->timezone = std::move(timezone);
    markDirtyLocked(false);
}

void Settings::setClockFormat(bool clock_24h_format) {
    RecursiveLockGuard lock(mutex);
    this->clock24hFormat = clock_24h_format;
    markDirtyLocked(false);
}

void Settings::setSelectedProfile(String selected_profile) {
    RecursiveLockGuard lock(mutex);
    this->selectedProfile = std::move(selected_profile);
    markDirtyLocked(false);
}

void Settings::setFavoritedProfiles(std::vector<String> favorited_profiles) {
    RecursiveLockGuard lock(mutex);
    favoritedProfiles = std::move(favorited_profiles);
    markDirtyLocked(false);
}

void Settings::addFavoritedProfile(String profile) {
    RecursiveLockGuard lock(mutex);
    if (std::find(favoritedProfiles.begin(), favoritedProfiles.end(), profile) != favoritedProfiles.end()) {
        return;
    }
    favoritedProfiles.emplace_back(profile);
    markDirtyLocked(false);
}

void Settings::removeFavoritedProfile(String profile) {
    RecursiveLockGuard lock(mutex);
    favoritedProfiles.erase(std::remove(favoritedProfiles.begin(), favoritedProfiles.end(), profile), favoritedProfiles.end());
    favoritedProfiles.shrink_to_fit();
    markDirtyLocked(false);
}

void Settings::setProfileOrder(std::vector<String> profile_order) {
    RecursiveLockGuard lock(mutex);
    std::vector<String> cleaned;
    cleaned.reserve(profile_order.size());
    for (auto &id : profile_order) {
        if (id.isEmpty())
            continue;
        if (std::find(cleaned.begin(), cleaned.end(), id) == cleaned.end()) {
            cleaned.emplace_back(std::move(id));
        }
    }

    profileOrder = std::move(cleaned);
    markDirtyLocked(false);
}

void Settings::setMainBrightness(int main_brightness) {
    RecursiveLockGuard lock(mutex);
    mainBrightness = main_brightness;
    markDirtyLocked(false);
}

void Settings::setStandbyBrightness(int standby_brightness) {
    RecursiveLockGuard lock(mutex);
    standbyBrightness = standby_brightness;
    markDirtyLocked(false);
}

void Settings::setStandbyBrightnessTimeout(int standby_brightness_timeout) {
    RecursiveLockGuard lock(mutex);
    standbyBrightnessTimeout = standby_brightness_timeout;
    markDirtyLocked(false);
}

void Settings::setWifiApTimeout(int timeout) {
    RecursiveLockGuard lock(mutex);
    wifiApTimeout = timeout;
    markDirtyLocked(false);
}

void Settings::setSteamPumpPercentage(float steam_pump_percentage) {
    RecursiveLockGuard lock(mutex);
    steamPumpPercentage = steam_pump_percentage;
    markDirtyLocked(false);
}

void Settings::setSteamPumpCutoff(float steam_pump_cutoff) {
    RecursiveLockGuard lock(mutex);
    steamPumpCutoff = steam_pump_cutoff;
    markDirtyLocked(false);
}

void Settings::setThemeMode(int theme_mode) {
    RecursiveLockGuard lock(mutex);
    themeMode = theme_mode;
    markDirtyLocked(false);
}

void Settings::setHistoryIndex(int history_index) {
    RecursiveLockGuard lock(mutex);
    historyIndex = history_index;
    markDirtyLocked(false);
}

void Settings::setSunriseR(int sunrise_r) {
    RecursiveLockGuard lock(mutex);
    sunriseR = sunrise_r;
    markDirtyLocked(false);
}

void Settings::setSunriseG(int sunrise_g) {
    RecursiveLockGuard lock(mutex);
    sunriseG = sunrise_g;
    markDirtyLocked(false);
}

void Settings::setSunriseB(int sunrise_b) {
    RecursiveLockGuard lock(mutex);
    sunriseB = sunrise_b;
    markDirtyLocked(false);
}

void Settings::setSunriseW(int sunrise_w) {
    RecursiveLockGuard lock(mutex);
    sunriseW = sunrise_w;
    markDirtyLocked(false);
}

void Settings::setSunriseExtBrightness(int sunrise_ext_brightness) {
    RecursiveLockGuard lock(mutex);
    sunriseExtBrightness = sunrise_ext_brightness;
    markDirtyLocked(false);
}

void Settings::setEmptyTankDistance(int empty_tank_distance) {
    RecursiveLockGuard lock(mutex);
    emptyTankDistance = empty_tank_distance;
    markDirtyLocked(false);
}

void Settings::setFullTankDistance(int full_tank_distance) {
    RecursiveLockGuard lock(mutex);
    fullTankDistance = full_tank_distance;
    markDirtyLocked(false);
}

void Settings::setAltRelayFunction(int alt_relay_function) {
    RecursiveLockGuard lock(mutex);
    altRelayFunction = alt_relay_function;
    markDirtyLocked(false);
}

void Settings::setAutoWakeupEnabled(bool enabled) {
    RecursiveLockGuard lock(mutex);
    autowakeupEnabled = enabled;
    markDirtyLocked(false);
}

void Settings::setAutoWakeupSchedules(const std::vector<AutoWakeupSchedule> &schedules) {
    RecursiveLockGuard lock(mutex);
    autowakeupSchedules = schedules;
    markDirtyLocked(false);
}

void Settings::doSave() {
    RecursiveLockGuard lock(mutex);
    if (!dirty) {
        return;
    }
    dirty = false;
    ESP_LOGI("Settings", "Saving settings");
    preferences.begin(PREFERENCES_KEY, false);
    preferences.putInt("sm", startupMode);
    preferences.putInt("ts", targetSteamTemp);
    preferences.putInt("tw", targetWaterTemp);
    preferences.putDouble("tgv", targetGrindVolume);
    preferences.putInt("tgd", targetGrindDuration);
    preferences.putDouble("del_br", brewDelay);
    preferences.putDouble("del_gd", grindDelay);
    preferences.putBool("del_ad", delayAdjust);
    preferences.putInt("to", temperatureOffset);
    preferences.putFloat("ps", pressureScaling);
    preferences.putString("pid", pid);
    preferences.putString("pmc", pumpModelCoeffs);
    preferences.putString("ws", wifiSsid);
    preferences.putString("wp", wifiPassword);
    preferences.putString("mn", mdnsName);
    preferences.putBool("hk", homekit);
    preferences.putBool("vt", volumetricTarget);
    preferences.putString("oc", otaChannel);
    preferences.putString("ssc", savedScale);
    preferences.putBool("bf_a", boilerFillActive);
    preferences.putInt("bf_su", startupFillTime);
    preferences.putInt("bf_st", steamFillTime);
    preferences.putBool("sg_a", smartGrindActive);
    preferences.putString("sg_i", smartGrindIp);
    preferences.putBool("sg_t", smartGrindToggle);
    preferences.putInt("sg_m", smartGrindMode);
    preferences.putBool("ha_a", homeAssistant);
    preferences.putString("ha_i", homeAssistantIP);
    preferences.putInt("ha_p", homeAssistantPort);
    preferences.putString("ha_t", homeAssistantTopic);
    preferences.putString("ha_u", homeAssistantUser);
    preferences.putString("ha_pw", homeAssistantPassword);
    preferences.putString("tz", timezone);
    preferences.putBool("clk_24h", clock24hFormat);
    preferences.putString("sp", selectedProfile);
    preferences.putInt("sbt", standbyTimeout);
    preferences.putBool("mb", momentaryButtons);
    preferences.putString("fp", implode(favoritedProfiles, ","));
    preferences.putString("po", implode(profileOrder, ","));
    preferences.putFloat("spp", steamPumpPercentage);
    preferences.putFloat("spc", steamPumpCutoff);
    preferences.putInt("hi", historyIndex);
    preferences.putBool("ab_en", autowakeupEnabled);

    // Save schedule format
    String schedulesForSave = "";
    for (size_t i = 0; i < autowakeupSchedules.size(); i++) {
        if (i > 0)
            schedulesForSave += ";";
        schedulesForSave += autowakeupSchedules[i].time + "|";

        // Convert days array to 7-bit string
        for (int j = 0; j < 7; j++) {
            schedulesForSave += autowakeupSchedules[i].days[j] ? "1" : "0";
        }
    }
    preferences.putString("ab_schedules", schedulesForSave);

    // Display settings
    preferences.putInt("main_b", mainBrightness);
    preferences.putInt("standby_b", standbyBrightness);
    preferences.putInt("standby_bt", standbyBrightnessTimeout);
    preferences.putInt("wifi_apt", wifiApTimeout);
    preferences.putInt("theme", themeMode);

    // Sunrise Settings
    preferences.putInt("sr_r", sunriseR);
    preferences.putInt("sr_g", sunriseG);
    preferences.putInt("sr_b", sunriseB);
    preferences.putInt("sr_w", sunriseW);
    preferences.putInt("sr_exb", sunriseExtBrightness);
    preferences.putInt("sr_ed", emptyTankDistance);
    preferences.putInt("sr_fd", fullTankDistance);
    preferences.putInt("alt_relay", altRelayFunction);

    preferences.end();
}

void Settings::markDirtyLocked(bool noDelay) {
    dirty = true;
    if (batchDepth > 0) {
        return;
    }
    if (noDelay) {
        doSave();
    }
}

[[noreturn]] void Settings::loopTask(void *arg) {
    auto *settings = static_cast<Settings *>(arg);
    while (true) {
        settings->doSave();
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}
