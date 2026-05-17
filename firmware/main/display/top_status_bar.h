#ifndef QUELLOG_TOP_STATUS_BAR_H_
#define QUELLOG_TOP_STATUS_BAR_H_

#include <string>

class Display;

struct TopStatusBarState {
    std::string title;
    bool wifi_visible = false;
    bool wifi_connected = false;
    bool hotspot_visible = false;
    bool battery_visible = false;
    int battery_level = 0;
    bool battery_charging = false;
};

class TopStatusBar {
public:
    static constexpr int kHeight = 30;

    void Render(Display* display, const TopStatusBarState& state) const;
};

#endif  // QUELLOG_TOP_STATUS_BAR_H_
