#include "board.h"

#include <esp_chip_info.h>
#include <esp_log.h>
#include <esp_random.h>
#include <sdkconfig.h>

#include <cstdio>

#include "display/display.h"
#include "settings.h"

Board::Board() {
    Settings settings("board", true);
    uuid_ = settings.GetString("uuid");
    if (uuid_.empty()) {
        uuid_ = GenerateUuid();
        settings.SetString("uuid", uuid_);
    }
}

std::string Board::GenerateUuid() {
    uint8_t uuid[16] = {};
    esp_fill_random(uuid, sizeof(uuid));
    uuid[6] = (uuid[6] & 0x0F) | 0x40;
    uuid[8] = (uuid[8] & 0x3F) | 0x80;

    char text[37];
    snprintf(text, sizeof(text),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             uuid[0], uuid[1], uuid[2], uuid[3], uuid[4], uuid[5], uuid[6], uuid[7],
             uuid[8], uuid[9], uuid[10], uuid[11], uuid[12], uuid[13], uuid[14], uuid[15]);
    return std::string(text);
}

Display* Board::GetDisplay() {
    static NoDisplay display;
    return &display;
}

std::string Board::GetCpuInfo() const {
    esp_chip_info_t chip_info = {};
    esp_chip_info(&chip_info);

    return std::string(CONFIG_IDF_TARGET) + "  " + std::to_string(chip_info.cores) + "核";
}
