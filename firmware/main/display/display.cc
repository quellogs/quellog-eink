#include "display.h"
#include "lvgl_text_renderer.h"

#include <esp_log.h>

#include <algorithm>
#include <cstdlib>

namespace {

constexpr char kTag[] = "Display";
constexpr int kPagePadding = 12;
constexpr int kLineHeight = 16;
constexpr int kSplitViewGap = 10;
constexpr int kSplitViewMenuWidth = 116;
constexpr int kSplitViewMenuRowHeight = 24;
constexpr int kSplitViewDetailPadding = 10;
constexpr int kChartTitleHeight = 16;
constexpr int kChartBottomLabelHeight = 28;
constexpr int kChartAmountHeight = 14;
constexpr int kChartGap = 10;

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

}  // namespace

void Display::RenderPage(const PageModel& model, const TopStatusBarState& top_status_bar) {
    ESP_LOGI(kTag, "render page: %s", top_status_bar.title.c_str());

    BeginPage();

    const int inner_width = ClampNonNegative(width_ - (kPagePadding * 2));
    top_status_bar_.Render(this, top_status_bar);
    int cursor_y = TopStatusBar::kHeight + kPagePadding;

    if (!model.split_view.menu_items.empty()) {
        RenderSplitView(model.split_view, cursor_y);
        EndPage();
        return;
    }

    if (!model.bar_charts.empty()) {
        const int chart_count = static_cast<int>(model.bar_charts.size());
        const int chart_available_height = ClampNonNegative(
            height_ - cursor_y - kPagePadding - (static_cast<int>(model.text_blocks.size()) * kLineHeight) - 8);
        const int each_chart_height = chart_count > 0 ? std::max(72, chart_available_height / chart_count) : 0;
        for (const BarChartModel& chart : model.bar_charts) {
            RenderBarChart(chart, {kPagePadding, cursor_y, inner_width, each_chart_height});
            cursor_y += each_chart_height + 4;
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
    if (text == nullptr || rect.w <= 0 || rect.h <= 0) {
        return;
    }

    const lv_font_t* font = LvglTextRenderer::SelectFontForHeight(rect.h);
    if (font == nullptr) {
        ESP_LOGW(kTag, "lvgl font unavailable, cannot render text: %s", text);
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
    LvglTextRenderer::DrawText(this, font, cursor_x, cursor_y, rect.w, text);
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
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }
    for (int y = rect.y; y < rect.y + rect.h; ++y) {
        for (int x = rect.x; x < rect.x + rect.w; ++x) {
            SetPixel(x, y, true);
        }
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

void Display::RenderBarChart(const BarChartModel& model, const Rect& rect) {
    if (rect.w <= 0 || rect.h <= 0) {
        return;
    }

    DrawText({rect.x, rect.y, rect.w, kChartTitleHeight}, model.title.c_str(), TextAlign::Left);

    const Rect plot = {
        rect.x,
        rect.y + kChartTitleHeight + 2,
        rect.w,
        ClampNonNegative(rect.h - kChartTitleHeight - kChartBottomLabelHeight - 4)
    };
    if (model.items.empty() || model.max_amount_cents <= 0 || plot.w <= 0 || plot.h <= 0) {
        DrawText({plot.x + 4, plot.y + 4, plot.w - 8, kLineHeight}, "No spending data", TextAlign::Left);
        return;
    }
    DrawRect(plot);

    const int item_count = static_cast<int>(model.items.size());
    const int total_gap = (item_count + 1) * kChartGap;
    const int bar_width = std::max(12, (plot.w - total_gap) / std::max(1, item_count));
    int x = plot.x + kChartGap;
    const int plot_bottom = plot.y + plot.h - 1;

    for (const BarChartItem& item : model.items) {
        const int bar_height = std::max(
            2,
            static_cast<int>((static_cast<long long>(plot.h - kChartAmountHeight - 6) * item.amount_cents) / model.max_amount_cents));
        const Rect bar = {
            x,
            plot_bottom - kChartBottomLabelHeight - bar_height,
            std::min(bar_width, plot.x + plot.w - x),
            bar_height
        };
        FillRect(bar);
        DrawRect(bar);

        if (model.show_amount_labels) {
            const std::string amount = FormatChartAmount(item.amount_cents);
            DrawText({x - 6, bar.y - kChartAmountHeight - 2, bar_width + 12, kChartAmountHeight},
                     amount.c_str(), TextAlign::Center);
        }

        const std::string label = FitText(item.label, bar_width + 12);
        DrawText({x - 6, plot_bottom - kChartBottomLabelHeight + 6, bar_width + 12, kChartBottomLabelHeight - 6},
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

    DrawRect(menu_rect);
    DrawRect(detail_rect);
    DrawLine(menu_rect.x + menu_rect.w + (kSplitViewGap / 2),
             origin_y,
             menu_rect.x + menu_rect.w + (kSplitViewGap / 2),
             origin_y + available_height);

    int menu_cursor_y = menu_rect.y + 6;
    for (const SplitViewMenuItem& item : model.menu_items) {
        if (menu_cursor_y + kSplitViewMenuRowHeight > menu_rect.y + menu_rect.h - 4) {
            break;
        }

        const Rect item_rect = {menu_rect.x + 4, menu_cursor_y, menu_rect.w - 8, kSplitViewMenuRowHeight};
        if (item.selected) {
            DrawRect(item_rect);
        }
        const std::string fitted_text = FitText(item.text, item_rect.w - 10);
        DrawText({item_rect.x + 5, item_rect.y, item_rect.w - 10, item_rect.h}, fitted_text.c_str(), TextAlign::Left);
        menu_cursor_y += kSplitViewMenuRowHeight + 6;
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
