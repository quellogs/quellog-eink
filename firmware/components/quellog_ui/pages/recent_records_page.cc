#include "recent_records_page.h"

namespace {

std::string FormatAmount(int64_t cents) {
    const long long whole = static_cast<long long>(cents / 100);
    const long long fraction = static_cast<long long>(cents % 100);
    const long long abs_fraction = fraction < 0 ? -fraction : fraction;
    return "CNY " + std::to_string(whole) + "." + (abs_fraction < 10 ? "0" : "") + std::to_string(abs_fraction);
}

std::string FormatRecordLine(const RecordSummary& record) {
    return record.title + " | " + record.category + " | " + FormatAmount(record.amount_cents);
}

}  // namespace

PageModel RecentRecordsPage::BuildModel(const AppContext& context) const {
    PageModel model;
    if (context.dashboard.recent_records.empty()) {
        model.text_blocks.push_back({"暂无本地记录。"});
    } else {
        for (const RecordSummary& record : context.dashboard.recent_records) {
            model.text_blocks.push_back({FormatRecordLine(record)});
        }
    }
    return model;
}
