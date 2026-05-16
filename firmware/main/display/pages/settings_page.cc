#include "settings_page.h"

#include <string>

namespace {

std::string FormatStorageLine(const std::string& label, uint32_t used_kb, uint32_t total_kb) {
    if (total_kb == 0) {
        return label + "  暂不可用";
    }
    const int percent = static_cast<int>((static_cast<uint64_t>(used_kb) * 100U) / total_kb);
    return label + "  " + std::to_string(used_kb) + "/" + std::to_string(total_kb) + "KB  " +
           std::to_string(percent) + "%";
}

void AppendWifiStatusBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({context.wifi_enabled ? "WiFi 开关  已开启" : "WiFi 开关  已关闭"});
    if (!context.wifi_enabled) {
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

void AppendBluetoothBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({"蓝牙"});
    if (!context.bluetooth_available) {
        blocks->push_back({"状态  不可用"});
        blocks->push_back({"当前固件未启用蓝牙组件。"});
        return;
    }
    blocks->push_back({context.bluetooth_enabled ? "状态  已开启" : "状态  已关闭"});
    blocks->push_back({"确认键可切换蓝牙开关。"});
}

void AppendSoundBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({"声音"});
    blocks->push_back({"音量  " + std::to_string(context.volume_percent) + "%"});
    blocks->push_back({context.volume_percent == 0 ? "当前为静音。" : "确认键每次增加 10%。"});
}

void AppendStorageBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({"存储空间"});
    if (!context.storage.available) {
        blocks->push_back({"空间信息暂不可用。"});
        return;
    }
    blocks->push_back({"Flash 总量  " + std::to_string(context.storage.flash_total_kb) + "KB"});
    blocks->push_back({FormatStorageLine("App 分区", context.storage.app_used_kb, context.storage.app_total_kb)});
    blocks->push_back({FormatStorageLine("NVS 分区", context.storage.nvs_used_kb, context.storage.nvs_total_kb)});
}

void AppendDeviceInfoBlocks(const AppContext& context, std::vector<TextBlockModel>* blocks) {
    if (blocks == nullptr) {
        return;
    }

    blocks->push_back({"设备信息"});
    blocks->push_back({"设备别名  " + context.device_alias});
    blocks->push_back({"板型  " + context.board_type});
    blocks->push_back({"UUID  " + context.device_uuid});
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
        {"无线网络", context.settings_selected_item == 0, SplitViewMenuIcon::Wifi},
        {"蓝牙", context.settings_selected_item == 1, SplitViewMenuIcon::Bluetooth},
        {"声音", context.settings_selected_item == 2, SplitViewMenuIcon::Sound},
        {"存储空间", context.settings_selected_item == 3, SplitViewMenuIcon::Storage},
        {"设备信息", context.settings_selected_item == 4, SplitViewMenuIcon::Device},
    };

    switch (context.settings_selected_item) {
        case 0:
            model.split_view.detail_blocks.push_back({"无线网络"});
            model.split_view.detail_blocks.push_back({"确认键可切换 WiFi 开关。"});
            AppendWifiStatusBlocks(context, &model.split_view.detail_blocks);
            break;
        case 1:
            AppendBluetoothBlocks(context, &model.split_view.detail_blocks);
            break;
        case 2:
            AppendSoundBlocks(context, &model.split_view.detail_blocks);
            break;
        case 3:
            AppendStorageBlocks(context, &model.split_view.detail_blocks);
            break;
        case 4:
            AppendDeviceInfoBlocks(context, &model.split_view.detail_blocks);
            break;
        default:
            AppendWifiStatusBlocks(context, &model.split_view.detail_blocks);
            break;
    }

    return model;
}
