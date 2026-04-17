#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H
#include "Event.h"
#include "Plugin.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

using EventCallback = std::function<void(Event &)>;

class Controller;
class PluginManager {
  public:
    PluginManager();
    ~PluginManager();

    void registerPlugin(Plugin *plugin);

    void setup(Controller *controller);
    void loop();

    void on(const String &eventId, const EventCallback &callback);
    void post(const Event &event);

    Event trigger(const String &eventId);
    Event trigger(const String &eventId, const String &key, const String &value);
    Event trigger(const String &eventId, const String &key, int value);
    Event trigger(const String &eventId, const String &key, float value);
    void trigger(Event &event);

  private:
    void drainPostedEvents();

    bool initialized = false;
    std::vector<Plugin *> plugins;
    std::map<std::string, std::vector<EventCallback>> listeners = {};
    std::deque<Event> postedEvents;
    SemaphoreHandle_t mutex = nullptr;
};

#endif // PLUGINMANAGER_H
