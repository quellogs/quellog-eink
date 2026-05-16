#include "settings_page.h"

#include <string>

namespace {

std::string BuildRefreshModeLabel(const AppContext& context) {
    return context.refresh_policy == RefreshPolicy::Timed ? "定时刷新" : "手动刷新";
}

void AppendWifiStatusBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    if (context.wifi_config_mode) {
        blocks->push_back({"WiFi 状态  配网模式"});
        blocks->push_back({"热点名称  " + context.wifi_ap_ssid});
        blocks->push_back({"访问地址  " + context.wifi_ap_url});
        return;
    }

    if (context.wifi_connected) {
        blocks->push_back({"WiFi 状态  已连接"});
        blocks->push_back({"当前网络  " + context.wifi_ssid});
        blocks->push_back({"设备 IP  " + context.wifi_ip});
        return;
    }

    if (context.wifi_connecting) {
        blocks->push_back({"WiFi 状态  连接中..."});
        return;
    }

    blocks->push_back({"WiFi 状态  未连接"});
}

void AppendDeviceInfoBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({""});
    blocks->push_back({"设备信息"});
    blocks->push_back({"设备别名  " + context.device_alias});
    blocks->push_back({"板型  " + context.board_type});
    if (context.battery_known) {
        blocks->push_back({"电量  " + std::to_string(context.battery_level) + "%"});
        blocks->push_back({context.battery_charging ? "电池状态  充电中" : "电池状态  未充电"});
    } else {
        blocks->push_back({"电量  暂不可用"});
    }
}

}  // namespace

PageModel SettingsPage::BuildModel(const AppContext& context) const {
    PageModel model;
    model.split_view.menu_items = {
        {"刷新模式", context.settings_selected_item == 0},
        {"重新配网", context.settings_selected_item == 1},
        {"返回首页", context.settings_selected_item == 2},
    };

    switch (context.settings_selected_item) {
        case 0:
            model.split_view.detail_blocks.push_back({"刷新模式"});
            model.split_view.detail_blocks.push_back({"当前设置  " + BuildRefreshModeLabel(context)});
            model.split_view.detail_blocks.push_back({"确认键可切换手动/定时刷新。"});
            model.split_view.detail_blocks.push_back({"最近刷新  " + context.last_refresh_label});
            break;
        case 1:
            model.split_view.detail_blocks.push_back({"重新配网"});
            model.split_view.detail_blocks.push_back({"确认键后设备会进入热点配网模式。"});
            model.split_view.detail_blocks.push_back({"可用手机连接热点并重新配置 WiFi。"});
            AppendWifiStatusBlocks(context, &model.split_view.detail_blocks);
            break;
        case 2:
        default:
            model.split_view.detail_blocks.push_back({"返回首页"});
            model.split_view.detail_blocks.push_back({"确认键后将返回统计页。"});
            model.split_view.detail_blocks.push_back({"统计页现在是设备首页。"});
            AppendWifiStatusBlocks(context, &model.split_view.detail_blocks);
            break;
    }

    AppendDeviceInfoBlocks(context, &model.split_view.detail_blocks);
    return model;
}
