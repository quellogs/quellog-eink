#include "top_status_bar.h"

#include "display.h"
#include "lvgl_text_renderer.h"

#include <algorithm>

namespace {

constexpr int kHorizontalPadding = 12;
constexpr int kVerticalPadding = 6;
constexpr int kItemSpacing = 8;
constexpr int kSeparatorOffset = 1;
constexpr int kTitleHeight = 20;
constexpr int kWifiIconWidth = 18;
constexpr int kBatteryIconWidth = 24;
constexpr int kBatteryIconHeight = 12;
constexpr int kHotspotBadgeWidth = 24;
constexpr int kHotspotBadgeHeight = 16;

void DrawWifiIcon(Display* display, int x, int center_y) {
    if (display == nullptr) {
        return;
    }

    display->DrawLine(x + 2, center_y + 1, x + 8, center_y - 5);
    display->DrawLine(x + 8, center_y - 5, x + 14, center_y + 1);
    display->DrawLine(x + 4, center_y + 2, x + 8, center_y - 2);
    display->DrawLine(x + 8, center_y - 2, x + 12, center_y + 2);
    display->DrawLine(x + 6, center_y + 3, x + 8, center_y + 1);
    display->DrawLine(x + 8, center_y + 1, x + 10, center_y + 3);
    display->FillRect({x + 7, center_y + 4, 3, 3});
}

void DrawChargingBolt(Display* display, int x, int y) {
    if (display == nullptr) {
        return;
    }

    display->DrawLine(x + 4, y, x + 1, y + 5);
    display->DrawLine(x + 1, y + 5, x + 4, y + 5);
    display->DrawLine(x + 4, y + 5, x + 2, y + 10);
    display->DrawLine(x + 2, y + 10, x + 6, y + 4);
    display->DrawLine(x + 6, y + 4, x + 3, y + 4);
}

void DrawBatteryIcon(Display* display, int x, int center_y, int level, bool charging) {
    if (display == nullptr) {
        return;
    }

    const int body_y = center_y - (kBatteryIconHeight / 2);
    const Rect body = {x, body_y, kBatteryIconWidth - 4, kBatteryIconHeight};
    const Rect terminal = {x + body.w, body_y + 3, 4, kBatteryIconHeight - 6};
    display->DrawRect(body);
    display->FillRect(terminal);

    if (charging) {
        DrawChargingBolt(display, body.x + 6, body.y + 1);
        return;
    }

    const int clamped_level = std::clamp(level, 0, 100);
    const int fill_width = ((body.w - 4) * clamped_level) / 100;
    if (fill_width > 0) {
        display->FillRect({body.x + 2, body.y + 2, fill_width, body.h - 4});
    }
}

std::string FitTextForHeight(int height, const std::string& text, int max_width) {
    const lv_font_t* font = LvglTextRenderer::SelectFontForHeight(height);
    return LvglTextRenderer::FitText(font, text, max_width);
}

}  // namespace

void TopStatusBar::Render(Display* display, const TopStatusBarState& state) const {
    if (display == nullptr) {
        return;
    }

    const int width = display->width();
    const int center_y = kVerticalPadding + (kTitleHeight / 2);
    int right_x = width - kHorizontalPadding;

    if (state.battery_visible) {
        right_x -= kBatteryIconWidth;
        DrawBatteryIcon(display, right_x, center_y, state.battery_level, state.battery_charging);
        right_x -= kItemSpacing;
    }

    if (state.hotspot_visible) {
        right_x -= kHotspotBadgeWidth;
        display->DrawRect({right_x, center_y - (kHotspotBadgeHeight / 2), kHotspotBadgeWidth, kHotspotBadgeHeight});
        display->DrawText({right_x, center_y - (kHotspotBadgeHeight / 2), kHotspotBadgeWidth, kHotspotBadgeHeight},
                          "AP", TextAlign::Center);
        right_x -= kItemSpacing;
    }

    if (state.wifi_visible) {
        right_x -= kWifiIconWidth;
        DrawWifiIcon(display, right_x, center_y);
        right_x -= kItemSpacing;
    }

    const int title_width = std::max(0, right_x - kHorizontalPadding);
    const std::string fitted_title = FitTextForHeight(kTitleHeight, state.title, title_width);
    display->DrawText({kHorizontalPadding, kVerticalPadding, title_width, kTitleHeight},
                      fitted_title.c_str(),
                      TextAlign::Left);

    display->DrawLine(0, kHeight - kSeparatorOffset, width - 1, kHeight - kSeparatorOffset);
}
