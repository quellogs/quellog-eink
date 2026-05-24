#ifndef QUELLOG_BOARD_H_
#define QUELLOG_BOARD_H_

#include <functional>
#include <cstdint>
#include <string>
#include <vector>

class Display;

enum class InputKey {
    None = 0,
    Up,
    Down,
    Confirm,
    OpenSettings
};

struct InputEvent {
    InputKey key = InputKey::None;
    bool long_press = false;
};

struct BoardStorageInfo {
    bool available = false;
    uint32_t flash_total_kb = 0;
    uint32_t app_total_kb = 0;
    uint32_t app_used_kb = 0;
    uint32_t nvs_total_kb = 0;
    uint32_t nvs_used_kb = 0;
};

struct BoardWifiNetwork {
    std::string ssid;
    int rssi = 0;
    bool secure = true;
};

struct BoardWifiConnectionInfo {
    std::string ssid;
    std::string ip_address;
    std::string netmask;
    std::string gateway;
    std::string dns_main;
    std::string dns_backup;
};

enum class NetworkState {
    Unknown = 0,
    Disconnected,
    Scanning,
    Connecting,
    Connected,
    ConfigMode,
};

enum class NetworkEvent {
    Scanning = 0,
    Connecting,
    Connected,
    Disconnected,
    WifiConfigModeEnter,
    WifiConfigModeExit,
};

using NetworkEventCallback = std::function<void(NetworkEvent, const std::string&)>;

void* create_board();

class Board {
public:
    static Board& GetInstance() {
        static Board* instance = static_cast<Board*>(create_board());
        return *instance;
    }

    Board(const Board&) = delete;
    Board& operator=(const Board&) = delete;
    virtual ~Board() = default;

    virtual std::string GetBoardType() = 0;
    virtual std::string GetUuid() const { return uuid_; }
    virtual std::string GetCpuInfo() const;
    virtual Display* GetDisplay();
    virtual bool PollInput(InputEvent& event) = 0;
    virtual bool GetBatteryLevel(int& level) {
        bool charging = false;
        bool external_power = false;
        return GetBatteryLevel(level, charging, external_power);
    }
    virtual bool GetBatteryLevel(int& level, bool& charging, bool& external_power) {
        (void)level;
        (void)charging;
        (void)external_power;
        return false;
    }
    virtual bool GetBatteryCapacityMah(int& capacity_mah) const {
        (void)capacity_mah;
        return false;
    }
    virtual std::string GetSystemInfoJson();
    virtual void StartNetwork() {}
    virtual void StopNetwork() {}
    virtual bool IsWifiEnabled() const { return false; }
    virtual void EnterWifiConfigMode() {}
    virtual bool ConnectToOpenWifi(const std::string& ssid) {
        (void)ssid;
        return false;
    }
    virtual void PrepareWifiConfigForSsid(const std::string& ssid) { (void)ssid; }
    virtual std::vector<BoardWifiNetwork> GetScannedWifiNetworks() const { return {}; }
    virtual std::string GetPendingWifiConfigSsid() const { return ""; }
    virtual bool IsWifiConnected() const { return false; }
    virtual bool IsWifiConfigMode() const { return false; }
    virtual NetworkState GetNetworkState() const { return NetworkState::Unknown; }
    virtual std::string GetWifiSsid() const { return ""; }
    virtual std::string GetWifiIpAddress() const { return ""; }
    virtual BoardWifiConnectionInfo GetWifiConnectionInfo() const { return {}; }
    virtual std::string GetWifiConfigApSsid() const { return ""; }
    virtual std::string GetWifiConfigApUrl() const { return ""; }
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) { (void)callback; }
    virtual bool IsBluetoothAvailable() const { return false; }
    virtual bool IsBluetoothEnabled() const { return false; }
    virtual bool SetBluetoothEnabled(bool enabled) {
        (void)enabled;
        return false;
    }
    virtual int GetVolumePercent() const { return 0; }
    virtual void SetVolumePercent(int percent) { (void)percent; }
    virtual BoardStorageInfo GetStorageInfo() const { return {}; }

protected:
    Board();
    std::string GenerateUuid();

private:
    std::string uuid_;
};

#define DECLARE_BOARD(BOARD_CLASS_NAME) \
void* create_board() { \
    return new BOARD_CLASS_NAME(); \
}

#endif  // QUELLOG_BOARD_H_
