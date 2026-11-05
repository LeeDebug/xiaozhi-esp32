#pragma once

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <material_symbols.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <atomic>

#include "application.h"
#include "assets/lang_config.h"
#include "audio_codec.h"
#include "display.h"
#include "ethernet_board.h"
#ifdef CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
#include "blufi.h"
#endif

// This board's network policy is owned by one worker. Driver callbacks only publish
// readiness; application state changes are dispatched to the application task.
class EthernetWifiBoard : public EthernetBoard {
    enum class Uplink { None, Ethernet, Wifi };
    std::atomic<bool> ethernet_ready_{false};
    std::atomic<bool> config_requested_{false};
    std::atomic<Uplink> uplink_{Uplink::None};
    TaskHandle_t network_task_ = nullptr;

    void Notify(NetworkEvent event, const std::string& data = "") {
        Application::GetInstance().Schedule([this, event, data]() {
            if (network_event_callback_) {
                network_event_callback_(event, data);
            }
        });
    }

    void StartProvisioning() {
        auto& wifi = WifiManager::GetInstance();
        wifi.StopStation();
        Application::GetInstance().Schedule([]() {
            auto& app = Application::GetInstance();
            const auto state = app.GetDeviceState();
            if (state == kDeviceStateConnecting || state == kDeviceStateListening ||
                state == kDeviceStateSpeaking) {
                app.SetDeviceState(kDeviceStateIdle);
            }
            app.ResetProtocol();
            app.SetDeviceState(kDeviceStateWifiConfiguring);
        });
#ifdef CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
        wifi.StartConfigAp();
        std::string hint = Lang::Strings::CONNECT_TO_HOTSPOT;
        hint += wifi.GetApSsid();
        hint += Lang::Strings::ACCESS_VIA_BROWSER;
        hint += wifi.GetApWebUrl();
        Application::GetInstance().Schedule([hint]() {
            Application::GetInstance().Alert(Lang::Strings::WIFI_CONFIG_MODE, hint.c_str(), "gear",
                                             Lang::Sounds::OGG_WIFICONFIG);
        });
#elif CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
        Blufi::GetInstance().init();
#endif
        Notify(NetworkEvent::WifiConfigModeEnter);
    }

    void RunNetwork() {
        const int64_t ethernet_deadline = esp_timer_get_time() + 10 * 1000000LL;
        EthernetBoard::StartNetwork();
        WifiManagerConfig config;
        config.ssid_prefix = "Xiaozhi";
        config.language = Lang::CODE;
        config.show_ota_config = true;
        config.show_sleep_config = true;
        auto& wifi = WifiManager::GetInstance();
        const auto mac = wifi.GetMacAddress();
        if (mac.size() == 17) {
            config.station_hostname = "Xiaozhi-" + mac.substr(12, 2) + mac.substr(15, 2);
        }
        const bool wifi_initialized = wifi.Initialize(config);
        bool station_started = false;
        bool provisioning = false;
        bool ethernet_wait_done = false;
        int64_t wifi_deadline = 0;
        Uplink announced = Uplink::None;

        while (true) {
            const auto now = esp_timer_get_time();
            // Check the netif as well as the link/IP events: DHCP lease loss must
            // also make Ethernet unavailable, even when the cable stays plugged in.
            auto* eth = esp_netif_get_handle_from_ifkey("ETH_DEF");
            esp_netif_ip_info_t ip = {};
            const bool ethernet = ethernet_ready_.load() && eth && esp_netif_is_netif_up(eth) &&
                                  esp_netif_get_ip_info(eth, &ip) == ESP_OK && ip.ip.addr != 0;
            if (!ethernet_wait_done && !ethernet && now >= ethernet_deadline) {
                ESP_LOGW("EthernetWifi", "Ethernet not ready after 10 seconds, using WiFi");
            }
            ethernet_wait_done = ethernet_wait_done || ethernet || now >= ethernet_deadline;
            const bool have_ssid = !SsidManager::GetInstance().GetSsidList().empty();

            if (wifi_initialized && !provisioning && !station_started && have_ssid) {
                wifi.StartStation();
                station_started = true;
            }
            bool wifi_ready = wifi_initialized && wifi.IsConnected();
            if (provisioning && (wifi_ready || !wifi.IsConfigMode())) {
#ifdef CONFIG_USE_HOTSPOT_WIFI_PROVISIONING
                provisioning = false;
                station_started = wifi_ready;
                wifi_deadline = 0;
                Notify(NetworkEvent::WifiConfigModeExit);
#elif CONFIG_USE_ESP_BLUFI_WIFI_PROVISIONING
                if (wifi_ready) {
                    Blufi::GetInstance().deinit();
                    provisioning = false;
                    station_started = true;
                    Notify(NetworkEvent::WifiConfigModeExit);
                }
#endif
            }

            // The Wi-Fi timeout only runs when Wi-Fi is needed as the primary.
            // Failure of a backup connection must never interrupt working Ethernet.
            if (ethernet || wifi_ready || !ethernet_wait_done || provisioning) {
                wifi_deadline = 0;
            } else if (wifi_deadline == 0) {
                wifi_deadline = now + 60 * 1000000LL;
            }
            const bool requested = config_requested_.exchange(false);
            if (wifi_initialized && !provisioning &&
                (requested || (ethernet_wait_done && !ethernet && !wifi_ready &&
                               (!have_ssid || now >= wifi_deadline)))) {
                StartProvisioning();
                provisioning = true;
                station_started = false;
                wifi_ready = false;
            }

            // Keep a running portal until credentials are submitted, even if a
            // cable is plugged in during provisioning.
            Uplink next =
                provisioning
                    ? Uplink::None
                    : (ethernet ? Uplink::Ethernet
                                : (ethernet_wait_done && wifi_ready ? Uplink::Wifi : Uplink::None));
            if (next != announced) {
                if (announced != Uplink::None) {
                    Notify(NetworkEvent::Disconnected);
                    announced = Uplink::None;
                    uplink_ = Uplink::None;
                    // Deliver disconnect and connect on separate application turns.
                    vTaskDelay(pdMS_TO_TICKS(100));
                    continue;
                }
                auto* netif = next == Uplink::Ethernet
                                  ? eth
                                  : esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
                if (netif && esp_netif_set_default_netif(netif) == ESP_OK) {
                    announced = next;
                    uplink_ = next;
                    ESP_LOGI("EthernetWifi", "Primary network: %s",
                             next == Uplink::Ethernet ? "Ethernet" : "WiFi");
                    Notify(NetworkEvent::Connected,
                           next == Uplink::Ethernet ? "Ethernet" : wifi.GetSsid());
                }
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

protected:
    void OnNetworkEvent(NetworkEvent event, const std::string& data = "") override {
        if (event == NetworkEvent::Connected) {
            ethernet_ready_ = true;
        } else if (event == NetworkEvent::Disconnected) {
            ethernet_ready_ = false;
        }
    }

public:
    void StartNetwork() override {
        if (network_task_) {
            return;
        }
        const auto err = esp_event_loop_create_default();
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
            ESP_ERROR_CHECK(err);
        }
        ESP_ERROR_CHECK(esp_event_handler_register(
            IP_EVENT, IP_EVENT_ETH_LOST_IP,
            [](void* arg, esp_event_base_t, int32_t, void*) {
                static_cast<EthernetWifiBoard*>(arg)->ethernet_ready_ = false;
            },
            this));
        if (xTaskCreate([](void* arg) { static_cast<EthernetWifiBoard*>(arg)->RunNetwork(); },
                        "eth_wifi", 6144, this, 5, &network_task_) != pdPASS) {
            ESP_LOGE("EthernetWifi", "Failed to create network worker");
        }
    }

    void EnterWifiConfigMode() { config_requested_ = true; }

    std::string GetBoardJson() override {
        auto* root = cJSON_CreateObject();
        cJSON_AddStringToObject(root, "type", BOARD_TYPE);
        cJSON_AddStringToObject(root, "name", BOARD_NAME);
        cJSON_AddStringToObject(root, "manufacturer", BOARD_MANUFACTURER);
        const bool ethernet = uplink_ == Uplink::Ethernet;
        cJSON_AddStringToObject(root, "network", ethernet ? "ethernet" : "wifi");
        auto& wifi = WifiManager::GetInstance();
        cJSON_AddStringToObject(
            root, "mac", (ethernet ? GetEthernetMacAddress() : wifi.GetMacAddress()).c_str());
        auto* netif = esp_netif_get_handle_from_ifkey(ethernet ? "ETH_DEF" : "WIFI_STA_DEF");
        esp_netif_ip_info_t ip = {};
        if (netif && esp_netif_get_ip_info(netif, &ip) == ESP_OK) {
            char address[16];
            snprintf(address, sizeof(address), IPSTR, IP2STR(&ip.ip));
            cJSON_AddStringToObject(root, "ip", address);
        }
        if (!ethernet) {
            cJSON_AddStringToObject(root, "ssid", wifi.GetSsid().c_str());
            cJSON_AddNumberToObject(root, "rssi", wifi.GetRssi());
        }
        char* text = cJSON_PrintUnformatted(root);
        std::string result = text ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        return result;
    }

    std::string GetDeviceStatusJson() override {
        auto* root = cJSON_CreateObject();
        auto* speaker = cJSON_AddObjectToObject(root, "audio_speaker");
        if (auto* codec = GetAudioCodec()) {
            cJSON_AddNumberToObject(speaker, "volume", codec->output_volume());
        }
        auto* screen = cJSON_AddObjectToObject(root, "screen");
        if (auto* backlight = GetBacklight()) {
            cJSON_AddNumberToObject(screen, "brightness", backlight->brightness());
        }
        if (auto* display = GetDisplay(); display && display->height() > 64) {
            if (auto* theme = display->GetTheme()) {
                cJSON_AddStringToObject(screen, "theme", theme->name().c_str());
            }
        }
        int level = 0;
        bool charging = false;
        bool discharging = false;
        if (GetBatteryLevel(level, charging, discharging)) {
            auto* battery = cJSON_AddObjectToObject(root, "battery");
            cJSON_AddNumberToObject(battery, "level", level);
            cJSON_AddBoolToObject(battery, "charging", charging);
        }
        float temperature = 0;
        if (GetTemperature(temperature)) {
            auto* chip = cJSON_AddObjectToObject(root, "chip");
            cJSON_AddNumberToObject(chip, "temperature", temperature);
        }
        auto* network = cJSON_Parse(GetBoardJson().c_str());
        if (network) {
            cJSON_AddStringToObject(network, "state",
                                    uplink_ == Uplink::None ? "disconnected" : "connected");
            cJSON_ReplaceItemInObject(
                network, "type",
                cJSON_CreateString(uplink_ == Uplink::Ethernet ? "ethernet" : "wifi"));
            cJSON_AddItemToObject(root, "network", network);
        }
        char* text = cJSON_PrintUnformatted(root);
        std::string result = text ? text : "{}";
        cJSON_free(text);
        cJSON_Delete(root);
        return result;
    }

    const char* GetNetworkStateIcon() override {
        if (uplink_ == Uplink::Ethernet) {
            return MATERIAL_SYMBOLS_ANDROID_CELL_4_BAR;
        }
        return uplink_ == Uplink::Wifi ? MATERIAL_SYMBOLS_WIFI : MATERIAL_SYMBOLS_WIFI_OFF;
    }

    void SetPowerSaveLevel(PowerSaveLevel level) override {
        WifiManager::GetInstance().SetPowerSaveLevel(
            level == PowerSaveLevel::LOW_POWER  ? WifiPowerSaveLevel::LOW_POWER
            : level == PowerSaveLevel::BALANCED ? WifiPowerSaveLevel::BALANCED
                                                : WifiPowerSaveLevel::PERFORMANCE);
    }
};
