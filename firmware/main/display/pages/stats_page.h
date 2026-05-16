#ifndef QUELLOG_STATS_PAGE_H_
#define QUELLOG_STATS_PAGE_H_

#include "display/ui_page.h"

class StatsPage : public UiPage {
public:
    const char* GetId() const override { return "stats"; }
    const char* GetTitle() const override { return "统计"; }
    PageModel BuildModel(const AppContext& context) const override;
};

#endif  // QUELLOG_STATS_PAGE_H_
