#include "settings_page.h"

#include <string>

namespace {

std::string FormatStorageSize(uint32_t size_kb) {
    if (size_kb < 1024) {
        return std::to_string(size_kb) + "KB";
    }

    const uint64_t rounded_tenths = ((static_cast<uint64_t>(size_kb) * 10U) + 512U) / 1024U;
    const uint64_t whole = rounded_tenths / 10U;
    const uint64_t fraction = rounded_tenths % 10U;
    if (fraction == 0) {
        return std::to_string(whole) + "MB";
    }
    return std::to_string(whole) + "." + std::to_string(fraction) + "MB";
}

std::string FormatStorageValue(uint32_t used_kb, uint32_t total_kb) {
    if (total_kb == 0) {
        return "暂不可用";
    }
    const int percent = static_cast<int>((static_cast<uint64_t>(used_kb) * 100U) / total_kb);
    return FormatStorageSize(used_kb) + "/" + FormatStorageSize(total_kb) + "  " + std::to_string(percent) + "%";
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

    blocks->push_back({"音量  " + std::to_string(context.volume_percent) + "%"});
    blocks->push_back({context.volume_percent == 0 ? "当前为静音。" : "确认键每次增加 10%。"});
}

void AppendStorageSections(const AppContext& context, SplitViewModel* split_view) {
    if (split_view == nullptr) {
        return;
    }

    SplitViewDetailSection storage_section;
    storage_section.title = "存储";
    if (!context.storage.available) {
        storage_section.items.push_back({"状态", "暂不可用"});
        split_view->detail_sections.push_back(storage_section);
        return;
    }

    storage_section.items.push_back({"总容量", FormatStorageSize(context.storage.flash_total_kb)});
    split_view->detail_sections.push_back(storage_section);

    split_view->detail_sections.push_back({
        "分区",
        {
            {"App", FormatStorageValue(context.storage.app_used_kb, context.storage.app_total_kb)},
            {"NVS", FormatStorageValue(context.storage.nvs_used_kb, context.storage.nvs_total_kb)},
        },
    });
}

void AppendDeviceInfoSections(const AppContext& context, SplitViewModel* split_view) {
    if (split_view == nullptr) {
        return;
    }

    split_view->detail_sections.push_back({
        "设备",
        {
            {"设备名称", context.device_alias},
            {"型号", context.board_type},
            {"CPU", context.cpu_info},
        },
    });

    SplitViewDetailSection battery_section;
    battery_section.title = "电池";
    if (context.battery_known) {
        battery_section.items.push_back({"电量", std::to_string(context.battery_level) + "%"});
    } else {
        battery_section.items.push_back({"电量", "暂不可用"});
    }
    if (context.battery_capacity_mah_known) {
        battery_section.items.push_back({"容量", std::to_string(context.battery_capacity_mah) + "mAh"});
    }
    split_view->detail_sections.push_back(battery_section);
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
            AppendStorageSections(context, &model.split_view);
            break;
        case 4:
            AppendDeviceInfoSections(context, &model.split_view);
            break;
        default:
            AppendWifiStatusBlocks(context, &model.split_view.detail_blocks);
            break;
    }

    return model;
}
