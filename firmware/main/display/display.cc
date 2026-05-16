#include "display.h"
#include "lvgl_text_renderer.h"

#include <esp_log.h>

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
constexpr int kSplitViewMenuIconSize = 16;
constexpr int kSplitViewMenuRadius = 6;
constexpr int kSplitViewMenuTextHeight = 16;
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
    for (const TextBlockModel& block : model.detail_blocks) {
        if (detail_cursor_y + kLineHeight > detail_rect.y + detail_rect.h - kSplitViewDetailPadding) {
            break;
        }

        DrawText({detail_rect.x + kSplitViewDetailPadding, detail_cursor_y, detail_text_width, kLineHeight},
                 block.text.c_str(),
                 block.align);
        detail_cursor_y += kLineHeight;
    }
}
