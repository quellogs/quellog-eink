#ifndef QUELLOG_BOARD_H_
#define QUELLOG_BOARD_H_

#include <functional>
#include <string>

class Display;

enum class InputKey {
    None = 0,
    Up,
    Down,
    Confirm
};

struct InputEvent {
    InputKey key = InputKey::None;
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
    virtual std::string GetSystemInfoJson();
    virtual void StartNetwork() {}
    virtual void EnterWifiConfigMode() {}
    virtual bool IsWifiConnected() const { return false; }
    virtual bool IsWifiConfigMode() const { return false; }
    virtual NetworkState GetNetworkState() const { return NetworkState::Unknown; }
    virtual std::string GetWifiSsid() const { return ""; }
    virtual std::string GetWifiIpAddress() const { return ""; }
    virtual std::string GetWifiConfigApSsid() const { return ""; }
    virtual std::string GetWifiConfigApUrl() const { return ""; }
    virtual void SetNetworkEventCallback(NetworkEventCallback callback) { (void)callback; }

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
