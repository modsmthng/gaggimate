#include "PluginManager.h"
#include "RecursiveLock.h"

PluginManager::PluginManager() { mutex = xSemaphoreCreateRecursiveMutex(); }

PluginManager::~PluginManager() {
    if (mutex != nullptr) {
        vSemaphoreDelete(mutex);
        mutex = nullptr;
    }
}

void PluginManager::registerPlugin(Plugin *plugin) { plugins.push_back(plugin); }

void PluginManager::setup(Controller *controller) {
    ESP_LOGV("PluginManager", "Setting up PluginManager");
    on("system:dummy", [](const Event &) {
        // Register a dummy event so the event map is initialized properly
    });
    for (const auto &plugin : plugins) {
        plugin->setup(controller, this);
    }
    initialized = true;
}

void PluginManager::loop() {
    if (!initialized)
        return;
    drainPostedEvents();
    for (auto &plugin : plugins) {
        plugin->loop();
    }
    drainPostedEvents();
}

void PluginManager::on(const String &eventId, const EventCallback &callback) {
    ESP_LOGV("PluginManager", "Registering listener: %s", eventId.c_str());
    RecursiveLockGuard lock(mutex);
    listeners[std::string(eventId.c_str())].push_back(callback);
}

void PluginManager::post(const Event &event) {
    RecursiveLockGuard lock(mutex);
    postedEvents.push_back(event);
}

Event PluginManager::trigger(const String &eventId) {
    Event event;
    event.id = eventId;
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const String &value) {
    Event event;
    event.id = eventId;
    event.setString(key, value);
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const int value) {
    Event event;
    event.id = eventId;
    event.setInt(key, value);
    trigger(event);
    return event;
}

Event PluginManager::trigger(const String &eventId, const String &key, const float value) {
    Event event;
    event.id = eventId;
    event.setFloat(key, value);
    trigger(event);
    return event;
}

void PluginManager::trigger(Event &event) {
    ESP_LOGV("PluginManager", "Triggering event: %s", event.id.c_str());
    std::vector<EventCallback> callbacks;
    {
        RecursiveLockGuard lock(mutex);
        auto it = listeners.find(std::string(event.id.c_str()));
        if (it != listeners.end()) {
            callbacks = it->second;
        }
    }

    for (auto const &callback : callbacks) {
        callback(event);
        if (event.stopPropagation) {
            break;
        }
    }
}

void PluginManager::drainPostedEvents() {
    std::deque<Event> queue;
    {
        RecursiveLockGuard lock(mutex);
        queue.swap(postedEvents);
    }

    while (!queue.empty()) {
        Event event = queue.front();
        queue.pop_front();
        trigger(event);
    }
}
