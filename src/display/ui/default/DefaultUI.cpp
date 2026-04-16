#include "DefaultUI.h"

#include <algorithm>
#include <cstdint>

#include <WiFi.h>
#include <display/core/Controller.h>
#include <display/core/process/BrewProcess.h>
#include <display/core/process/Process.h>
#include <display/core/zones.h>
#include <display/drivers/AmoledDisplayDriver.h>
#include <display/drivers/LilyGoDriver.h>
#include <display/drivers/WaveshareDriver.h>
#include <display/drivers/common/LV_Helper.h>
#include <display/main.h>
#include <display/ui/default/lvgl/ui_theme_manager.h>
#include <display/ui/default/lvgl/ui_themes.h>
#include <display/ui/utils/effects.h>
#include <utility>

#include "esp_sntp.h"

static EffectManager effect_mgr;

int16_t calculate_angle(int set_temp, int range, int offset) {
    const double percentage = static_cast<double>(set_temp) / static_cast<double>(MAX_TEMP);
    return (percentage * ((double)range)) - range / 2 - offset;
}

void DefaultUI::updateTempHistory() {
    if (currentTemp > 0) {
        tempHistory[tempHistoryIndex] = currentTemp;
        tempHistoryIndex += 1;
    }

    if (tempHistoryIndex > TEMP_HISTORY_LENGTH) {
        tempHistoryIndex = 0;
        isTempHistoryInitialized = true;
    }

    if (tempHistoryIndex % 4 == 0) {
        heatingFlash = !heatingFlash;
        rerender = true;
    }
}

void DefaultUI::updateTempStableFlag() {
    if (isTempHistoryInitialized) {
        float totalError = 0.0f;
        float maxError = 0.0f;
        for (uint16_t i = 0; i < TEMP_HISTORY_LENGTH; i++) {
            float error = abs(tempHistory[i] - targetTemp);
            totalError += error;
            maxError = error > maxError ? error : maxError;
        }

        const float avgError = totalError / TEMP_HISTORY_LENGTH;
        const float errorMargin = max(2.0f, static_cast<float>(targetTemp) * 0.02f);

        isTemperatureStable = avgError < errorMargin && maxError <= errorMargin;
    }

    // instantly reset stability if setpoint has changed
    if (prevTargetTemp != targetTemp) {
        isTemperatureStable = false;
    }

    prevTargetTemp = targetTemp;
}

void DefaultUI::adjustHeatingIndicator(lv_obj_t *dials) {
    lv_obj_t *heatingIcon = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPICON);
    lv_obj_set_style_img_recolor(heatingIcon, lv_color_hex(isTemperatureStable ? 0x00D100 : 0xF62C2C),
                                 LV_PART_MAIN | LV_STATE_DEFAULT);
    if (!isTemperatureStable) {
        lv_obj_set_style_opa(heatingIcon, heatingFlash ? LV_OPA_50 : LV_OPA_100, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
}

void DefaultUI::reloadProfiles() {
    profileLoaded = 0;
    pendingProfileListSelection = false;
    profileListRows.clear();
}

DefaultUI::DefaultUI(Controller *controller, Driver *driver, PluginManager *pluginManager)
    : controller(controller), panelDriver(driver), pluginManager(pluginManager) {
    setupPanel();
}

void DefaultUI::init() {
    profileManager = controller->getProfileManager();
    auto triggerRender = [this](Event const &) { rerender = true; };
    pluginManager->on("boiler:currentTemperature:change", [=](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != currentTemp) {
            currentTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("boiler:pressure:change", [=](Event const &event) {
        float newPressure = event.getFloat("value");
        if (round(newPressure * 10.0f) != round(pressure * 10.0f)) {
            pressure = newPressure;
            rerender = true;
        }
    });
    pluginManager->on("boiler:targetTemperature:change", [=](Event const &event) {
        int newTemp = static_cast<int>(event.getFloat("value"));
        if (newTemp != targetTemp) {
            targetTemp = newTemp;
            rerender = true;
        }
    });
    pluginManager->on("controller:targetVolume:change", [=](Event const &event) {
        targetVolume = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:targetDuration:change", [=](Event const &event) {
        targetDuration = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:grindDuration:change", [=](Event const &event) {
        grindDuration = event.getInt("value");
        rerender = true;
    });
    pluginManager->on("controller:grindVolume:change", [=](Event const &event) {
        grindVolume = event.getFloat("value");
        rerender = true;
    });
    pluginManager->on("controller:process:end", triggerRender);
    pluginManager->on("controller:process:start", triggerRender);
    pluginManager->on("controller:mode:change", [this](Event const &event) {
        mode = event.getInt("value");
        switch (mode) {
        case MODE_STANDBY:
            changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
            break;
        case MODE_BREW:
            changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
            break;
        case MODE_GRIND:
            changeScreen(&ui_GrindScreen, &ui_GrindScreen_screen_init);
            break;
        case MODE_STEAM:
            changeScreen(&ui_SimpleProcessScreen, &ui_SimpleProcessScreen_screen_init);
            break;
        case MODE_WATER:
            changeScreen(&ui_SimpleProcessScreen, &ui_SimpleProcessScreen_screen_init);
            break;
        default:
            break;
        };
    });
    pluginManager->on("controller:brew:start",
                      [this](Event const &event) { changeScreen(&ui_StatusScreen, &ui_StatusScreen_screen_init); });
    pluginManager->on("controller:brew:clear", [this](Event const &event) {
        pendingBrewClear = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:waiting", [this](Event const &) {
        waitingForController = true;
        rerender = true;
    });
    pluginManager->on("controller:bluetooth:connect", [this](Event const &) {
        waitingForController = false;
        rerender = true;
        initialized = true;
        pressureAvailable = controller->getSystemInfo().capabilities.pressure;
        pendingControllerConnect = true;
    });
    pluginManager->on("controller:wifi:connect", [this](Event const &event) {
        rerender = true;
        apActive = event.getInt("AP");
    });
    pluginManager->on("ota:update:start", [this](Event const &) {
        updateActive = true;
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("ota:update:end", [this](Event const &) {
        updateActive = false;
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("ota:update:status", [this](Event const &event) {
        rerender = true;
        updateAvailable = event.getInt("value");
    });
    pluginManager->on("controller:error", [this](Event const &) {
        rerender = true;
        changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
    });
    pluginManager->on("controller:autotune:start",
                      [this](Event const &) { changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init); });
    pluginManager->on("controller:autotune:result",
                      [this](Event const &) { changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init); });

    pluginManager->on("profiles:profile:select", [this](Event const &event) {
        selectedProfileId = event.getString("id");
        reloadProfiles();
        rerender = true;
    });
    pluginManager->on("profiles:profile:favorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:unfavorite", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("profiles:profile:save", [this](Event const &event) { reloadProfiles(); });
    pluginManager->on("controller:volumetric-measurement:bluetooth:change", [=](Event const &event) {
        double newWeight = event.getFloat("value");
        if (round(newWeight * 10.0) != round(bluetoothWeight * 10.0)) {
            bluetoothWeight = newWeight;
            rerender = true;
        }
    });
    setupState();
    setupReactive();
    xTaskCreatePinnedToCore(loopTask, "DefaultUI::loop", configMINIMAL_STACK_SIZE * 6, this, 1, &taskHandle, 1);
}

void DefaultUI::loop() {
    const unsigned long now = millis();
    const unsigned long diff = now - lastRender;

    loadProfilesIfNeeded();

    if (pendingControllerConnect) {
        pendingControllerConnect = false;
        if (currentScreen == ui_StandbyScreen || targetScreen == &ui_StandbyScreen) {
            Settings &settings = controller->getSettings();
            if (settings.getStartupMode() == MODE_BREW) {
                changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
            } else {
                standbyEnterTime = millis();
            }
        }
    }

    if (pendingBrewClear) {
        pendingBrewClear = false;
        if (currentScreen == ui_StatusScreen || targetScreen == &ui_StatusScreen) {
            changeScreen(&ui_BrewScreen, &ui_BrewScreen_screen_init);
        }
    }

    if (now - lastTempLog > TEMP_HISTORY_INTERVAL) {
        updateTempHistory();
        lastTempLog = now;
    }

    if (pendingProfileListSelection) {
        if (lv_scr_act() != ui_ProfileScreen || currentProfileSelectorView != ProfileSelectorView::List) {
            pendingProfileListSelection = false;
        } else if (now - pendingProfileListSelectionAt >= PROFILE_LIST_SELECTION_FEEDBACK_MS) {
            pendingProfileListSelection = false;
            onProfileSelect();
        }
    }

    if ((controller->isActive() && diff > RERENDER_INTERVAL_ACTIVE) || diff > RERENDER_INTERVAL_IDLE) {
        rerender = true;
    }

    if (rerender) {
        rerender = false;
        lastRender = now;
        error = controller->isErrorState();
        autotuning = controller->isAutotuning();
        const Settings &settings = controller->getSettings();
        volumetricAvailable = controller->isVolumetricAvailable();
        bluetoothScales = controller->isBluetoothScaleHealthy();
        volumetricMode = volumetricAvailable && settings.isVolumetricTarget();
        brewVolumetric = volumetricAvailable && profileVolumetric;
        grindActive = controller->isGrindActive();
        active = controller->isActive();
        smartGrindActive = settings.isSmartGrindActive();
        grindAvailable = smartGrindActive || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
        applyTheme();
        if (controller->isErrorState()) {
            changeScreen(&ui_StandbyScreen, &ui_StandbyScreen_screen_init);
        }
        updateTempStableFlag();
        handleScreenChange();
        currentScreen = lv_scr_act();
        if (lv_scr_act() == ui_StandbyScreen)
            updateStandbyScreen();
        if (lv_scr_act() == ui_StatusScreen)
            updateStatusScreen();
        effect_mgr.evaluate_all();
    }

    lv_task_handler();
}

void DefaultUI::loadProfilesIfNeeded() {
    if (!profileLoaded) {
        const auto favoritedIds = profileManager->getFavoritedProfiles();
        favoritedProfileIds.clear();
        favoritedProfiles.clear();
        profileListProfiles.clear();
        favoritedProfileIds.reserve(favoritedIds.size() + 1);
        Profile currentProfile = profileManager->getSelectedProfileSnapshot();
        selectedProfile = currentProfile;
        selectedProfileId = currentProfile.id;
        targetDuration = currentProfile.getTotalDuration();
        targetVolume = currentProfile.getTotalVolume();
        profileVolumetric = currentProfile.isVolumetric();
        favoritedProfileIds.emplace_back(currentProfile.id);
        for (const auto &id : favoritedIds) {
            if (std::find(favoritedProfileIds.begin(), favoritedProfileIds.end(), id) == favoritedProfileIds.end()) {
                favoritedProfileIds.emplace_back(id);
            }
        }

        favoritedProfiles.reserve(favoritedProfileIds.size());
        for (const auto &profileId : favoritedProfileIds) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            favoritedProfiles.emplace_back(std::move(profile));
        }

        std::vector<String> orderedProfileListIds;
        orderedProfileListIds.reserve(favoritedProfileIds.size());

        for (const auto &profileId : profileListProfileIds) {
            if (std::find(favoritedProfileIds.begin(), favoritedProfileIds.end(), profileId) != favoritedProfileIds.end() &&
                std::find(orderedProfileListIds.begin(), orderedProfileListIds.end(), profileId) == orderedProfileListIds.end()) {
                orderedProfileListIds.emplace_back(profileId);
            }
        }

        for (const auto &profileId : favoritedProfileIds) {
            if (std::find(orderedProfileListIds.begin(), orderedProfileListIds.end(), profileId) == orderedProfileListIds.end()) {
                orderedProfileListIds.emplace_back(profileId);
            }
        }

        profileListProfileIds = std::move(orderedProfileListIds);

        for (const auto &profileId : profileListProfileIds) {
            Profile profile{};
            profileManager->loadProfile(profileId, profile);
            profileListProfiles.emplace_back(std::move(profile));
        }

        syncCurrentProfileIdxToSelectedProfile();
        syncProfileListIdxToSelectedProfile();
        profileLoaded = 1;
    }
}

void DefaultUI::changeScreen(lv_obj_t **screen, void (*target_init)()) {
    targetScreen = screen;
    targetScreenInit = target_init;
    rerender = true;

    // Reset some submenus
    brewScreenState = BrewScreenState::Brew;
}

void DefaultUI::changeBrewScreenMode(BrewScreenState state) {
    brewScreenState = state;
    rerender = true;
}

void DefaultUI::syncCurrentProfileIdxToSelectedProfile() {
    currentProfileIdx = 0;

    auto selected = std::find(favoritedProfileIds.begin(), favoritedProfileIds.end(), selectedProfileId);
    if (selected != favoritedProfileIds.end()) {
        currentProfileIdx = static_cast<int>(std::distance(favoritedProfileIds.begin(), selected));
    }
}

void DefaultUI::syncProfileListIdxToSelectedProfile() {
    profileListCurrentIdx = 0;

    auto selected = std::find(profileListProfileIds.begin(), profileListProfileIds.end(), selectedProfileId);
    if (selected != profileListProfileIds.end()) {
        profileListCurrentIdx = static_cast<int>(std::distance(profileListProfileIds.begin(), selected));
    }
}

void DefaultUI::onProfileSwitch() {
    syncCurrentProfileIdxToSelectedProfile();
    syncProfileListIdxToSelectedProfile();
    pendingProfileListSelection = false;
    currentProfileSelectorView = lastProfileSelectorView;
    profileListRows.clear();
    changeScreen(&ui_ProfileScreen, ui_ProfileScreen_screen_init);
}

void DefaultUI::onNextProfile() {
    if (!profileLoaded || favoritedProfileIds.empty()) {
        return;
    }

    if (currentProfileIdx < static_cast<int>(favoritedProfileIds.size()) - 1) {
        currentProfileIdx++;
    }
    rerender = true;
}

void DefaultUI::onPreviousProfile() {
    if (!profileLoaded || favoritedProfileIds.empty()) {
        return;
    }

    if (currentProfileIdx > 0) {
        currentProfileIdx--;
    }
    rerender = true;
}

void DefaultUI::onProfileSelect() {
    if (!profileLoaded) {
        return;
    }

    const bool listView = currentProfileSelectorView == ProfileSelectorView::List;
    const auto &activeProfileIds = listView ? profileListProfileIds : favoritedProfileIds;
    const int activeProfileIdx = listView ? profileListCurrentIdx : currentProfileIdx;

    if (activeProfileIdx < 0 || activeProfileIdx >= static_cast<int>(activeProfileIds.size())) {
        return;
    }

    pendingProfileListSelection = false;
    lastProfileSelectorView = currentProfileSelectorView;
    profileManager->selectProfile(activeProfileIds[activeProfileIdx]);
    profileDirty = false;
    changeScreen(&ui_BrewScreen, ui_BrewScreen_screen_init);
}

void DefaultUI::onProfileScreenBack() {
    pendingProfileListSelection = false;

    if (currentProfileSelectorView == ProfileSelectorView::List) {
        currentProfileSelectorView = ProfileSelectorView::Detail;
        lastProfileSelectorView = ProfileSelectorView::Detail;
        rerender = true;
        return;
    }

    controller->deactivate();
    controller->setMode(MODE_BREW);
    changeScreen(&ui_MenuScreen, &ui_MenuScreen_screen_init);
}

void DefaultUI::onProfileSelectorListOpen() {
    pendingProfileListSelection = false;
    syncProfileListIdxToSelectedProfile();
    currentProfileSelectorView = ProfileSelectorView::List;
    lastProfileSelectorView = ProfileSelectorView::List;
    rerender = true;
}

void DefaultUI::onProfileListEntrySelect(int index) {
    if (index < 0 || index >= static_cast<int>(profileListProfiles.size())) {
        return;
    }

    profileListCurrentIdx = index;
    if (currentProfileSelectorView == ProfileSelectorView::List) {
        pendingProfileListSelection = true;
        pendingProfileListSelectionAt = millis();
        rerender = true;
        return;
    }

    rerender = true;
}

void DefaultUI::onVolumetricDelete() {
    controller->onVolumetricDelete();
    reloadProfiles();
    profileDirty = true;
    rerender = true;
}

void DefaultUI::setupPanel() {
    ui_init();
    lv_task_handler();

    delay(100);
    // Set initial brightness based on settings
    const Settings &settings = controller->getSettings();
    setBrightness(settings.getMainBrightness());
}

void DefaultUI::setupState() {
    error = controller->isErrorState();
    autotuning = controller->isAutotuning();
    const Settings &settings = controller->getSettings();
    volumetricAvailable = controller->isVolumetricAvailable();
    volumetricMode = volumetricAvailable && settings.isVolumetricTarget();
    grindActive = controller->isGrindActive();
    active = controller->isActive();
    smartGrindActive = settings.isSmartGrindActive();
    grindAvailable = smartGrindActive || settings.getAltRelayFunction() == ALT_RELAY_GRIND;
    mode = controller->getMode();
    currentTemp = static_cast<int>(controller->getCurrentTemp());
    targetTemp = static_cast<int>(controller->getTargetTemp());
    selectedProfile = profileManager->getSelectedProfileSnapshot();
    targetDuration = selectedProfile.getTotalDuration();
    targetVolume = selectedProfile.getTotalVolume();
    grindDuration = settings.getTargetGrindDuration();
    grindVolume = settings.getTargetGrindVolume();
    pressureAvailable = controller->getSystemInfo().capabilities.pressure ? 1 : 0;
    pressureScaling = std::ceil(settings.getPressureScaling());
    profileVolumetric = selectedProfile.isVolumetric();
}

void DefaultUI::updateProfileSelectorView() {
    const bool listView = currentProfileSelectorView == ProfileSelectorView::List;
    const bool detailLoaded =
        profileLoaded && currentProfileIdx >= 0 && currentProfileIdx < static_cast<int>(favoritedProfiles.size());
    const bool listLoaded =
        profileLoaded && profileListCurrentIdx >= 0 && profileListCurrentIdx < static_cast<int>(profileListProfiles.size());
    const bool loaded = listView ? listLoaded : detailLoaded;

    _ui_flag_modify(ui_ProfileScreen_loadingSpinner, LV_OBJ_FLAG_HIDDEN,
                    loaded ? _UI_MODIFY_FLAG_ADD : _UI_MODIFY_FLAG_REMOVE);
    _ui_flag_modify(ui_ProfileScreen_chooseButton, LV_OBJ_FLAG_HIDDEN,
                    loaded && !listView ? _UI_MODIFY_FLAG_REMOVE : _UI_MODIFY_FLAG_ADD);
    _ui_flag_modify(ui_ProfileScreen_profileDetails, LV_OBJ_FLAG_HIDDEN,
                    loaded && !listView ? _UI_MODIFY_FLAG_REMOVE : _UI_MODIFY_FLAG_ADD);
    _ui_flag_modify(ui_ProfileScreen_listView, LV_OBJ_FLAG_HIDDEN,
                    listView ? _UI_MODIFY_FLAG_REMOVE : _UI_MODIFY_FLAG_ADD);
    _ui_flag_modify(ui_ProfileScreen_previousProfileBtn, LV_OBJ_FLAG_HIDDEN,
                    loaded && !listView ? _UI_MODIFY_FLAG_REMOVE : _UI_MODIFY_FLAG_ADD);
    _ui_flag_modify(ui_ProfileScreen_nextProfileBtn, LV_OBJ_FLAG_HIDDEN,
                    loaded && !listView ? _UI_MODIFY_FLAG_REMOVE : _UI_MODIFY_FLAG_ADD);

    if (!loaded) {
        return;
    }

    lv_imgbtn_set_src(ui_ProfileScreen_ImgButton1, LV_IMGBTN_STATE_RELEASED, nullptr,
                      listView ? &ui_img_98036921 : &ui_img_295763949, nullptr);
    lv_label_set_text(ui_ProfileScreen_mainLabel, currentProfileIdx == 0 ? "Current profile" : "Select profile");
    lv_label_set_text(ui_ProfileScreen_listMainLabel, "Select profile");
}

void DefaultUI::rebuildProfileList() {
    profileListRows.clear();

    if (ui_ProfileScreen_profileList == nullptr) {
        return;
    }

    lv_obj_clean(ui_ProfileScreen_profileList);

    for (size_t idx = 0; idx < profileListProfiles.size(); ++idx) {
        lv_obj_t *row = lv_obj_create(ui_ProfileScreen_profileList);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_radius(row, 18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_color(row, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_color(row, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(row, LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_width(row, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_left(row, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_right(row, 14, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_top(row, 10, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_pad_bottom(row, 10, LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_t *label = lv_label_create(row);
        lv_obj_set_width(label, 230);
        lv_obj_set_height(label, LV_SIZE_CONTENT);
        lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
        lv_label_set_text(label, profileListProfiles[idx].label.c_str());
        lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_18, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);

        lv_obj_add_event_cb(row, ::onProfileListEntrySelect, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(static_cast<intptr_t>(idx)));
        profileListRows.push_back(row);
    }
}

void DefaultUI::updateProfileListSelection(bool scrollToCurrent) {
    if (ui_ProfileScreen_profileList == nullptr) {
        return;
    }

    for (size_t idx = 0; idx < profileListRows.size(); ++idx) {
        lv_obj_t *row = profileListRows[idx];
        lv_obj_t *label = lv_obj_get_child(row, 0);
        const bool selected = static_cast<int>(idx) == profileListCurrentIdx;

        lv_obj_set_style_bg_opa(row, selected ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_border_opa(row, selected ? LV_OPA_TRANSP : LV_OPA_30, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_color(label, selected ? lv_color_hex(0x000000) : lv_color_hex(0xFFFFFF),
                                    LV_PART_MAIN | LV_STATE_DEFAULT);

        if (selected && scrollToCurrent) {
            lv_obj_scroll_to_view(row, LV_ANIM_OFF);
        }
    }
}

void DefaultUI::setupReactive() {
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; }, [=]() { adjustDials(ui_MenuScreen_dials); },
                          &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_StatusScreen; }, [=]() { adjustDials(ui_StatusScreen_dials); },
                          &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; }, [=]() { adjustDials(ui_BrewScreen_dials); },
                          &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; }, [=]() { adjustDials(ui_GrindScreen_dials); },
                          &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() { adjustDials(ui_SimpleProcessScreen_dials); }, &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; }, [=]() { adjustDials(ui_ProfileScreen_dials); },
                          &pressureAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; }, [=]() { adjustHeatingIndicator(ui_BrewScreen_dials); },
                          &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() { adjustHeatingIndicator(ui_SimpleProcessScreen_dials); }, &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; }, [=]() { adjustHeatingIndicator(ui_MenuScreen_dials); },
                          &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() { adjustHeatingIndicator(ui_ProfileScreen_dials); }, &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() { adjustHeatingIndicator(ui_GrindScreen_dials); }, &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_StatusScreen; },
                          [=]() { adjustHeatingIndicator(ui_StatusScreen_dials); }, &isTemperatureStable, &heatingFlash);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() { lv_label_set_text(ui_SimpleProcessScreen_mainLabel5, mode == MODE_STEAM ? "Steam" : "Water"); },
                          &mode);
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; },
                          [=]() {
                              lv_arc_set_value(uic_MenuScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_MenuScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_StatusScreen; },
                          [=]() {
                              lv_arc_set_value(uic_StatusScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_StatusScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_arc_set_value(uic_BrewScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_BrewScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_arc_set_value(uic_GrindScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_GrindScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() {
                              lv_arc_set_value(uic_SimpleProcessScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_SimpleProcessScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() {
                              lv_arc_set_value(uic_ProfileScreen_dials_tempGauge, currentTemp);
                              lv_label_set_text_fmt(uic_ProfileScreen_dials_tempText, "%d°C", currentTemp);
                          },
                          &currentTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; }, [=]() { adjustTempTarget(ui_MenuScreen_dials); },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_StatusScreen; },
                          [=]() {
                              lv_label_set_text_fmt(ui_StatusScreen_targetTemp, "%d°C", targetTemp);
                              adjustTempTarget(ui_StatusScreen_dials);
                          },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_label_set_text_fmt(ui_BrewScreen_targetTemp, "%d°C", targetTemp);
                              adjustTempTarget(ui_BrewScreen_dials);
                          },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; }, [=]() { adjustTempTarget(ui_GrindScreen_dials); },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() {
                              lv_label_set_text_fmt(ui_SimpleProcessScreen_targetTemp, "%d°C", targetTemp);
                              adjustTempTarget(ui_SimpleProcessScreen_dials);
                          },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; }, [=]() { adjustTempTarget(ui_ProfileScreen_dials); },
                          &targetTemp);
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; },
                          [=]() {
                              lv_arc_set_value(uic_MenuScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_MenuScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_StatusScreen; },
                          [=]() {
                              lv_arc_set_value(uic_StatusScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_StatusScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_arc_set_value(uic_BrewScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_BrewScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_arc_set_value(uic_GrindScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_GrindScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() {
                              lv_arc_set_value(uic_SimpleProcessScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_SimpleProcessScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_ProfileScreen; },
                          [=]() {
                              lv_arc_set_value(uic_ProfileScreen_dials_pressureGauge, pressure * 10.0f);
                              lv_label_set_text_fmt(uic_ProfileScreen_dials_pressureText, "%.1f bar", pressure);
                          },
                          &pressure);
    effect_mgr.use_effect([=] { return currentScreen == ui_StandbyScreen; },
                          [=]() {
                              updateAvailable ? lv_obj_clear_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_HIDDEN)
                                              : lv_obj_add_flag(ui_StandbyScreen_updateIcon, LV_OBJ_FLAG_HIDDEN);
                          },
                          &updateAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_StandbyScreen; },
                          [=]() {
                              bool deactivated = true;
                              if (updateActive) {
                                  lv_label_set_text_fmt(ui_StandbyScreen_mainLabel, "Updating...");
                              } else if (error) {
                                  if (controller->getError() == ERROR_CODE_RUNAWAY) {
                                      lv_label_set_text_fmt(ui_StandbyScreen_mainLabel, "Temperature error, please restart");
                                  }
                              } else if (autotuning) {
                                  lv_label_set_text_fmt(ui_StandbyScreen_mainLabel, "Autotuning...");
                              } else if (waitingForController) {
                                  lv_label_set_text_fmt(ui_StandbyScreen_mainLabel, "Waiting for controller...");
                              } else {
                                  deactivated = !initialized;
                              }
                              _ui_flag_modify(ui_StandbyScreen_mainLabel, LV_OBJ_FLAG_HIDDEN, deactivated);
                              _ui_flag_modify(ui_StandbyScreen_touchIcon, LV_OBJ_FLAG_HIDDEN, !deactivated);
                              _ui_flag_modify(ui_StandbyScreen_statusContainer, LV_OBJ_FLAG_HIDDEN, !deactivated);
                          },
                          &updateAvailable, &error, &autotuning, &waitingForController, &initialized);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              if (brewVolumetric) {
                                  lv_label_set_text_fmt(ui_BrewScreen_targetDuration, "%.1fg", targetVolume);
                                  lv_label_set_text_fmt(ui_BrewScreen_modeTargetLabel, "%.1fg", targetVolume);
                              } else {
                                  const double secondsDouble = targetDuration;
                                  const auto minutes = static_cast<int>(secondsDouble / 60.0);
                                  const auto seconds = static_cast<int>(secondsDouble) % 60;
                                  const auto roundedSeconds = static_cast<int>(secondsDouble + 0.5);
                                  lv_label_set_text_fmt(ui_BrewScreen_targetDuration, "%2d:%02d", minutes, seconds);
                                  if (roundedSeconds < 60) {
                                      lv_label_set_text_fmt(ui_BrewScreen_modeTargetLabel, "%ds", roundedSeconds);
                                  } else {
                                      const auto roundedTenths = static_cast<int>(secondsDouble / 6.0 + 0.5);
                                      if (roundedTenths % 10 == 0) {
                                          lv_label_set_text_fmt(ui_BrewScreen_modeTargetLabel, "%dmin", roundedTenths / 10);
                                      } else {
                                          lv_label_set_text_fmt(ui_BrewScreen_modeTargetLabel, "%.1fmin",
                                                                static_cast<double>(roundedTenths) / 10.0);
                                      }
                                  }
                              }
                          },
                          &targetDuration, &targetVolume, &brewVolumetric);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              if (volumetricMode) {
                                  lv_label_set_text_fmt(ui_GrindScreen_targetDuration, "%.1fg", grindVolume);
                              } else {
                                  const double secondsDouble = grindDuration / 1000.0;
                                  const auto minutes = static_cast<int>(secondsDouble / 60.0);
                                  const auto seconds = static_cast<int>(secondsDouble) % 60;
                                  lv_label_set_text_fmt(ui_GrindScreen_targetDuration, "%2d:%02d", minutes, seconds);
                              }
                          },
                          &grindDuration, &grindVolume, &volumetricMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              lv_img_set_src(ui_BrewScreen_Image4, brewVolumetric ? &ui_img_1424216268 : &ui_img_360122106);
                              _ui_flag_modify(ui_BrewScreen_byTimeButton, LV_OBJ_FLAG_HIDDEN, brewVolumetric);
                          },
                          &brewVolumetric);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_GrindScreen; },
        [=]() {
            lv_img_set_src(ui_GrindScreen_targetSymbol, volumetricMode ? &ui_img_1424216268 : &ui_img_360122106);
            ui_object_set_themeable_style_property(ui_GrindScreen_weightLabel, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_TEXT_COLOR,
                                                   volumetricMode ? _ui_theme_color_Dark : _ui_theme_color_NiceWhite);
            ui_object_set_themeable_style_property(ui_GrindScreen_volumetricButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   volumetricMode ? _ui_theme_color_Dark : _ui_theme_color_NiceWhite);
            ui_object_set_themeable_style_property(ui_GrindScreen_modeSwitch, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_BG_COLOR,
                                                   volumetricMode ? _ui_theme_color_NiceWhite : _ui_theme_color_Dark);
        },
        &volumetricMode);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() { _ui_flag_modify(ui_GrindScreen_modeSwitch, LV_OBJ_FLAG_HIDDEN, volumetricAvailable); },
                          &volumetricAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_SimpleProcessScreen; },
                          [=]() {
                              if (mode == MODE_STEAM) {
                                  _ui_flag_modify(ui_SimpleProcessScreen_goButton, LV_OBJ_FLAG_HIDDEN, active);
                                  lv_imgbtn_set_src(ui_SimpleProcessScreen_goButton, LV_IMGBTN_STATE_RELEASED, nullptr,
                                                    &ui_img_691326438, nullptr);
                              } else {
                                  lv_imgbtn_set_src(ui_SimpleProcessScreen_goButton, LV_IMGBTN_STATE_RELEASED, nullptr,
                                                    active ? &ui_img_1456692430 : &ui_img_445946954, nullptr);
                              }
                          },
                          &active, &mode);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              lv_imgbtn_set_src(ui_GrindScreen_startButton, LV_IMGBTN_STATE_RELEASED, nullptr,
                                                grindActive ? &ui_img_1456692430 : &ui_img_445946954, nullptr);
                          },
                          &grindActive);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=] {
                              lv_label_set_text(ui_BrewScreen_profileName, selectedProfile.label.c_str());
                              ui_BrewScreen_refresh_profile_name_scroll();
                          },
                          &selectedProfileId);

    effect_mgr.use_effect(
        [=] { return currentScreen == ui_ProfileScreen; },
        [=] {
            updateProfileSelectorView();

            const bool detailLoaded =
                profileLoaded && currentProfileIdx >= 0 && currentProfileIdx < static_cast<int>(favoritedProfiles.size());
            const bool listLoaded =
                profileLoaded && profileListCurrentIdx >= 0 && profileListCurrentIdx < static_cast<int>(profileListProfiles.size());
            const bool hasPrevious = detailLoaded && currentProfileIdx > 0;
            const bool hasNext = detailLoaded && currentProfileIdx < static_cast<int>(favoritedProfiles.size()) - 1;

            if (detailLoaded) {
                lv_label_set_text(ui_ProfileScreen_profileName, favoritedProfiles[currentProfileIdx].label.c_str());

                const auto minutes = static_cast<int>(favoritedProfiles[currentProfileIdx].getTotalDuration() / 60.0 - 0.5);
                const auto seconds = static_cast<int>(favoritedProfiles[currentProfileIdx].getTotalDuration()) % 60;
                lv_label_set_text_fmt(ui_ProfileScreen_targetDuration2, "%2d:%02d", minutes, seconds);
                lv_label_set_text_fmt(ui_ProfileScreen_targetTemp2, "%d°C",
                                      static_cast<int>(favoritedProfiles[currentProfileIdx].temperature));
                unsigned int phaseCount = favoritedProfiles[currentProfileIdx].getPhaseCount();
                unsigned int stepCount = favoritedProfiles[currentProfileIdx].phases.size();
                lv_label_set_text_fmt(ui_ProfileScreen_stepsLabel, "%d step%s", stepCount, stepCount > 1 ? "s" : "");
                lv_label_set_text_fmt(ui_ProfileScreen_phasesLabel, "%d phase%s", phaseCount, phaseCount > 1 ? "s" : "");
            }

            if (profileListRows.size() != profileListProfiles.size()) {
                rebuildProfileList();
            }
            if (listLoaded) {
                updateProfileListSelection(currentProfileSelectorView == ProfileSelectorView::List);
            }

            ui_object_set_themeable_style_property(ui_ProfileScreen_previousProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   hasPrevious ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_ProfileScreen_previousProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   hasPrevious ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
            ui_object_set_themeable_style_property(
                ui_ProfileScreen_nextProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR,
                hasNext ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(
                ui_ProfileScreen_nextProfileBtn, LV_PART_MAIN | LV_STATE_DEFAULT, LV_STYLE_IMG_RECOLOR_OPA,
                hasNext ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
        },
        &currentProfileIdx, &profileListCurrentIdx, &profileLoaded, &currentProfileSelectorView);

    // Show/hide grind button based on SmartGrind setting or Alt Relay function
    effect_mgr.use_effect([=] { return currentScreen == ui_MenuScreen; },
                          [=]() {
                              grindAvailable ? lv_obj_clear_flag(ui_MenuScreen_grindBtn, LV_OBJ_FLAG_HIDDEN)
                                             : lv_obj_add_flag(ui_MenuScreen_grindBtn, LV_OBJ_FLAG_HIDDEN);
                          },
                          &grindAvailable);
    effect_mgr.use_effect([=] { return currentScreen == ui_BrewScreen; },
                          [=]() {
                              if (volumetricAvailable && bluetoothScales) {
                                  lv_label_set_text_fmt(ui_BrewScreen_weightLabel, "%.1fg", bluetoothWeight);
                              } else {
                                  lv_label_set_text(ui_BrewScreen_weightLabel, "-");
                              }
                          },
                          &bluetoothWeight, &volumetricAvailable, &bluetoothScales);
    effect_mgr.use_effect([=] { return currentScreen == ui_GrindScreen; },
                          [=]() {
                              if (volumetricAvailable && bluetoothScales) {
                                  lv_label_set_text_fmt(ui_GrindScreen_weightLabel, "%.1fg", bluetoothWeight);
                              } else {
                                  lv_label_set_text(ui_GrindScreen_weightLabel, "-");
                              }
                          },
                          &bluetoothWeight, &volumetricAvailable, &bluetoothScales);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_BrewScreen; },
        [=]() {
            _ui_flag_modify(ui_BrewScreen_adjustments, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_acceptButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_saveButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_saveAsNewButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Settings);
            _ui_flag_modify(ui_BrewScreen_startButton, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Brew);
            _ui_flag_modify(ui_BrewScreen_startTouchArea, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Brew);
            _ui_flag_modify(ui_BrewScreen_profileInfo, LV_OBJ_FLAG_HIDDEN, brewScreenState == BrewScreenState::Brew);
            _ui_flag_modify(ui_BrewScreen_modeSwitch, LV_OBJ_FLAG_HIDDEN,
                            brewScreenState == BrewScreenState::Brew && volumetricAvailable);
            _ui_flag_modify(ui_BrewScreen_modeSwitchTouchArea, LV_OBJ_FLAG_HIDDEN,
                            brewScreenState == BrewScreenState::Brew && volumetricAvailable);
            if (volumetricAvailable) {
                lv_img_set_src(ui_BrewScreen_volumetricButton, bluetoothScales ? &ui_img_1424216268 : &ui_img_flowmeter_png);
            }
        },
        &brewScreenState, &volumetricAvailable, &bluetoothScales);
    effect_mgr.use_effect(
        [=] { return currentScreen == ui_BrewScreen; },
        [=]() {
            ui_object_set_themeable_style_property(ui_BrewScreen_saveButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   profileDirty ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   profileDirty ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveAsNewButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR,
                                                   profileDirty ? _ui_theme_color_NiceWhite : _ui_theme_color_SemiDark);
            ui_object_set_themeable_style_property(ui_BrewScreen_saveAsNewButton, LV_PART_MAIN | LV_STATE_DEFAULT,
                                                   LV_STYLE_IMG_RECOLOR_OPA,
                                                   profileDirty ? _ui_theme_alpha_NiceWhite : _ui_theme_alpha_SemiDark);
        },
        &brewScreenState, &profileDirty);
}

void DefaultUI::handleScreenChange() {
    lv_obj_t *current = lv_scr_act();

    if (current != *targetScreen) {
        if (*targetScreen == ui_StandbyScreen) {
            standbyEnterTime = millis();
        } else if (current == ui_StandbyScreen) {
            const Settings &settings = controller->getSettings();
            setBrightness(settings.getMainBrightness());
        }

        _ui_screen_change(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, targetScreenInit);
        lv_obj_del(current);
        rerender = true;
    }
}

void DefaultUI::updateStandbyScreen() {
    if (standbyEnterTime > 0) {
        const Settings &settings = controller->getSettings();
        const unsigned long now = millis();
        if (now - standbyEnterTime >= settings.getStandbyBrightnessTimeout()) {
            setBrightness(settings.getStandbyBrightness());
        }
    }

    if (!apActive && WiFi.status() == WL_CONNECTED && !updateActive && !error && !autotuning && !waitingForController &&
        initialized) {
        time_t now;
        struct tm timeinfo;

        localtime_r(&now, &timeinfo);
        // allocate enough space for both 12h/24h time formats
        if (getLocalTime(&timeinfo, 500)) {
            char time[9];
            Settings &settings = controller->getSettings();
            const char *format = settings.isClock24hFormat() ? "%H:%M" : "%I:%M %p";
            strftime(time, sizeof(time), format, &timeinfo);
            lv_label_set_text(ui_StandbyScreen_time, time);
            lv_obj_clear_flag(ui_StandbyScreen_time, LV_OBJ_FLAG_HIDDEN);

            christmasMode = (timeinfo.tm_mon == 11 && timeinfo.tm_mday < 27) || (timeinfo.tm_mon == 0 && timeinfo.tm_mday < 6);
        }
    } else {
        lv_obj_add_flag(ui_StandbyScreen_time, LV_OBJ_FLAG_HIDDEN);
    }
    controller->getClientController()->isConnected() ? lv_obj_clear_flag(ui_StandbyScreen_bluetoothIcon, LV_OBJ_FLAG_HIDDEN)
                                                     : lv_obj_add_flag(ui_StandbyScreen_bluetoothIcon, LV_OBJ_FLAG_HIDDEN);
    !apActive &&WiFi.status() == WL_CONNECTED ? lv_obj_clear_flag(ui_StandbyScreen_wifiIcon, LV_OBJ_FLAG_HIDDEN)
                                              : lv_obj_add_flag(ui_StandbyScreen_wifiIcon, LV_OBJ_FLAG_HIDDEN);
}

void DefaultUI::updateStatusScreen() const {
    ProcessSnapshot snapshot = controller->getProcessSnapshot();
    if (!snapshot.available || !snapshot.isBrew || snapshot.profile.phases.empty() ||
        snapshot.phaseIndex >= snapshot.profile.phases.size()) {
        return;
    }

    const auto &phase = snapshot.phase;

    unsigned long now = millis();
    if (!snapshot.active && snapshot.finished > 0) {
        now = snapshot.finished;
    }

    lv_label_set_text(ui_StatusScreen_stepLabel, phase.phase == PhaseType::PHASE_TYPE_BREW ? "BREW" : "INFUSION");
    String phaseText = "Finished";
    if (snapshot.active) {
        phaseText = phase.name;
    } else if (controller->getSettings().isDelayAdjust() && !snapshot.complete) {
        phaseText = "Calibrating...";
    }
    lv_label_set_text(ui_StatusScreen_phaseLabel, phaseText.c_str());

    if (snapshot.processStarted > 0 && now >= snapshot.processStarted) {
        const unsigned long processDuration = now - snapshot.processStarted;
        const double processSecondsDouble = processDuration / 1000.0;
        const auto processMinutes = static_cast<int>(processSecondsDouble / 60.0);
        const auto processSeconds = static_cast<int>(processSecondsDouble) % 60;
        lv_label_set_text_fmt(ui_StatusScreen_currentDuration, "%2d:%02d", processMinutes, processSeconds);
    } else {
        lv_label_set_text_fmt(ui_StatusScreen_currentDuration, "00:00");
    }

    if (snapshot.target == ProcessTarget::VOLUMETRIC && phase.hasVolumetricTarget()) {
        Target target = phase.getVolumetricTarget();
        lv_bar_set_value(ui_StatusScreen_brewBar, snapshot.currentVolume * 10.0, LV_ANIM_OFF);
        lv_bar_set_range(ui_StatusScreen_brewBar, 0, target.value * 10.0 + 1.0);
        lv_label_set_text_fmt(ui_StatusScreen_brewLabel, "%.1f / %.1fg", snapshot.currentVolume, target.value);
    } else {
        if (snapshot.currentPhaseStarted > 0 && now >= snapshot.currentPhaseStarted) {
            const unsigned long progress = now - snapshot.currentPhaseStarted;
            lv_bar_set_value(ui_StatusScreen_brewBar, progress, LV_ANIM_OFF);
            lv_bar_set_range(ui_StatusScreen_brewBar, 0, std::max(static_cast<int>(snapshot.phaseDuration), 1));
            lv_label_set_text_fmt(ui_StatusScreen_brewLabel, "%d / %ds", progress / 1000, snapshot.phaseDuration / 1000);
        } else {
            lv_bar_set_value(ui_StatusScreen_brewBar, 0, LV_ANIM_OFF);
            lv_bar_set_range(ui_StatusScreen_brewBar, 0, 1);
            lv_label_set_text(ui_StatusScreen_brewLabel, "0s");
        }
    }

    if (snapshot.target == ProcessTarget::TIME) {
        const unsigned long targetDuration = snapshot.totalDuration;
        const double targetSecondsDouble = targetDuration / 1000.0;
        const auto targetMinutes = static_cast<int>(targetSecondsDouble / 60.0);
        const auto targetSeconds = static_cast<int>(targetSecondsDouble) % 60;
        lv_label_set_text_fmt(ui_StatusScreen_targetDuration, "%2d:%02d", targetMinutes, targetSeconds);
    } else {
        lv_label_set_text_fmt(ui_StatusScreen_targetDuration, "%.1fg", snapshot.brewVolume);
    }
    lv_img_set_src(ui_StatusScreen_Image8, snapshot.target == ProcessTarget::TIME ? &ui_img_360122106 : &ui_img_1424216268);

    if (snapshot.advancedPump) {
        float pressure = snapshot.pumpPressure;
        const double percentage = 1.0 - static_cast<double>(pressure) / static_cast<double>(pressureScaling);
        adjustTarget(uic_StatusScreen_dials_pressureTarget, percentage, -62.0, 124.0);
    } else {
        const double percentage = 1.0 - 0.5;
        adjustTarget(uic_StatusScreen_dials_pressureTarget, percentage, -62.0, 124.0);
    }

    if (snapshot.active) {
        lv_obj_add_flag(ui_StatusScreen_brewVolume, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (snapshot.target == ProcessTarget::VOLUMETRIC) {
            lv_obj_clear_flag(ui_StatusScreen_brewVolume, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(ui_StatusScreen_barContainer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ui_StatusScreen_labelContainer, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(ui_StatusScreen_brewVolume, "%.1lfg", snapshot.currentVolume);
        lv_imgbtn_set_src(ui_StatusScreen_pauseButton, LV_IMGBTN_STATE_RELEASED, nullptr, &ui_img_631115820, nullptr);
    }
}

void DefaultUI::adjustDials(lv_obj_t *dials) {
    lv_obj_t *tempGauge = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPGAUGE);
    lv_obj_t *tempText = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPTEXT);
    lv_obj_t *pressureTarget = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSURETARGET);
    lv_obj_t *pressureGauge = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSUREGAUGE);
    lv_obj_t *pressureText = ui_comp_get_child(dials, UI_COMP_DIALS_PRESSURETEXT);
    lv_obj_t *pressureSymbol = ui_comp_get_child(dials, UI_COMP_DIALS_IMAGE6);
    _ui_flag_modify(pressureTarget, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureGauge, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureText, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    _ui_flag_modify(pressureSymbol, LV_OBJ_FLAG_HIDDEN, pressureAvailable);
    lv_obj_set_x(tempText, pressureAvailable ? -50 : 0);
    lv_obj_set_y(tempText, pressureAvailable ? -205 : -180);
    lv_arc_set_bg_angles(tempGauge, 118, pressureAvailable ? 242 : 62);
    lv_arc_set_range(pressureGauge, 0, pressureScaling * 10);
}

inline void DefaultUI::adjustTempTarget(lv_obj_t *dials) {
    double gaugeAngle = pressureAvailable ? 124.0 : 304;
    double gaugeStart = pressureAvailable ? 118.0 : -62;
    double percentage = static_cast<double>(targetTemp) / 160.0;
    lv_obj_t *tempTarget = ui_comp_get_child(dials, UI_COMP_DIALS_TEMPTARGET);
    adjustTarget(tempTarget, percentage, gaugeStart, gaugeAngle);
}

void DefaultUI::applyTheme() {
    const Settings &settings = controller->getSettings();
    int newThemeMode = settings.getThemeMode();

    if (newThemeMode != currentThemeMode) {
        currentThemeMode = newThemeMode;
        ui_theme_set(currentThemeMode);

        if (AmoledDisplayDriver::getInstance() == panelDriver && currentThemeMode == UI_THEME_DEFAULT) {
            enable_amoled_black_theme_override(lv_disp_get_default());
        }
    }
}

void DefaultUI::adjustTarget(lv_obj_t *obj, double percentage, double start, double range) const {
    double angle = start + range - range * percentage;

    lv_img_set_angle(obj, angle * -10);
    int x = static_cast<int>(std::cos(angle * M_PI / 180.0f) * 235.0);
    int y = static_cast<int>(std::sin(angle * M_PI / 180.0f) * -235.0);
    lv_obj_set_pos(obj, x, y);
}

void DefaultUI::loopTask(void *arg) {
    auto *ui = static_cast<DefaultUI *>(arg);
    while (true) {
        ui->loop();
        vTaskDelay(25 / portTICK_PERIOD_MS);
    }
}
