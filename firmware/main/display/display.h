#ifndef QUELLOG_DISPLAY_H_
#define QUELLOG_DISPLAY_H_

#include <cstdint>
#include <string>
#include <vector>

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

struct TextBlockModel {
    std::string text;
    TextAlign align = TextAlign::Left;
};

struct BarChartItem {
    std::string label;
    int64_t amount_cents = 0;
    bool is_others = false;
};

struct BarChartModel {
    std::string title;
    std::vector<BarChartItem> items;
    int64_t max_amount_cents = 0;
    bool show_amount_labels = true;
};

struct PageModel {
    std::string title;
    std::vector<TextBlockModel> text_blocks;
    std::vector<BarChartModel> bar_charts;
    std::string footer;
};

class Display {
public:
    Display() = default;
    virtual ~Display() = default;

    virtual void RenderPage(const PageModel& model);
    virtual void SetStatus(const char* status);
    virtual void ShowNotification(const char* notification);
    virtual void RequestFullRefresh();
    virtual void RequestPartialRefresh();

    virtual void BeginPage();
    virtual void EndPage();
    virtual void DrawText(const Rect& rect, const char* text, TextAlign align = TextAlign::Left);
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
    void RenderBarChart(const BarChartModel& model, const Rect& rect);

    int width_ = 0;
    int height_ = 0;
};

class NoDisplay : public Display {
};

#endif  // QUELLOG_DISPLAY_H_
