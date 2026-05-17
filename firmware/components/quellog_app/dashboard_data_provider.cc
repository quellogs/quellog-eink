#include "dashboard_data_provider.h"

DashboardData LoadDashboardData() {
    DashboardData data;
    data.today_expense_cents = 4680;
    data.month_expense_cents = 125430;
    data.budget_used_percent = 63;
    data.sync_status = "本地占位数据";
    data.recent_records = {
        {"工作日午餐", "餐饮", 3200},
        {"地铁充值", "交通", 2000},
        {"咖啡豆补货", "日用", 6480},
    };
    data.categories = {
        {"餐饮", 34, 42500},
        {"日用", 27, 33800},
        {"交通", 12, 15400},
        {"居家", 11, 13900},
    };
    return data;
}
