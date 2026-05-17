#include "stats_page.h"

#include <algorithm>

namespace {

constexpr int kPreferredVisibleCategoryCount = 9;

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
            "按分类支出",
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
    return model;
}
