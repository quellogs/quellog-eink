#include "stats_page.h"

#include <algorithm>
#include <string>

namespace {

constexpr int kPreferredVisibleCategoryCount = 9;

const char* GetPeriodLabel(DashboardPeriod period) {
    switch (period) {
        case DashboardPeriod::Quarter:
            return "季度";
        case DashboardPeriod::Year:
            return "年度";
        case DashboardPeriod::Month:
        default:
            return "月度";
    }
}

std::vector<BarChartItem> BuildTopSpendingChartItems(const std::vector<CategorySummary>& categories,
                                                     int max_visible_items) {
    std::vector<CategorySummary> sorted = categories;
    std::sort(sorted.begin(), sorted.end(), [](const CategorySummary& left, const CategorySummary& right) {
        return left.amount_cents > right.amount_cents;
    });

    std::vector<BarChartItem> items;
    items.reserve(std::min(static_cast<size_t>(max_visible_items), sorted.size()));

    int64_t others_amount = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (static_cast<int>(i) < max_visible_items) {
            items.push_back({sorted[i].category, sorted[i].amount_cents, false});
        } else {
            others_amount += sorted[i].amount_cents;
        }
    }

    if (others_amount > 0) {
        items.push_back({"其他", others_amount, true});
    }

    return items;
}

int64_t GetMaxAmountCents(const std::vector<BarChartItem>& items) {
    int64_t max_amount = 0;
    for (const BarChartItem& item : items) {
        max_amount = std::max(max_amount, item.amount_cents);
    }
    return max_amount;
}

}  // namespace

PageModel StatsPage::BuildModel(const AppContext& context) const {
    PageModel model;
    if (context.dashboard.categories.empty()) {
        model.text_blocks.push_back({"暂无分类统计。"});
    } else {
        const std::vector<BarChartItem> items =
            BuildTopSpendingChartItems(context.dashboard.categories, kPreferredVisibleCategoryCount);
        model.bar_charts.push_back({
            std::string(GetPeriodLabel(context.stats_period)) + "分类支出",
            items,
            GetMaxAmountCents(items),
            kPreferredVisibleCategoryCount,
            static_cast<int>(context.dashboard.categories.size()) > kPreferredVisibleCategoryCount,
            AmountLabelMode::All,
            0,
            std::min(1, static_cast<int>(items.size()) - 1),
            false,
            true,
        });
    }

    if (context.stats_period_modal_visible) {
        model.modal.visible = true;
        model.modal.title = "统计周期";
        model.modal.message = "选择分类统计范围";
        model.modal.options.push_back({
            "月度",
            context.stats_period == DashboardPeriod::Month,
            context.stats_period_focus_index == static_cast<int>(DashboardPeriod::Month),
        });
        model.modal.options.push_back({
            "季度",
            context.stats_period == DashboardPeriod::Quarter,
            context.stats_period_focus_index == static_cast<int>(DashboardPeriod::Quarter),
        });
        model.modal.options.push_back({
            "年度",
            context.stats_period == DashboardPeriod::Year,
            context.stats_period_focus_index == static_cast<int>(DashboardPeriod::Year),
        });
    }
    return model;
}
