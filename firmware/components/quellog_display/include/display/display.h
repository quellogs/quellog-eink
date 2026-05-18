#ifndef QUELLOG_DISPLAY_H_
#define QUELLOG_DISPLAY_H_

#include <cstdint>
#include <string>
#include <vector>

#include "top_status_bar.h"

struct _lv_font_t;
typedef struct _lv_font_t lv_font_t;

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

enum class TextAlign {
    Left = 0,
    Center,
    Right
};

enum class PixelColor {
    Black = 0,
    White,
};

enum class SplitViewMenuIcon {
    None = 0,
    Wifi,
    Bluetooth,
    Sound,
    Storage,
    Device,
    Power,
};

struct TextBlockModel {
    std::string text;
    TextAlign align = TextAlign::Left;
};

struct SplitViewDetailBar {
    std::string label;
    std::string value;
    int percent = 0;
};

struct SplitViewDetailOption {
    std::string label;
    bool selected = false;
    bool focused = false;
};

struct SplitViewDetailItem {
    std::string label;
    std::string value;
};

struct SplitViewDetailSection {
    std::string title;
    std::vector<SplitViewDetailItem> items;
};

struct WifiListItemModel {
    std::string ssid;
    int rssi = 0;
    bool secure = true;
    bool focused = false;
};

struct SplitViewMenuItem {
    std::string text;
    bool selected = false;
    SplitViewMenuIcon icon = SplitViewMenuIcon::None;
};

struct SplitViewModel {
    std::vector<SplitViewMenuItem> menu_items;
    std::vector<TextBlockModel> detail_blocks;
    std::vector<SplitViewDetailOption> detail_options;
    std::vector<SplitViewDetailBar> detail_bars;
    std::vector<SplitViewDetailSection> detail_sections;
    std::string detail_qr_payload;
    std::vector<TextBlockModel> detail_qr_blocks;
    bool wifi_switch_visible = false;
    bool wifi_switch_on = false;
    bool wifi_switch_focused = false;
    std::string wifi_switch_label = "无线网络";
    std::vector<WifiListItemModel> wifi_items;
};

struct ModalModel {
    bool visible = false;
    std::string title;
    std::string message;
    std::vector<SplitViewDetailOption> options;
    std::string qr_payload;
};

struct BarChartItem {
    std::string label;
    int64_t amount_cents = 0;
    bool is_others = false;
};

enum class AmountLabelMode {
    None = 0,
    TopHighlights,
    All
};

struct SummaryMetricModel {
    std::string label;
    std::string value;
    std::string note;
};

struct RecentRecordRowModel {
    std::string title;
    std::string category;
    std::string amount;
};

struct RecentRecordListModel {
    std::vector<RecentRecordRowModel> rows;
    bool visible = false;
    int page_index = 0;
    int page_count = 0;
    int total_count = 0;
};

struct BarChartModel {
    std::string title;
    std::vector<BarChartItem> items;
    int64_t max_amount_cents = 0;
    int max_visible_items = 8;
    bool include_others = false;
    AmountLabelMode amount_label_mode = AmountLabelMode::TopHighlights;
    int highlight_item_index = 0;
    int secondary_highlight_item_index = 1;
    bool show_reference_lines = true;
    bool show_baseline = true;
};

struct PageModel {
    std::vector<SummaryMetricModel> summary_metrics;
    std::vector<TextBlockModel> text_blocks;
    std::vector<BarChartModel> bar_charts;
    RecentRecordListModel recent_records;
    SplitViewModel split_view;
    ModalModel modal;
};

class Display {
public:
    Display() = default;
    virtual ~Display() = default;

    virtual void RenderPage(const PageModel& model, const TopStatusBarState& top_status_bar);
    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification);
    virtual void RequestFullRefresh();
    virtual void RequestPartialRefresh();

    virtual void BeginPage();
    virtual void EndPage();
    virtual void DrawText(const Rect& rect, const char* text, TextAlign align = TextAlign::Left);
    virtual void DrawText(const Rect& rect, const char* text, TextAlign align, PixelColor color);
    virtual void DrawLine(int x1, int y1, int x2, int y2);
    virtual void DrawRect(const Rect& rect);
    virtual void FillRect(const Rect& rect);
    virtual void Clear(bool white = true);
    virtual void SetPixel(int x, int y, bool black);

    int width() const { return width_; }
    int height() const { return height_; }

protected:
    int MeasureTextWidth(const char* text) const;
    std::string FitText(const std::string& text, int max_width, const char* ellipsis = "...") const;
    void DrawTextWithFont(const Rect& rect, const lv_font_t* font, const char* text, TextAlign align = TextAlign::Left);
    void DrawTextWithFont(
        const Rect& rect, const lv_font_t* font, const char* text, TextAlign align, PixelColor color);
    void DrawLineWithColor(int x1, int y1, int x2, int y2, PixelColor color);
    void DrawRectWithColor(const Rect& rect, PixelColor color);
    void FillRectWithColor(const Rect& rect, PixelColor color);
    void FillRoundedRect(const Rect& rect, int radius, PixelColor color);
    void FillCircle(int center_x, int center_y, int radius, PixelColor color);
    void DrawIconMask(const uint16_t* rows, int row_count, const Rect& rect, PixelColor color);
    void DrawMenuIcon(SplitViewMenuIcon icon, const Rect& rect, PixelColor color);
    void DrawHorizontalDashes(int x, int y, int width, int dash_length, int gap_length);
    void DrawArrowLine(int x1, int y1, int x2, int y2, int arrow_size);
    void FillRectPattern(const Rect& rect, int step_x, int step_y);
    void RenderQrCode(const std::string& payload, const Rect& rect);
    void RenderWifiSwitch(const SplitViewModel& model, const Rect& rect);
    void RenderWifiListItem(const WifiListItemModel& item, const Rect& rect);
    void DrawWifiSignalIcon(int x, int y, int rssi, PixelColor color);
    void RenderSummaryMetrics(const std::vector<SummaryMetricModel>& metrics, int* cursor_y);
    void RenderBarChart(const BarChartModel& model, const Rect& rect);
    void RenderRecentRecords(const RecentRecordListModel& model, int origin_y);
    void RenderSplitView(const SplitViewModel& model, int origin_y);
    void RenderModal(const ModalModel& model);

    int width_ = 0;
    int height_ = 0;
    TopStatusBar top_status_bar_;
};

class NoDisplay : public Display {
};

#endif  // QUELLOG_DISPLAY_H_
