#pragma once
#include "mesp/wsValue.hpp"


namespace topams {

    using mesp::ws_nvs_value;
    using mesp::wsValue;
    using mstd::Exstring;


    struct h_bridge_motor {
        gpio_num_t in1 = GPIO_NUM_2;
        gpio_num_t in2 = GPIO_NUM_3;
    };

    inline h_bridge_motor main_motor{GPIO_NUM_2, GPIO_NUM_3};


    struct channel {
        int32_t id = 0;
        gpio_num_t solenoid = GPIO_NUM_NC;

        ws_nvs_value<Exstring<24>> name{"ext" + Exstring<3>(id) + "_name", "PETG"};
        ws_nvs_value<Exstring<8>> color{"ext" + Exstring<3>(id) + "_color", "#ffffff"};
        ws_nvs_value<int32_t> next_channel{"ext" + Exstring<3>(id) + "_next", id};

        ws_nvs_value<int32_t> temper{"ext" + Exstring<3>(id) + "_temper", 250};
        ws_nvs_value<int32_t> load_time{"ext" + Exstring<3>(id) + "_load", 6000};
        ws_nvs_value<int32_t> uload_time{"ext" + Exstring<3>(id) + "_uload", 5000};

        channel() = default;
        channel(int32_t i, gpio_num_t s = GPIO_NUM_NC)
            : id(i),
              solenoid(s),
              name("ext" + Exstring<3>(id) + "_name", "PETG"),
              color("ext" + Exstring<3>(id) + "_color", "#ffffff"),
              next_channel("ext" + Exstring<3>(id) + "_next", id),
              temper("ext" + Exstring<3>(id) + "_temper", 250),
              load_time("ext" + Exstring<3>(id) + "_load", 6000),
              uload_time("ext" + Exstring<3>(id) + "_uload", 5000) {}
    };

    inline constexpr int32_t MAX_CHANNELS = 4;

    inline std::array<channel, MAX_CHANNELS>
        channels{{
            {1, GPIO_NUM_12},
            {2, GPIO_NUM_18},
            {3, GPIO_NUM_19},
            {4, GPIO_NUM_13}
        }};

    inline gpio_num_t estop_button = GPIO_NUM_7;

    inline std::atomic<int32_t> active_channel{0};
    inline std::atomic<bool> emergency_stop{false};

    struct mqtt_config {
        inline static mesp::ws_nvs_value<Exstring<16>> bambu_ip{"bambu_ip", ""};
        inline static ws_nvs_value<Exstring<16>> device_serial{"device_serial", ""};
        inline static ws_nvs_value<Exstring<9>> MQTT_pass{"MQTT_pass", ""};

        static auto server_ip() {
            return "mqtts://" + bambu_ip.get_value() + ":8883";
        }
        static auto client() {
            return Exstring<5>("bblp");
        }
        static auto passward() {
            return MQTT_pass.get_value();
        }
        static auto topic_subscribe() {
            return "device/" + device_serial.get_value() + "/report";
        }
        static auto topic_publish() {
            return "device/" + device_serial.get_value() + "/request";
        }
    };
}