#ifndef QUELLOG_DASHBOARD_DATA_PROVIDER_H_
#define QUELLOG_DASHBOARD_DATA_PROVIDER_H_

#include "app_context.h"

// DashboardDataProvider 是应用层的数据入口；后续接入真实账单数据时只需替换这里的实现。
DashboardData LoadDashboardData();

#endif  // QUELLOG_DASHBOARD_DATA_PROVIDER_H_
