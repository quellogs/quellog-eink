#include "application.h"

#include <algorithm>

#include "settings.h"

namespace {

constexpr int kStatsPageIndex = 0;

}  // namespace

void Application::LoadSettings() {
    Settings settings("app", true);
    const int saved_page_index = settings.GetInt("page_index", kStatsPageIndex);
    current_page_index_ = NormalizeSavedPageIndex(saved_page_index);
    device_alias_ = settings.GetString("device_alias", "泉流迹墨水屏");
    if (current_page_index_ != saved_page_index) {
        SaveSettings();
    }
}

void Application::SaveSettings() {
    Settings settings("app", true);
    settings.SetInt("page_index", current_page_index_);
    settings.SetString("device_alias", device_alias_);
}

int Application::NormalizeSavedPageIndex(int saved_page_index) const {
    // Migrate persisted indexes from the legacy page order:
    // 0=总览, 1=最近记录, 2=统计, 3=设置.
    int normalized_index = saved_page_index;
    switch (saved_page_index) {
        case 0:
        case 2:
            normalized_index = 0;
            break;
        case 1:
            normalized_index = 1;
            break;
        case 3:
            normalized_index = 2;
            break;
        default:
            break;
    }

    const int page_count = std::max(1, pages_.Count());
    return std::clamp(normalized_index, 0, page_count - 1);
}

