#include "display.h"
#include "lvgl_text_renderer.h"

#include <esp_log.h>
#include <qrcode.h>

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace {

constexpr char kTag[] = "Display";
constexpr int kPagePadding = 10;
constexpr int kLineHeight = 16;
constexpr int kSplitViewGap = 10;
constexpr int kSplitViewMenuWidth = 116;
constexpr int kSplitViewMenuRowHeight = 30;
constexpr int kSplitViewDetailPadding = 10;
constexpr int kSplitViewDetailBarHeight = 12;
constexpr int kSplitViewDetailBarGap = 12;
constexpr int kSplitViewDetailBarLabelGap = 3;
constexpr int kSplitViewDetailBarLabelPadding = 8;
constexpr int kSplitViewDetailBarSectionGap = 8;
constexpr int kSplitViewDetailOptionHeight = 28;
constexpr int kSplitViewDetailOptionGap = 4;
constexpr int kSplitViewDetailOptionMarkSize = 10;
constexpr int kSplitViewQrGap = 8;
constexpr int kSplitViewSectionGap = 10;
constexpr int kSplitViewSectionHeaderHeight = 22;
constexpr int kSplitViewSectionRowHeight = 26;
constexpr int kSplitViewSectionPadding = 8;
constexpr int kSplitViewMenuIconSize = 16;
constexpr int kSplitViewMenuRadius = 6;
constexpr int kSplitViewMenuTextHeight = 16;
constexpr int kWifiSwitchRowHeight = 34;
constexpr int kWifiSwitchWidth = 42;
constexpr int kWifiSwitchHeight = 20;
constexpr int kWifiListItemHeight = 28;
constexpr int kWifiSignalIconWidth = 18;
constexpr int kModalWidth = 300;
constexpr int kModalPadding = 12;
constexpr int kModalTitleHeight = 20;
constexpr int kModalMessageHeight = 16;
constexpr int kModalOptionHeight = 28;
constexpr int kModalOptionGap = 4;
constexpr int kModalQrSize = 104;
constexpr int kChartBottomLabelHeight = 18;
constexpr int kChartAmountHeight = 14;
constexpr int kChartGap = 5;
constexpr int kChartYAxisWidth = 10;
constexpr int kChartXAxisTickHeight = 4;
constexpr int kChartArrowSize = 5;
constexpr int kChartArrowOverhang = 6;
constexpr int kChartTopStatusBarGap = 4;
constexpr int kChartBottomPagePadding = 2;
constexpr int kSummaryLabelHeight = 12;
constexpr int kSummaryValueHeight = 24;
constexpr int kSummaryNoteHeight = 12;
constexpr int kSummaryCardHeight = 54;
constexpr int kSummaryGap = 6;
constexpr int kSummarySectionGap = 10;

int ClampNonNegative(int value) {
    return value < 0 ? 0 : value;
}

std::string FormatChartAmount(int64_t cents) {
    const long long whole = static_cast<long long>(cents / 100);
    const long long fraction = static_cast<long long>(cents % 100);
    const long long abs_fraction = fraction < 0 ? -fraction : fraction;
    return std::to_string(whole) + "." + (abs_fraction < 10 ? "0" : "") + std::to_string(abs_fraction);
}

int ClampToRange(int value, int min_value, int max_value) {
    return std::max(min_value, std::min(value, max_value));
}

bool ShouldShowAmountLabel(const BarChartModel& model, int item_index) {
    switch (model.amount_label_mode) {
        case AmountLabelMode::None:
            return false;
        case AmountLabelMode::All:
            return true;
        case AmountLabelMode::TopHighlights:
            return item_index == model.highlight_item_index || item_index == model.secondary_highlight_item_index;
        default:
            return false;
    }
}

struct QrRenderContext {
    Display* display = nullptr;
    Rect rect;
    int module_count = 0;
    int module_scale = 0;
    int origin_x = 0;
    int origin_y = 0;
};

void RenderQrCodeModule(esp_qrcode_handle_t qrcode, void* user_data) {
    QrRenderContext* context = static_cast<QrRenderContext*>(user_data);
    if (context == nullptr || context->display == nullptr) {
        return;
    }

    const int qrcode_size = esp_qrcode_get_size(qrcode);
    constexpr int kQuietZoneModules = 4;
    context->module_count = qrcode_size + (kQuietZoneModules * 2);
    context->module_scale =
        std::max(1, std::min(context->rect.w, context->rect.h) / context->module_count);
    const int draw_size = context->module_count * context->module_scale;
    context->origin_x = context->rect.x + ((context->rect.w - draw_size) / 2);
    context->origin_y = context->rect.y + ((context->rect.h - draw_size) / 2);

    for (int y = 0; y < draw_size; ++y) {
        for (int x = 0; x < draw_size; ++x) {
            context->display->SetPixel(context->origin_x + x, context->origin_y + y, false);
        }
    }

    const int quiet_zone = kQuietZoneModules;
    for (int module_y = 0; module_y < qrcode_size; ++module_y) {
        for (int module_x = 0; module_x < qrcode_size; ++module_x) {
            if (!esp_qrcode_get_module(qrcode, module_x, module_y)) {
                continue;
            }

            const int pixel_x = context->origin_x + ((module_x + quiet_zone) * context->module_scale);
            const int pixel_y = context->origin_y + ((module_y + quiet_zone) * context->module_scale);
            for (int y = 0; y < context->module_scale; ++y) {
                for (int x = 0; x < context->module_scale; ++x) {
                    context->display->SetPixel(pixel_x + x, pixel_y + y, true);
                }
            }
        }
    }
}

constexpr uint16_t kWifiIconRows[] = {
    0b0000000000000000,
    0b0000111111110000,
    0b0011111111111100,
    0b0111100000011110,
    0b1110000000000111,
    0b0000000000000000,
    0b0000011111100000,
    0b0001111111111000,
    0b0011100000011100,
    0b0000000000000000,
    0b0000001111000000,
    0b0000011111100000,
    0b0000011111100000,
    0b0000001111000000,
    0b0000000000000000,
    0b0000000000000000,
};

constexpr uint16_t kBluetoothIconRows[] = {
    0b0000001110000000,
    0b0000001111000000,
    0b0000001111100000,
    0b0000001111110000,
    0b0000001110111000,
    0b0000001110011100,
    0b0011001110111000,
    0b0001111111100000,
    0b0000111111000000,
    0b0001111111100000,
    0b0011001110111000,
    0b0000001110011100,
    0b0000001110111000,
    0b0000001111110000,
    0b0000001111100000,
    0b0000001110000000,
};

constexpr uint16_t kSoundIconRows[] = {
    0b0000000000000000,
    0b0000011000000100,
    0b0000111000001100,
    0b0001111000011000,
    0b0011111000110000,
    0b0111111001100110,
    0b1111111001001110,
    0b1111111001011100,
    0b1111111001011100,
    0b1111111001001110,
    0b0111111001100110,
    0b0011111000110000,
    0b0001111000011000,
    0b0000111000001100,
    0b0000011000000100,
    0b0000000000000000,
};

constexpr uint16_t kStorageIconRows[] = {
    0b0000000000000000,
    0b0011111111111100,
    0b0111111111111110,
    0b1111111111111111,
    0b1110000000000111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b1111111111111111,
    0b0111111111111110,
    0b0011111111111100,
    0b0000000000000000,
};

constexpr uint16_t kDeviceIconRows[] = {
    0b0000000000000000,
    0b0011111111111100,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0111111111111110,
    0b0011111111111100,
    0b0000001111000000,
    0b0000001111000000,
    0b0000111111110000,
    0b0001111111111000,
    0b0000000000000000,
};

}  // namespace

void Display::RenderPage(const PageModel& model, const TopStatusBarState& top_status_bar) {
    ESP_LOGI(kTag, "render page: %s", top_status_bar.title.c_str());

    BeginPage();

    const int inner_width = ClampNonNegative(width_ - (kPagePadding * 2));
    top_status_bar_.Render(this, top_status_bar);
    int cursor_y =
        TopStatusBar::kHeight + (model.bar_charts.empty() ? kPagePadding : kChartTopStatusBarGap);

    if (!model.split_view.menu_items.empty()) {
        RenderSplitView(model.split_view, cursor_y);
        RenderModal(model.modal);
        EndPage();
        return;
    }

    if (!model.summary_metrics.empty()) {
        RenderSummaryMetrics(model.summary_metrics, &cursor_y);
    }

    if (!model.bar_charts.empty()) {
        const int chart_count = static_cast<int>(model.bar_charts.size());
        const int chart_available_height = ClampNonNegative(
            height_ - cursor_y - kChartBottomPagePadding - (static_cast<int>(model.text_blocks.size()) * kLineHeight));
        const int each_chart_height = chart_count > 0 ? std::max(120, chart_available_height / chart_count) : 0;
        for (const BarChartModel& chart : model.bar_charts) {
            RenderBarChart(chart, {kPagePadding, cursor_y, inner_width, each_chart_height});
            cursor_y += each_chart_height + 6;
        }
    }

    for (const TextBlockModel& block : model.text_blocks) {
        DrawText({kPagePadding, cursor_y, inner_width, kLineHeight}, block.text.c_str(), block.align);
        cursor_y += kLineHeight;
    }

    RenderModal(model.modal);
    EndPage();
}

void Display::SetStatus(const char* status) {
    ESP_LOGI(kTag, "status: %s", status);
}

void Display::ShowNotification(const char* notification) {
    ESP_LOGI(kTag, "notice: %s", notification);
}

void Display::RequestFullRefresh() {
    ESP_LOGI(kTag, "full refresh requested");
}

void Display::RequestPartialRefresh() {
    ESP_LOGI(kTag, "partial refresh requested");
}

void Display::BeginPage() {
    ESP_LOGI(kTag, "begin page");
    Clear(true);
}

void Display::EndPage() {
    ESP_LOGI(kTag, "end page");
}

void Display::DrawText(const Rect& rect, const char* text, TextAlign align) {
    DrawText(rect, text, align, PixelColor::Black);
}

void Display::DrawText(const Rect& rect, const char* text, TextAlign align, PixelColor color) {
    if (text == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const lv_font_t* font = LvglTextRenderer::SelectFontForHeight(rect.h);
    DrawTextWithFont(rect, font, text, align, color);
}

void Display::DrawTextWithFont(const Rect& rect, const lv_font_t* font, const char* text, TextAlign align) {
    DrawTextWithFont(rect, font, text, align, PixelColor::Black);
}

void Display::DrawTextWithFont(
    const Rect& rect, const lv_font_t* font, const char* text, TextAlign align, PixelColor color) {
    if (text == nullptr || font == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int text_width = LvglTextRenderer::MeasureText(font, text);
    int cursor_x = rect.x;
    if (align == TextAlign::Center) {
        cursor_x = rect.x + ((rect.w - text_width) / 2);
    } else if (align == TextAlign::Right) {
        cursor_x = rect.x + rect.w - text_width;
    }
    cursor_x = ClampToRange(cursor_x, rect.x, rect.x + rect.w);
    const int cursor_y = rect.y + std::max(0, (rect.h - LvglTextRenderer::GetLineHeight(font)) / 2);
    LvglTextRenderer::DrawText(this, font, cursor_x, cursor_y, rect.w, text, color == PixelColor::Black);
}

void Display::DrawLine(int x1, int y1, int x2, int y2) {
    int dx = std::abs(x2 - x1);
    const int sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1);
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        SetPixel(x1, y1, true);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void Display::DrawRect(const Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    DrawLine(rect.x, rect.y, rect.x + rect.w - 1, rect.y);
    DrawLine(rect.x, rect.y, rect.x, rect.y + rect.h - 1);
    DrawLine(rect.x + rect.w - 1, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1);
    DrawLine(rect.x, rect.y + rect.h - 1, rect.x + rect.w - 1, rect.y + rect.h - 1);
}

void Display::FillRect(const Rect& rect) {
    FillRectWithColor(rect, PixelColor::Black);
}

void Display::DrawLineWithColor(int x1, int y1, int x2, int y2, PixelColor color) {
    int dx = std::abs(x2 - x1);
    const int sx = x1 < x2 ? 1 : -1;
    int dy = -std::abs(y2 - y1);
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        SetPixel(x1, y1, color == PixelColor::Black);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        const int e2 = err * 2;
        if (e2 >= dy) {
            err += dy;
            x1 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y1 += sy;
        }
    }
}

void Display::DrawRectWithColor(const Rect& rect, PixelColor color) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    DrawLineWithColor(rect.x, rect.y, rect.x + rect.w - 1, rect.y, color);
    DrawLineWithColor(rect.x, rect.y, rect.x, rect.y + rect.h - 1, color);
    DrawLineWithColor(rect.x + rect.w - 1, rect.y, rect.x + rect.w - 1, rect.y + rect.h - 1, color);
    DrawLineWithColor(rect.x, rect.y + rect.h - 1, rect.x + rect.w - 1, rect.y + rect.h - 1, color);
}

void Display::FillRectWithColor(const Rect& rect, PixelColor color) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            SetPixel(x, y, color == PixelColor::Black);
        }
    }
}

void Display::FillRoundedRect(const Rect& rect, int radius, PixelColor color) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int safe_radius = std::max(0, std::min(radius, std::min(rect.w, rect.h) / 2));
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            int corner_dx = 0;
            if (x < rect.x + safe_radius) {
                corner_dx = rect.x + safe_radius - x;
            } else if (x >= rect.x + rect.w - safe_radius) {
                corner_dx = x - (rect.x + rect.w - safe_radius - 1);
            }

            int corner_dy = 0;
            if (y < rect.y + safe_radius) {
                corner_dy = rect.y + safe_radius - y;
            } else if (y >= rect.y + rect.h - safe_radius) {
                corner_dy = y - (rect.y + rect.h - safe_radius - 1);
            }

            if (corner_dx > 0 && corner_dy > 0 &&
                (corner_dx * corner_dx + corner_dy * corner_dy) > safe_radius * safe_radius) {
                continue;
            }
            SetPixel(x, y, color == PixelColor::Black);
        }
    }
}

void Display::FillCircle(int center_x, int center_y, int radius, PixelColor color) {
    if (radius <= 0) {
        return;
    }

    const int radius_squared = radius * radius;
    for (int y = center_y - radius; y <= center_y + radius; ++y) {
        for (int x = center_x - radius; x <= center_x + radius; ++x) {
            const int dx = x - center_x;
            const int dy = y - center_y;
            if ((dx * dx) + (dy * dy) <= radius_squared) {
                SetPixel(x, y, color == PixelColor::Black);
            }
        }
    }
}

void Display::DrawIconMask(const uint16_t* rows, int row_count, const Rect& rect, PixelColor color) {
    if (rows == nullptr || row_count <= 0 || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int draw_size = std::min(rect.w, rect.h);
    const int origin_x = rect.x + ((rect.w - draw_size) / 2);
    const int origin_y = rect.y + ((rect.h - draw_size) / 2);
    for (int y = 0; y < draw_size; ++y) {
        const int source_y = (y * row_count) / draw_size;
        const uint16_t row = rows[source_y];
        for (int x = 0; x < draw_size; ++x) {
            const int source_x = (x * 16) / draw_size;
            if ((row & (1U << (15 - source_x))) != 0) {
                SetPixel(origin_x + x, origin_y + y, color == PixelColor::Black);
            }
        }
    }
}

void Display::DrawMenuIcon(SplitViewMenuIcon icon, const Rect& rect, PixelColor color) {
    switch (icon) {
        case SplitViewMenuIcon::Wifi:
            DrawIconMask(kWifiIconRows, static_cast<int>(std::size(kWifiIconRows)), rect, color);
            break;
        case SplitViewMenuIcon::Bluetooth:
            DrawIconMask(kBluetoothIconRows, static_cast<int>(std::size(kBluetoothIconRows)), rect, color);
            break;
        case SplitViewMenuIcon::Sound:
            DrawIconMask(kSoundIconRows, static_cast<int>(std::size(kSoundIconRows)), rect, color);
            break;
        case SplitViewMenuIcon::Storage:
            DrawIconMask(kStorageIconRows, static_cast<int>(std::size(kStorageIconRows)), rect, color);
            break;
        case SplitViewMenuIcon::Device:
            DrawIconMask(kDeviceIconRows, static_cast<int>(std::size(kDeviceIconRows)), rect, color);
            break;
        case SplitViewMenuIcon::None:
        default:
            break;
    }
}

void Display::Clear(bool white) {
    (void)white;
}

void Display::SetPixel(int x, int y, bool black) {
    (void)x;
    (void)y;
    (void)black;
}

int Display::MeasureTextWidth(const char* text) const {
    return LvglTextRenderer::MeasureText(LvglTextRenderer::GetDefaultTextFont(), text);
}

std::string Display::FitText(const std::string& text, int max_width, const char* ellipsis) const {
    return LvglTextRenderer::FitText(LvglTextRenderer::GetDefaultTextFont(), text, max_width, ellipsis);
}

void Display::DrawHorizontalDashes(int x, int y, int width, int dash_length, int gap_length) {
    if (width <= 0 || dash_length <= 0) {
        return;
    }

    const int step = dash_length + std::max(0, gap_length);
    for (int cursor_x = x; cursor_x < x + width; cursor_x += step) {
        const int dash_end = std::min(x + width - 1, cursor_x + dash_length - 1);
        DrawLine(cursor_x, y, dash_end, y);
    }
}

void Display::DrawArrowLine(int x1, int y1, int x2, int y2, int arrow_size) {
    DrawLine(x1, y1, x2, y2);
    if (arrow_size <= 0) {
        return;
    }

    if (x1 == x2) {
        const int direction = y2 >= y1 ? 1 : -1;
        DrawLine(x2, y2, x2 - arrow_size, y2 - (arrow_size * direction));
        DrawLine(x2, y2, x2 + arrow_size, y2 - (arrow_size * direction));
        return;
    }

    if (y1 == y2) {
        const int direction = x2 >= x1 ? 1 : -1;
        DrawLine(x2, y2, x2 - (arrow_size * direction), y2 - arrow_size);
        DrawLine(x2, y2, x2 - (arrow_size * direction), y2 + arrow_size);
    }
}

void Display::FillRectPattern(const Rect& rect, int step_x, int step_y) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const int safe_step_x = std::max(2, step_x);
    const int safe_step_y = std::max(2, step_y);
    for (int y = rect.y + 1; y < rect.y + rect.h - 1; y += safe_step_y) {
        for (int x = rect.x + 1; x < rect.x + rect.w - 1; x += safe_step_x) {
            SetPixel(x, y, true);
        }
    }
}

void Display::RenderQrCode(const std::string& payload, const Rect& rect) {
    if (payload.empty() || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    QrRenderContext context;
    context.display = this;
    context.rect = rect;

    esp_qrcode_config_t config = {};
    config.display_func_with_cb = RenderQrCodeModule;
    config.max_qrcode_version = 10;
    config.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;
    config.user_data = &context;

    const esp_err_t result = esp_qrcode_generate(&config, payload.c_str());
    if (result != ESP_OK) {
        ESP_LOGW(kTag, "qrcode generate failed: %s", esp_err_to_name(result));
        return;
    }

    const int qrcode_size = context.module_count;
    if (qrcode_size <= 0 || context.module_scale <= 0) {
        return;
    }

    const int draw_size = qrcode_size * context.module_scale;
    DrawRect({context.origin_x - 1, context.origin_y - 1, draw_size + 2, draw_size + 2});
}

void Display::RenderWifiSwitch(const SplitViewModel& model, const Rect& rect) {
    if (!model.wifi_switch_visible || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const PixelColor foreground_color = model.wifi_switch_focused ? PixelColor::White : PixelColor::Black;
    if (model.wifi_switch_focused) {
        FillRoundedRect(rect, kSplitViewMenuRadius, PixelColor::Black);
    } else {
        DrawRect(rect);
    }

    DrawText({rect.x + 8, rect.y + 8, rect.w - kWifiSwitchWidth - 22, kLineHeight},
             "无线网络",
             TextAlign::Left,
             foreground_color);

    const Rect switch_rect = {
        rect.x + rect.w - kWifiSwitchWidth - 8,
        rect.y + ((rect.h - kWifiSwitchHeight) / 2),
        kWifiSwitchWidth,
        kWifiSwitchHeight
    };
    DrawRectWithColor(switch_rect, foreground_color);
    const int knob_size = kWifiSwitchHeight - 6;
    const int knob_x = model.wifi_switch_on
        ? switch_rect.x + switch_rect.w - knob_size - 3
        : switch_rect.x + 3;
    FillCircle(knob_x + (knob_size / 2),
               switch_rect.y + (switch_rect.h / 2),
               knob_size / 2,
               foreground_color);
    if (model.wifi_switch_on) {
        DrawText({switch_rect.x, switch_rect.y + 2, switch_rect.w, kLineHeight},
                 "ON",
                 TextAlign::Center,
                 foreground_color);
    }
}

void Display::DrawWifiSignalIcon(int x, int y, int rssi, PixelColor color) {
    const int bars = rssi >= -55 ? 4 : (rssi >= -67 ? 3 : (rssi >= -78 ? 2 : 1));
    constexpr int bar_width = 3;
    constexpr int bar_gap = 2;
    for (int index = 0; index < 4; ++index) {
        const int bar_height = 4 + (index * 3);
        const Rect bar = {
            x + (index * (bar_width + bar_gap)),
            y + 14 - bar_height,
            bar_width,
            bar_height
        };
        if (index < bars) {
            FillRectWithColor(bar, color);
        } else {
            DrawRectWithColor(bar, color);
        }
    }
}

void Display::RenderWifiListItem(const WifiListItemModel& item, const Rect& rect) {
    const PixelColor foreground_color = item.focused ? PixelColor::White : PixelColor::Black;
    if (item.focused) {
        FillRoundedRect(rect, kSplitViewMenuRadius, PixelColor::Black);
    } else {
        DrawRect(rect);
    }

    DrawWifiSignalIcon(rect.x + 8, rect.y + 7, item.rssi, foreground_color);
    const int lock_width = item.secure ? 14 : 0;
    const int text_x = rect.x + 8 + kWifiSignalIconWidth + 8;
    const int text_width = ClampNonNegative(rect.w - (text_x - rect.x) - lock_width - 12);
    const std::string fitted_ssid = FitText(item.ssid, text_width);
    DrawText({text_x, rect.y + 6, text_width, kLineHeight}, fitted_ssid.c_str(), TextAlign::Left, foreground_color);
    if (item.secure) {
        const int lock_x = rect.x + rect.w - 18;
        DrawRectWithColor({lock_x, rect.y + 12, 10, 8}, foreground_color);
        DrawLineWithColor(lock_x + 2, rect.y + 12, lock_x + 2, rect.y + 9, foreground_color);
        DrawLineWithColor(lock_x + 7, rect.y + 12, lock_x + 7, rect.y + 9, foreground_color);
        DrawLineWithColor(lock_x + 2, rect.y + 9, lock_x + 7, rect.y + 9, foreground_color);
    }
}

void Display::RenderSummaryMetrics(const std::vector<SummaryMetricModel>& metrics, int* cursor_y) {
    if (cursor_y == nullptr || metrics.empty()) {
        return;
    }

    const int metric_count = static_cast<int>(metrics.size());
    const int inner_width = ClampNonNegative(width_ - (kPagePadding * 2));
    const int total_gap = std::max(0, metric_count - 1) * kSummaryGap;
    const int card_width = metric_count > 0 ? ClampNonNegative((inner_width - total_gap) / metric_count) : 0;
    int x = kPagePadding;

    for (int index = 0; index < metric_count; ++index) {
        const SummaryMetricModel& metric = metrics[index];
        const Rect card = {x, *cursor_y, card_width, kSummaryCardHeight};
        DrawRect(card);
        DrawHorizontalDashes(card.x + 4, card.y + 16, card.w - 8, 4, 3);
        DrawText({card.x + 6, card.y + 3, card.w - 12, kSummaryLabelHeight}, metric.label.c_str(), TextAlign::Left);
        DrawText({card.x + 6, card.y + 16, card.w - 12, kSummaryValueHeight}, metric.value.c_str(), TextAlign::Left);
        if (!metric.note.empty()) {
            const std::string note = FitText(metric.note, card.w - 12);
            DrawText({card.x + 6, card.y + card.h - kSummaryNoteHeight - 4, card.w - 12, kSummaryNoteHeight},
                     note.c_str(), TextAlign::Left);
        }
        x += card_width + kSummaryGap;
    }

    *cursor_y += kSummaryCardHeight + kSummarySectionGap;
}

void Display::RenderBarChart(const BarChartModel& model, const Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const Rect plot = {
        rect.x,
        rect.y,
        rect.w,
        rect.h
    };
    if (model.items.empty() || model.max_amount_cents <= 0 || plot.w <= 0 || plot.h <= 0) {
        DrawText({plot.x + 4, plot.y + 4, plot.w - 8, kLineHeight}, "No spending data", TextAlign::Left);
        return;
    }

    const int item_count = static_cast<int>(model.items.size());
    const int amount_reserved_height = model.amount_label_mode == AmountLabelMode::None ? 4 : kChartAmountHeight + 2;
    const int plot_bottom = plot.y + plot.h - 1;
    const int axis_left = plot.x + kChartYAxisWidth;
    const int axis_right = plot.x + plot.w - kChartArrowSize - kChartArrowOverhang - 1;
    const int bar_area_top = plot.y + amount_reserved_height + 2;
    const int bar_area_bottom = plot_bottom - kChartBottomLabelHeight - kChartXAxisTickHeight;
    const int bar_area_height = std::max(16, bar_area_bottom - bar_area_top);
    const int total_gap = std::max(0, item_count - 1) * kChartGap;
    const int plot_content_width = std::max(0, axis_right - axis_left - 4);
    const int bar_width = item_count > 0 ? std::max(8, (plot_content_width - total_gap) / item_count) : 0;
    const int content_width = item_count > 0 ? (bar_width * item_count) + total_gap : 0;
    int x = axis_left + 4 + std::max(0, (plot_content_width - content_width) / 2);

    const int top_tick_y = bar_area_top;
    const int middle_tick_y = bar_area_top + (bar_area_height / 2);

    if (model.show_reference_lines) {
        DrawHorizontalDashes(axis_left + 1, top_tick_y, axis_right - axis_left, 4, 4);
        DrawHorizontalDashes(axis_left + 1, middle_tick_y, axis_right - axis_left, 4, 4);
    }
    DrawArrowLine(axis_left, bar_area_bottom, axis_right + kChartArrowOverhang, bar_area_bottom, kChartArrowSize);
    DrawArrowLine(axis_left, bar_area_bottom, axis_left, plot.y, kChartArrowSize);

    for (int index = 0; index < item_count; ++index) {
        const BarChartItem& item = model.items[index];
        const int bar_height = std::max(
            3,
            static_cast<int>((static_cast<long long>(bar_area_height) * item.amount_cents) / model.max_amount_cents));
        const Rect bar = {
            x,
            bar_area_bottom - bar_height,
            std::min(bar_width, plot.x + plot.w - x),
            bar_height
        };
        FillRect(bar);
        DrawRect(bar);

        if (ShouldShowAmountLabel(model, index)) {
            const std::string amount = FormatChartAmount(item.amount_cents);
            DrawText({x - 6, bar.y - kChartAmountHeight - 2, bar_width + 12, kChartAmountHeight},
                     amount.c_str(), TextAlign::Center);
        }

        const int tick_x = x + (bar.w / 2);
        DrawLine(tick_x, bar_area_bottom, tick_x, bar_area_bottom + kChartXAxisTickHeight);
        const std::string label = FitText(item.label, bar_width + 6, "..");
        DrawText({x - 3, plot_bottom - kChartBottomLabelHeight + 1, bar_width + 6, kChartBottomLabelHeight - 1},
                 label.c_str(), TextAlign::Center);
        x += bar_width + kChartGap;
    }
}

void Display::RenderSplitView(const SplitViewModel& model, int origin_y) {
    const int available_height = ClampNonNegative(height_ - origin_y - kPagePadding);
    const int inner_width = ClampNonNegative(width_ - (kPagePadding * 2));
    if (available_height <= 0 || inner_width <= 0) {
        return;
    }

    const int menu_width = std::min(kSplitViewMenuWidth, std::max(80, inner_width / 3));
    const Rect menu_rect = {kPagePadding, origin_y, menu_width, available_height};
    const Rect detail_rect = {
        menu_rect.x + menu_rect.w + kSplitViewGap,
        origin_y,
        ClampNonNegative(inner_width - menu_rect.w - kSplitViewGap),
        available_height
    };

    DrawLine(menu_rect.x + menu_rect.w + (kSplitViewGap / 2),
             origin_y,
             menu_rect.x + menu_rect.w + (kSplitViewGap / 2),
             origin_y + available_height);

    int menu_cursor_y = menu_rect.y + 2;
    for (const SplitViewMenuItem& item : model.menu_items) {
        if (menu_cursor_y + kSplitViewMenuRowHeight > menu_rect.y + menu_rect.h - 4) {
            break;
        }

        const Rect item_rect = {menu_rect.x + 2, menu_cursor_y, menu_rect.w - 4, kSplitViewMenuRowHeight};
        const PixelColor foreground_color = item.selected ? PixelColor::White : PixelColor::Black;
        if (item.selected) {
            FillRoundedRect(item_rect, kSplitViewMenuRadius, PixelColor::Black);
        }
        const Rect icon_rect = {
            item_rect.x + 8,
            item_rect.y + ((item_rect.h - kSplitViewMenuIconSize) / 2),
            kSplitViewMenuIconSize,
            kSplitViewMenuIconSize
        };
        DrawMenuIcon(item.icon, icon_rect, foreground_color);
        const int text_x = icon_rect.x + icon_rect.w + 8;
        const std::string fitted_text = FitText(item.text, item_rect.x + item_rect.w - text_x - 6);
        const Rect text_rect = {
            text_x,
            item_rect.y + ((item_rect.h - kSplitViewMenuTextHeight) / 2),
            item_rect.x + item_rect.w - text_x - 6,
            kSplitViewMenuTextHeight
        };
        DrawText(text_rect,
                 fitted_text.c_str(),
                 TextAlign::Left,
                 foreground_color);
        menu_cursor_y += kSplitViewMenuRowHeight + 4;
    }

    int detail_cursor_y = detail_rect.y + kSplitViewDetailPadding;
    const int detail_text_width = ClampNonNegative(detail_rect.w - (kSplitViewDetailPadding * 2));
    if (model.wifi_switch_visible) {
        RenderWifiSwitch(model,
                         {detail_rect.x + kSplitViewDetailPadding,
                          detail_cursor_y,
                          detail_text_width,
                          kWifiSwitchRowHeight});
        detail_cursor_y += kWifiSwitchRowHeight + kSplitViewDetailOptionGap;
    }

    for (const SplitViewDetailOption& option : model.detail_options) {
        if (detail_cursor_y + kSplitViewDetailOptionHeight > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        const Rect option_rect = {
            detail_rect.x + kSplitViewDetailPadding,
            detail_cursor_y,
            detail_text_width,
            kSplitViewDetailOptionHeight
        };
        const PixelColor foreground_color = option.focused ? PixelColor::White : PixelColor::Black;
        if (option.focused) {
            FillRoundedRect(option_rect, kSplitViewMenuRadius, PixelColor::Black);
        } else {
            DrawRect(option_rect);
        }

        const Rect mark_rect = {
            option_rect.x + 8,
            option_rect.y + ((option_rect.h - kSplitViewDetailOptionMarkSize) / 2),
            kSplitViewDetailOptionMarkSize,
            kSplitViewDetailOptionMarkSize
        };
        DrawRectWithColor(mark_rect, foreground_color);
        if (option.selected) {
            FillRectWithColor({mark_rect.x + 2, mark_rect.y + 2, mark_rect.w - 4, mark_rect.h - 4}, foreground_color);
        }

        const int text_x = mark_rect.x + mark_rect.w + 8;
        const std::string fitted_label = FitText(option.label, option_rect.x + option_rect.w - text_x - 8);
        DrawText({text_x, option_rect.y + 6, option_rect.x + option_rect.w - text_x - 8, kLineHeight},
                 fitted_label.c_str(),
                 TextAlign::Left,
                 foreground_color);
        detail_cursor_y += kSplitViewDetailOptionHeight + kSplitViewDetailOptionGap;
    }

    if (!model.detail_options.empty() && !model.detail_blocks.empty()) {
        detail_cursor_y += kSplitViewDetailOptionGap;
    }

    for (const TextBlockModel& block : model.detail_blocks) {
        if (detail_cursor_y + kLineHeight > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        DrawText({detail_rect.x + kSplitViewDetailPadding, detail_cursor_y, detail_text_width, kLineHeight},
                 block.text.c_str(),
                 block.align);
        detail_cursor_y += kLineHeight;
    }

    if (!model.wifi_items.empty()) {
        detail_cursor_y += kSplitViewDetailOptionGap;
    }
    for (const WifiListItemModel& item : model.wifi_items) {
        if (detail_cursor_y + kWifiListItemHeight > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }
        RenderWifiListItem(item,
                           {detail_rect.x + kSplitViewDetailPadding,
                            detail_cursor_y,
                            detail_text_width,
                            kWifiListItemHeight});
        detail_cursor_y += kWifiListItemHeight + kSplitViewDetailOptionGap;
    }

    if ((!model.detail_blocks.empty() || !model.detail_options.empty() || !model.wifi_items.empty() ||
         model.wifi_switch_visible) &&
        !model.detail_sections.empty()) {
        detail_cursor_y += kSplitViewSectionGap;
    }

    for (const SplitViewDetailSection& section : model.detail_sections) {
        const int item_count = static_cast<int>(section.items.size());
        const int section_height = kSplitViewSectionHeaderHeight + (item_count * kSplitViewSectionRowHeight);
        if (detail_cursor_y + section_height > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        const Rect section_rect = {
            detail_rect.x + kSplitViewDetailPadding,
            detail_cursor_y,
            detail_text_width,
            section_height
        };
        DrawRect(section_rect);
        FillRect({section_rect.x, section_rect.y, section_rect.w, kSplitViewSectionHeaderHeight});

        const std::string fitted_title = FitText(section.title, section_rect.w - (kSplitViewSectionPadding * 2));
        DrawText({section_rect.x + kSplitViewSectionPadding,
                  section_rect.y + 3,
                  section_rect.w - (kSplitViewSectionPadding * 2),
                  kLineHeight},
                 fitted_title.c_str(),
                 TextAlign::Left,
                 PixelColor::White);

        int label_width = 0;
        for (const SplitViewDetailItem& item : section.items) {
            label_width = std::max(label_width, MeasureTextWidth(item.label.c_str()) + 8);
        }
        label_width = ClampToRange(label_width, 52, std::max(52, section_rect.w / 4));

        int row_y = section_rect.y + kSplitViewSectionHeaderHeight;
        for (const SplitViewDetailItem& item : section.items) {
            if (row_y > section_rect.y + kSplitViewSectionHeaderHeight) {
                DrawLine(section_rect.x + kSplitViewSectionPadding,
                         row_y,
                         section_rect.x + section_rect.w - kSplitViewSectionPadding,
                         row_y);
            }

            const int value_x = section_rect.x + kSplitViewSectionPadding + label_width;
            const int value_width =
                ClampNonNegative(section_rect.w - (kSplitViewSectionPadding * 2) - label_width);
            const std::string fitted_label = FitText(item.label, label_width - 4);
            const std::string fitted_value = FitText(item.value, value_width);
            DrawText({section_rect.x + kSplitViewSectionPadding, row_y + 5, label_width - 4, kLineHeight},
                     fitted_label.c_str(),
                     TextAlign::Left);
            DrawText({value_x, row_y + 5, value_width, kLineHeight},
                     fitted_value.c_str(),
                     TextAlign::Right);
            row_y += kSplitViewSectionRowHeight;
        }

        detail_cursor_y += section_height + kSplitViewSectionGap;
    }

    if (!model.detail_qr_payload.empty()) {
        const int caption_height = static_cast<int>(model.detail_qr_blocks.size()) * kLineHeight;
        const int remaining_height =
            detail_rect.y + detail_rect.h - kSplitViewDetailPadding - detail_cursor_y - caption_height;
        const int qr_size = std::min(detail_text_width, std::max(0, remaining_height));
        if (qr_size >= 80) {
            detail_cursor_y += kSplitViewQrGap;
            RenderQrCode(model.detail_qr_payload,
                         {detail_rect.x + kSplitViewDetailPadding,
                          detail_cursor_y,
                          detail_text_width,
                          qr_size - kSplitViewQrGap});
            detail_cursor_y += qr_size;
        }
    }

    for (const TextBlockModel& block : model.detail_qr_blocks) {
        if (detail_cursor_y + kLineHeight > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        DrawText({detail_rect.x + kSplitViewDetailPadding, detail_cursor_y, detail_text_width, kLineHeight},
                 block.text.c_str(),
                 block.align);
        detail_cursor_y += kLineHeight;
    }

    if ((!model.detail_blocks.empty() || !model.detail_options.empty() || !model.detail_qr_payload.empty() ||
         !model.detail_qr_blocks.empty()) &&
        !model.detail_bars.empty()) {
        detail_cursor_y += kSplitViewDetailBarSectionGap;
    }

    int detail_bar_label_width = 0;
    for (const SplitViewDetailBar& bar : model.detail_bars) {
        detail_bar_label_width =
            std::max(detail_bar_label_width, MeasureTextWidth(bar.label.c_str()) + kSplitViewDetailBarLabelPadding);
    }
    detail_bar_label_width = std::min(detail_bar_label_width, detail_text_width / 2);

    for (const SplitViewDetailBar& bar : model.detail_bars) {
        const int required_height = kLineHeight + kSplitViewDetailBarLabelGap + kSplitViewDetailBarHeight;
        if (detail_cursor_y + required_height > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        const int value_x = detail_rect.x + kSplitViewDetailPadding + detail_bar_label_width;
        const int value_width = ClampNonNegative(detail_text_width - detail_bar_label_width);
        const std::string fitted_label = FitText(bar.label, detail_bar_label_width - 4);
        const std::string fitted_value = FitText(bar.value, value_width);
        DrawText({detail_rect.x + kSplitViewDetailPadding,
                  detail_cursor_y,
                  detail_bar_label_width - 4,
                  kLineHeight},
                 fitted_label.c_str(),
                 TextAlign::Left);
        DrawText({value_x, detail_cursor_y, value_width, kLineHeight},
                 fitted_value.c_str(),
                 TextAlign::Right);
        detail_cursor_y += kLineHeight + kSplitViewDetailBarLabelGap;

        const Rect bar_rect = {
            detail_rect.x + kSplitViewDetailPadding,
            detail_cursor_y,
            detail_text_width,
            kSplitViewDetailBarHeight
        };
        DrawRect(bar_rect);

        const int fill_area_width = ClampNonNegative(bar_rect.w - 2);
        const int fill_width = (fill_area_width * ClampToRange(bar.percent, 0, 100)) / 100;
        if (fill_width > 0) {
            FillRect({bar_rect.x + 1, bar_rect.y + 1, fill_width, ClampNonNegative(bar_rect.h - 2)});
        }
        detail_cursor_y += kSplitViewDetailBarHeight + kSplitViewDetailBarGap;
    }

}

void Display::RenderModal(const ModalModel& model) {
    if (!model.visible) {
        return;
    }

    const int option_count = static_cast<int>(model.options.size());
    const bool has_message = !model.message.empty();
    const bool has_qr = !model.qr_payload.empty();
    const int options_height =
        option_count == 0 ? 0 : (option_count * kModalOptionHeight) + ((option_count - 1) * kModalOptionGap);
    const int qr_height = has_qr ? kModalQrSize + kModalOptionGap : 0;
    const int message_height = has_message ? kModalMessageHeight + kModalOptionGap : 0;
    const int modal_width = std::min(kModalWidth, std::max(0, width_ - (kPagePadding * 2)));
    const int modal_height =
        (kModalPadding * 2) + kModalTitleHeight + kModalOptionGap + message_height + options_height + qr_height;
    const Rect modal_rect = {
        (width_ - modal_width) / 2,
        std::max(kPagePadding, (height_ - modal_height) / 2),
        modal_width,
        std::min(modal_height, std::max(0, height_ - (kPagePadding * 2)))
    };

    FillRectWithColor(modal_rect, PixelColor::White);
    DrawRect(modal_rect);
    DrawRect({modal_rect.x + 2, modal_rect.y + 2, modal_rect.w - 4, modal_rect.h - 4});

    int cursor_y = modal_rect.y + kModalPadding;
    const int content_x = modal_rect.x + kModalPadding;
    const int content_width = std::max(0, modal_rect.w - (kModalPadding * 2));
    const std::string fitted_title = FitText(model.title, content_width);
    DrawText({content_x, cursor_y, content_width, kModalTitleHeight}, fitted_title.c_str(), TextAlign::Center);
    cursor_y += kModalTitleHeight + kModalOptionGap;

    if (has_message) {
        const std::string fitted_message = FitText(model.message, content_width);
        DrawText({content_x, cursor_y, content_width, kModalMessageHeight}, fitted_message.c_str(), TextAlign::Center);
        cursor_y += kModalMessageHeight + kModalOptionGap;
    }

    for (const SplitViewDetailOption& option : model.options) {
        if (cursor_y + kModalOptionHeight > modal_rect.y + modal_rect.h - kModalPadding) {
            break;
        }

        const Rect option_rect = {content_x, cursor_y, content_width, kModalOptionHeight};
        const PixelColor foreground_color = option.focused ? PixelColor::White : PixelColor::Black;
        if (option.focused) {
            FillRoundedRect(option_rect, kSplitViewMenuRadius, PixelColor::Black);
        } else {
            DrawRect(option_rect);
        }

        const int text_x = option_rect.x + 8;
        const std::string label = FitText(option.label, option_rect.x + option_rect.w - text_x - 8);
        DrawText({text_x, option_rect.y + 6, option_rect.x + option_rect.w - text_x - 8, kLineHeight},
                 label.c_str(),
                 TextAlign::Left,
                 foreground_color);
        cursor_y += kModalOptionHeight + kModalOptionGap;
    }

    if (has_qr && cursor_y + kModalQrSize <= modal_rect.y + modal_rect.h - kModalPadding) {
        RenderQrCode(model.qr_payload, {content_x, cursor_y, content_width, kModalQrSize});
    }
}
