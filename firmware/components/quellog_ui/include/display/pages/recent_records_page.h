#ifndef QUELLOG_RECENT_RECORDS_PAGE_H_
#define QUELLOG_RECENT_RECORDS_PAGE_H_

#include "display/ui_page.h"

class RecentRecordsPage : public UiPage {
public:
    const char* GetId() const override { return "recent_records"; }
    const char* GetTitle() const override { return "最近记录"; }
    PageModel BuildModel(const AppContext& context) const override;
};

#endif  // QUELLOG_RECENT_RECORDS_PAGE_H_
