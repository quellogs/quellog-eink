#include "ui_page_registry.h"

#include "pages/recent_records_page.h"
#include "pages/settings_page.h"
#include "pages/stats_page.h"

UiPageRegistry UiPageRegistry::CreateDefault() {
    UiPageRegistry registry;
    registry.Register(std::make_unique<StatsPage>());
    registry.Register(std::make_unique<RecentRecordsPage>());
    registry.Register(std::make_unique<SettingsPage>());
    return registry;
}

void UiPageRegistry::Register(std::unique_ptr<UiPage> page) {
    pages_.push_back(std::move(page));
}

const UiPage* UiPageRegistry::Get(int index) const {
    if (index < 0 || index >= Count()) {
        return nullptr;
    }
    return pages_[index].get();
}
