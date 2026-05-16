#include "lvgl_text_renderer.h"

#include "display.h"

#ifdef LV_LVGL_H_INCLUDE_SIMPLE
#include "lvgl.h"
#else
#include "lvgl/lvgl.h"
#endif

#include <algorithm>

LV_FONT_DECLARE(SourceHanSansSC_Regular_slim);
LV_FONT_DECLARE(SourceHanSansSC_Medium_slim);

namespace {

constexpr int kUnknownGlyphAdvanceDivisor = 2;
constexpr int kEmphasisThresholdHeight = 22;

uint32_t NextUtf8CodePoint(const char** cursor) {
    const uint8_t* text = reinterpret_cast<const uint8_t*>(*cursor);
    if (*text == 0) {
        return 0;
    }

    uint32_t codepoint = 0;
    int length = 0;
    if (*text < 0x80) {
        codepoint = *text;
        length = 1;
    } else if ((*text & 0xE0U) == 0xC0U) {
        codepoint = *text & 0x1FU;
        length = 2;
    } else if ((*text & 0xF0U) == 0xE0U) {
        codepoint = *text & 0x0FU;
        length = 3;
    } else if ((*text & 0xF8U) == 0xF0U) {
        codepoint = *text & 0x07U;
        length = 4;
    } else {
        *cursor += 1;
        return 0xFFFD;
    }

    for (int index = 1; index < length; ++index) {
        if ((text[index] & 0xC0U) != 0x80U) {
            *cursor += 1;
            return 0xFFFD;
        }
        codepoint = (codepoint << 6U) | (text[index] & 0x3FU);
    }

    *cursor += length;
    return codepoint;
}

int UnknownGlyphAdvance(const lv_font_t* font) {
    if (font == nullptr) {
        return 0;
    }
    return std::max(1, static_cast<int>(font->line_height / kUnknownGlyphAdvanceDivisor));
}

}  // namespace

namespace LvglTextRenderer {

const lv_font_t* GetDefaultTextFont() {
    return &SourceHanSansSC_Regular_slim;
}

const lv_font_t* GetEmphasisTextFont() {
    return &SourceHanSansSC_Medium_slim;
}

const lv_font_t* SelectFontForHeight(int height) {
    return height >= kEmphasisThresholdHeight ? GetEmphasisTextFont() : GetDefaultTextFont();
}

int GetLineHeight(const lv_font_t* font) {
    return font == nullptr ? 0 : static_cast<int>(font->line_height);
}

int MeasureText(const lv_font_t* font, const char* text) {
    if (font == nullptr || text == nullptr || *text == '\0') {
        return 0;
    }

    int width = 0;
    const char* cursor = text;
    while (*cursor != '\0') {
        const uint32_t codepoint = NextUtf8CodePoint(&cursor);
        if (codepoint == 0) {
            break;
        }
        if (codepoint == '\n') {
            break;
        }

        lv_font_glyph_dsc_t glyph = {};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) {
            width += UnknownGlyphAdvance(font);
            continue;
        }
        width += glyph.adv_w;
    }

    return width;
}

std::string FitText(const lv_font_t* font, const std::string& text, int max_width, const char* ellipsis) {
    if (font == nullptr || max_width <= 0 || text.empty()) {
        return "";
    }
    if (MeasureText(font, text.c_str()) <= max_width) {
        return text;
    }

    const std::string suffix = ellipsis == nullptr ? "" : std::string(ellipsis);
    const int suffix_width = MeasureText(font, suffix.c_str());
    std::string result;

    const char* cursor = text.c_str();
    int width = 0;
    while (*cursor != '\0') {
        const char* glyph_start = cursor;
        const uint32_t codepoint = NextUtf8CodePoint(&cursor);
        if (codepoint == 0) {
            break;
        }

        int advance = 0;
        if (codepoint == '\n') {
            advance = UnknownGlyphAdvance(font);
        } else {
            lv_font_glyph_dsc_t glyph = {};
            if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) {
                advance = UnknownGlyphAdvance(font);
            } else {
                advance = glyph.adv_w;
            }
        }

        if (width + advance + suffix_width > max_width) {
            break;
        }

        result.append(glyph_start, cursor - glyph_start);
        width += advance;
    }

    if (result.empty()) {
        return suffix_width <= max_width ? suffix : "";
    }

    return result + suffix;
}

void DrawText(Display* display, const lv_font_t* font, int start_x, int start_y, int max_width, const char* text) {
    if (display == nullptr || font == nullptr || text == nullptr) {
        return;
    }

    int cursor_x = start_x;
    int cursor_y = start_y;
    const char* cursor = text;
    while (*cursor != '\0') {
        const uint32_t codepoint = NextUtf8CodePoint(&cursor);
        if (codepoint == 0) {
            break;
        }

        if (codepoint == '\n') {
            cursor_x = start_x;
            cursor_y += font->line_height;
            continue;
        }

        lv_font_glyph_dsc_t glyph = {};
        if (!lv_font_get_glyph_dsc(font, &glyph, codepoint, 0)) {
            cursor_x += UnknownGlyphAdvance(font);
            continue;
        }

        glyph.req_raw_bitmap = 1;
        const uint8_t* bitmap = reinterpret_cast<const uint8_t*>(font->get_glyph_bitmap(&glyph, nullptr));
        glyph.req_raw_bitmap = 0;
        if (bitmap == nullptr) {
            cursor_x += glyph.adv_w;
            continue;
        }

        const int glyph_x = cursor_x + glyph.ofs_x;
        const int glyph_y = cursor_y + font->line_height - font->base_line - glyph.ofs_y - glyph.box_h;
        const int row_bits = glyph.stride > 0 ? static_cast<int>(glyph.stride * 8) : static_cast<int>(glyph.box_w);

        for (int row = 0; row < glyph.box_h; ++row) {
            for (int col = 0; col < glyph.box_w; ++col) {
                const int bit_index = row * row_bits + col;
                const bool black = (bitmap[bit_index >> 3] >> (7 - (bit_index & 0x07))) & 1U;
                if (!black) {
                    continue;
                }

                const int pixel_x = glyph_x + col;
                const int pixel_y = glyph_y + row;
                if (pixel_x < 0 || pixel_x >= display->width() || pixel_y < 0 || pixel_y >= display->height()) {
                    continue;
                }
                display->SetPixel(pixel_x, pixel_y, true);
            }
        }

        cursor_x += glyph.adv_w;
        if (max_width > 0 && cursor_x > start_x + max_width) {
            break;
        }
    }
}

}  // namespace LvglTextRenderer
