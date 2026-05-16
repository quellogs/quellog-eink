#include "settings_page.h"

PageModel SettingsPage::BuildModel(const AppContext& context) const {
    const auto selected_prefix = [context](int index) {
        return context.settings_selected_item == index ? "> " : "  ";
    };

    PageModel model;
    model.text_blocks.push_back({std::string(selected_prefix(0)) + "刷新模式  " +
                                 std::string(context.refresh_policy == RefreshPolicy::Timed ? "定时" : "手动")});
    model.text_blocks.push_back({std::string(selected_prefix(1)) + "重新配网"});
    model.text_blocks.push_back({std::string(selected_prefix(2)) + "返回首页"});
    model.text_blocks.push_back({""});

    if (context.wifi_config_mode) {
        model.text_blocks.push_back({"WiFi  配网模式"});
        model.text_blocks.push_back({"热点  " + context.wifi_ap_ssid});
        model.text_blocks.push_back({"地址  " + context.wifi_ap_url});
    } else if (context.wifi_connected) {
        model.text_blocks.push_back({"WiFi  已连接"});
        model.text_blocks.push_back({"网络  " + context.wifi_ssid});
        model.text_blocks.push_back({"IP  " + context.wifi_ip});
    } else if (context.wifi_connecting) {
        model.text_blocks.push_back({"WiFi  连接中..."});
    } else {
        model.text_blocks.push_back({"WiFi  未连接"});
    }

    model.text_blocks.push_back({"设备别名  " + context.device_alias});
    model.text_blocks.push_back({"板型  " + context.board_type});
    if (context.battery_known) {
        model.text_blocks.push_back({"电量  " + std::to_string(context.battery_level) + "%"});
        if (context.battery_charging) {
            model.text_blocks.push_back({"电池状态  充电中"});
        }
    } else {
        model.text_blocks.push_back({"电量暂不可用"});
    }
    return model;
}
