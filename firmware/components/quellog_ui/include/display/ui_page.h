#ifndef QUELLOG_UI_PAGE_H_
#define QUELLOG_UI_PAGE_H_

#include "app_context.h"
#include "display.h"

class UiPage {
public:
    virtual ~UiPage() = default;

    virtual const char* GetId() const = 0;
    virtual const char* GetTitle() const = 0;
    virtual PageModel BuildModel(const AppContext& context) const = 0;
};

#endif  // QUELLOG_UI_PAGE_H_
