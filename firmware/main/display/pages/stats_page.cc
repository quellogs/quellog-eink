#include "stats_page.h"

#include <algorithm>
namespace {

std::string FormatAmount(int64_t cents) {
    const long long whole = static_cast<long long>(cents / 100);
    const long long fraction = static_cast<long long>(cents % 100);
    const long long abs_fraction = fraction < 0 ? -fraction : fraction;
    return "CNY " + std::to_string(whole) + "." + (abs_fraction < 10 ? "0" : "") + std::to_string(abs_fraction);
}

std::vector<BarChartItem> BuildTopSpendingChartItems(const std::vector<CategorySummary>& categories) {
    std::vector<CategorySummary> sorted = categories;
    std::sort(sorted.begin(), sorted.end(), [](const CategorySummary& left, const CategorySummary& right) {
        return left.amount_cents > right.amount_cents;
    });

    std::vector<BarChartItem> items;
    items.reserve(std::min<size_t>(6, sorted.size()));

    int64_t others_amount = 0;
    for (size_t i = 0; i < sorted.size(); ++i) {
        if (i < 5) {
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
        const std::vector<BarChartItem> items = BuildTopSpendingChartItems(context.dashboard.categories);
        model.bar_charts.push_back({
            "按分类支出",
            items,
            GetMaxAmountCents(items),
            true,
        });
        model.text_blocks.push_back({"以下为分类汇总，超出前五项并入“其他”。"});
        for (const CategorySummary& category : context.dashboard.categories) {
            model.text_blocks.push_back({category.category + "  " + FormatAmount(category.amount_cents)});
        }
    }
    return model;
}
