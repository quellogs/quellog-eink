#include "recent_records_page.h"

#include <algorithm>

namespace {

std::string FormatAmount(int64_t cents) {
    const bool negative = cents < 0;
    const int64_t abs_cents = negative ? -cents : cents;
    const long long whole = static_cast<long long>(abs_cents / 100);
    const long long fraction = static_cast<long long>(abs_cents % 100);
    return std::string(negative ? "-" : "") + std::to_string(whole) + "." +
        (fraction < 10 ? "0" : "") + std::to_string(fraction) + " 元";
}

int CalculatePageCount(int total_count, int page_size) {
    if (total_count <= 0 || page_size <= 0) {
        return 0;
    }
    return (total_count + page_size - 1) / page_size;
}

}  // namespace

PageModel RecentRecordsPage::BuildModel(const AppContext& context) const {
    PageModel model;
    const int total_count = static_cast<int>(context.dashboard.recent_records.size());
    const int page_size = std::max(1, context.recent_records_page_size);
    const int page_count = CalculatePageCount(total_count, page_size);
    const int page_index = page_count > 0 ? std::clamp(context.recent_records_page_index, 0, page_count - 1) : 0;
    const int start_index = page_index * page_size;
    const int end_index = std::min(total_count, start_index + page_size);

    model.recent_records.visible = true;
    model.recent_records.total_count = total_count;
    model.recent_records.page_index = page_index;
    model.recent_records.page_count = page_count;

    for (int index = start_index; index < end_index; ++index) {
        const RecordSummary& record = context.dashboard.recent_records[index];
        model.recent_records.rows.push_back({
            record.title,
            record.category,
            FormatAmount(record.amount_cents),
        });
    }
    return model;
}
