#ifndef QUELLOG_DASHBOARD_DATA_PROVIDER_H_
#define QUELLOG_DASHBOARD_DATA_PROVIDER_H_

#include "app_context.h"

struct DashboardLoadResult {
    DashboardData data;
    bool success = false;
    std::string status_message;
};

// DashboardDataProvider 是应用层的数据入口，负责从 quellog-server 设备 API 同步账单数据。
DashboardLoadResult LoadDashboardData(DashboardPeriod period);

#endif  // QUELLOG_DASHBOARD_DATA_PROVIDER_H_
