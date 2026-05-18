#ifndef QUELLOG_SETTINGS_WEB_SERVER_H_
#define QUELLOG_SETTINGS_WEB_SERVER_H_

#include <esp_http_server.h>

class SettingsWebServer {
public:
    SettingsWebServer() = default;
    ~SettingsWebServer();

    SettingsWebServer(const SettingsWebServer&) = delete;
    SettingsWebServer& operator=(const SettingsWebServer&) = delete;

    bool Start();
    void Stop();
    bool IsRunning() const { return server_ != nullptr; }

private:
    httpd_handle_t server_ = nullptr;
};

#endif  // QUELLOG_SETTINGS_WEB_SERVER_H_
