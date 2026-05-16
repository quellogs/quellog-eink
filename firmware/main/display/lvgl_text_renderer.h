#ifndef QUELLOG_LVGL_TEXT_RENDERER_H_
#define QUELLOG_LVGL_TEXT_RENDERER_H_

#include <string>

class Display;

struct _lv_font_t;
typedef struct _lv_font_t lv_font_t;

namespace LvglTextRenderer {

const lv_font_t* GetDefaultTextFont();
const lv_font_t* GetEmphasisTextFont();
const lv_font_t* SelectFontForHeight(int height);
int GetLineHeight(const lv_font_t* font);
int MeasureText(const lv_font_t* font, const char* text);
std::string FitText(const lv_font_t* font, const std::string& text, int max_width, const char* ellipsis = "...");
void DrawText(Display* display, const lv_font_t* font, int start_x, int start_y, int max_width, const char* text);

}  // namespace LvglTextRenderer

#endif  // QUELLOG_LVGL_TEXT_RENDERER_H_
