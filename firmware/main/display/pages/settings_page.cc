#include "settings_page.h"

#include <algorithm>
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

std::string EscapeWifiQrField(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        if (ch == '\\' || ch == ';' || ch == ',' || ch == ':' || ch == '"') {
            escaped.push_back('\\');
        }
        escaped.push_back(ch);
    }
    return escaped;
}

std::string BuildOpenWifiQrPayload(const std::string& ssid) {
    return "WIFI:T:nopass;S:" + EscapeWifiQrField(ssid) + ";;";
}

std::string BuildBluetoothDeviceName(const std::string& uuid) {
    std::string compact_uuid;
    compact_uuid.reserve(uuid.size());
    for (char ch : uuid) {
        if (ch != '-') {
            compact_uuid.push_back(ch);
        }
    }

    const size_t suffix_length = std::min<size_t>(6, compact_uuid.size());
    return "Quellog-" + compact_uuid.substr(compact_uuid.size() - suffix_length);
}

void AppendWifiSettingsDetail(const AppContext& context, SplitViewModel* split_view) {
    if (split_view == nullptr) {
        return;
    }

    split_view->wifi_switch_visible = true;
    split_view->wifi_switch_label = "无线网络";
    split_view->wifi_switch_on = context.wifi_enabled;
    split_view->wifi_switch_focused = context.settings_wifi_focus_index == 0;

    if (context.wifi_config_mode && !context.settings_wifi_ap_modal_visible) {
        split_view->detail_blocks.push_back({"AP 配网中，手机扫码继续连接。"});
        return;
    }

    if (!context.wifi_enabled) {
        split_view->detail_blocks.push_back({"开启后将扫描附近网络。"});
        return;
    }

    if (context.wifi_connected) {
        split_view->detail_blocks.push_back({"已连接  " + context.wifi_ssid});
        if (!context.wifi_ip.empty()) {
            split_view->detail_blocks.push_back({"IP  " + context.wifi_ip});
        }
    } else if (context.wifi_connecting) {
        split_view->detail_blocks.push_back({"正在连接..."});
    } else {
        split_view->detail_blocks.push_back({"选择网络连接或配网。"});
    }

    for (size_t index = 0; index < context.wifi_networks.size(); ++index) {
        const WifiNetworkInfo& network = context.wifi_networks[index];
        split_view->wifi_items.push_back({
            network.ssid,
            network.rssi,
            network.secure,
            context.settings_wifi_focus_index == static_cast<int>(index) + 1,
        });
    }
}

void AppendWifiApModal(const AppContext& context, ModalModel* modal) {
    if (modal == nullptr || !context.settings_wifi_ap_modal_visible) {
        return;
    }
    modal->visible = true;
    modal->title = "AP 配网";
    modal->message = "手机扫码打开配网页";
    if (!context.wifi_ap_ssid.empty()) {
        modal->options.push_back({"热点  " + context.wifi_ap_ssid, true, false});
        modal->options.push_back({
            "地址  " + (context.wifi_ap_url.empty() ? std::string("192.168.4.1") : context.wifi_ap_url),
            true,
            false,
        });
        modal->qr_payload = BuildOpenWifiQrPayload(context.wifi_ap_ssid);
    }
}

void AppendWifiConnectingModal(const AppContext& context, ModalModel* modal) {
    if (modal == nullptr || !context.settings_wifi_connecting_modal_visible) {
        return;
    }

    modal->visible = true;
    modal->title = "正在连接 Wi-Fi";
    if (!context.pending_wifi_config_ssid.empty()) {
        modal->message = "正在连接  " + context.pending_wifi_config_ssid;
    } else if (!context.wifi_ssid.empty()) {
        modal->message = "正在连接  " + context.wifi_ssid;
    } else {
        modal->message = "正在切换到家庭网络";
    }
    modal->options.push_back({"请稍候，连接成功后自动关闭", true, false});
}

void AppendBluetoothSettingsDetail(const AppContext& context, SplitViewModel* split_view) {
    if (split_view == nullptr) {
        return;
    }

    if (!context.bluetooth_available) {
        split_view->detail_blocks.push_back({"状态  不可用"});
        split_view->detail_blocks.push_back({"当前固件未启用蓝牙组件。"});
        return;
    }

    split_view->wifi_switch_visible = true;
    split_view->wifi_switch_label = "蓝牙";
    split_view->wifi_switch_on = context.bluetooth_enabled;
    split_view->wifi_switch_focused = true;

    if (!context.bluetooth_enabled) {
        split_view->detail_blocks.push_back({"开启后设备将通过 BLE 广播。"});
        return;
    }

    split_view->detail_sections.push_back({
        "蓝牙信息",
        {
            {"名称", BuildBluetoothDeviceName(context.device_uuid)},
            {"状态", "已开启，可被扫描发现"},
            {"模式", "BLE"},
        },
    });
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
            {"名称", context.device_alias},
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
            AppendWifiSettingsDetail(context, &model.split_view);
            AppendWifiApModal(context, &model.modal);
            AppendWifiConnectingModal(context, &model.modal);
            break;
        case 1:
            AppendBluetoothSettingsDetail(context, &model.split_view);
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
            AppendWifiSettingsDetail(context, &model.split_view);
            break;
    }

    return model;
}
