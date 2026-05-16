#ifndef QUELLOG_SETTINGS_PAGE_H_
#define QUELLOG_SETTINGS_PAGE_H_

#include "display/ui_page.h"

class SettingsPage : public UiPage {
public:
    const char* GetId() const override { return "settings"; }
    const char* GetTitle() const override { return "设置"; }
    PageModel BuildModel(const AppContext& context) const override;
};

#endif  // QUELLOG_SETTINGS_PAGE_H_
