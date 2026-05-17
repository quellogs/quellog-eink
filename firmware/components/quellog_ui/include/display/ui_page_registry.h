#ifndef QUELLOG_UI_PAGE_REGISTRY_H_
#define QUELLOG_UI_PAGE_REGISTRY_H_

#include <memory>
#include <vector>

#include "ui_page.h"

class UiPageRegistry {
public:
    UiPageRegistry() = default;
    UiPageRegistry(UiPageRegistry&&) = default;
    UiPageRegistry& operator=(UiPageRegistry&&) = default;

    static UiPageRegistry CreateDefault();

    void Register(std::unique_ptr<UiPage> page);
    const UiPage* Get(int index) const;
    int Count() const { return static_cast<int>(pages_.size()); }

private:
    std::vector<std::unique_ptr<UiPage>> pages_;
};

#endif  // QUELLOG_UI_PAGE_REGISTRY_H_
