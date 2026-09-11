#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>
#include <HTTPClient.h>
#include <Update.h>

// #undef F

#include "bambu.hpp"
#include "esptools.hpp"
#include "mesp/channel.hpp"
#include "mesp/espIO.hpp"
#include "mesp/mqtt.hpp"
#include "mesp/smartWIFI.hpp"
#include "mesp/wsValue.hpp"
#include "topams_config.hpp"

namespace mstd {
    template <typename T>
    void atomic_wait_un(std::atomic<T>& value, T target) {//@_@可以考虑加入mstd
        auto old_value = value.load();
        while (old_value != target) {
            value.wait(old_value);
            old_value = value;
        }
    }
}//mstd

#include "index.hpp"

using mesp::Mqttclient;
using mesp::webfpr;
using mesp::webfpr_lager;
using mstd::Exstring;
using mstd::fpr;

//重启按钮动作
inline mstd::call_once restart_command(
    []() {
        mesp::command_emplace<+[]() {
            ESP.restart();
        }>("restart");
    });



inline void state_event(const Exstring<128>& msg) {}

using async_work_type = std::function<void()>;
inline mesp::channel_lock<async_work_type> async_channel;

inline bool ota_in_progress = false;

    inline void ota_perform_from_stream(Stream& stream, size_t total_size) {
        ota_in_progress = true;
        bool success = false;
        size_t written = 0;
        size_t last_progress = 0;

        webfpr("开始OTA更新,总大小:" + Exstring(total_size));

        if (!Update.begin(total_size, U_FLASH)) {
            webfpr("OTA begin失败,错误:" + Exstring(Update.getError()));
            Update.end(false);
            ota_in_progress = false;
            return;
        }

        uint8_t buf[4096];
        size_t remaining = total_size;

        while (remaining > 0) {
            size_t to_read = sizeof(buf);
            if (to_read > remaining) to_read = remaining;

            int got = stream.readBytes(buf, to_read);
            if (got <= 0) {
                webfpr("OTA读取数据中断");
                break;
            }

            size_t wrote = Update.write(buf, got);
            if (wrote != got) {
                webfpr("OTA写入失败,已写:" + Exstring(wrote) + "期望:" + Exstring(got) + "错误:" + Exstring(Update.getError()));
                break;
            }

            written += wrote;
            remaining -= wrote;

            size_t progress = (written * 100) / total_size;
            if (progress >= last_progress + 10 || progress == 100) {
                last_progress = progress;
                webfpr("OTA进度:" + Exstring(progress) + "% (" + Exstring(written) + "/" + Exstring(total_size) + ")");
            }
        }

        if (remaining == 0 && Update.end(true)) {
            webfpr("OTA写入成功,准备重启...");
            success = true;
        } else {
            webfpr("OTA失败,剩余:" + Exstring(remaining) + "错误:" + Exstring(Update.getError()));
            Update.end(false);
        }

        ota_in_progress = false;

        if (success) {
            mstd::delay(2s);
            ESP.restart();
        }
    }

    inline void ota_from_url(const Exstring<512>& url) {
        if (ota_in_progress) {
            webfpr("OTA正在进行中,请稍后再试");
            return;
        }

        if (WiFi.status() != WL_CONNECTED) {
            webfpr("WiFi未连接,无法下载固件");
            return;
        }

        webfpr("开始从URL下载固件:");
        webfpr(url);

        HTTPClient http;
        http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
        http.setRedirectLimit(10);

        if (!http.begin(url.c_str())) {
            webfpr("HTTP客户端初始化失败");
            return;
        }

        int httpCode = http.GET();
        if (httpCode != HTTP_CODE_OK) {
            webfpr("HTTP请求失败,状态码:" + Exstring(httpCode));
            http.end();
            return;
        }

        int total_size = http.getSize();
        if (total_size <= 0) {
            webfpr("无法获取固件大小,Content-Length缺失");
            http.end();
            return;
        }

        WiFiClient* stream = http.getStreamPtr();
        ota_perform_from_stream(*stream, (size_t)total_size);
        http.end();
    }

namespace topams {

    inline mesp::Mqttclient mqttclient;


    inline void release_all_solenoids() {
        for (auto& ch : channels) {
            if (ch.solenoid != GPIO_NUM_NC) {
                mesp::gpio_out(ch.solenoid, false);
            }
        }
        active_channel.store(0);
    }

    inline void stop_motor() {
        mesp::gpio_out(main_motor.in1, false);
        mesp::gpio_out(main_motor.in2, false);
    }

    inline void emergency_stop_all() {
        emergency_stop.store(true);
        stop_motor();
        release_all_solenoids();
        webfpr("紧急停止已触发!");
    }

    template <typename... T>
    inline void motor_run(int32_t channel_id, bool fwd, const T&... t) {
        if (emergency_stop.load()) {
            webfpr("紧急停止状态中,拒绝执行电机操作");
            return;
        }

        if (channel_id < 1 || channel_id > MAX_CHANNELS) {
            webfpr("通道编号错误:" + Exstring(channel_id));
            return;
        }
        int32_t ch_idx = channel_id - 1;

        const auto& ch_solenoid = channels[ch_idx].solenoid;
        const auto& load_time = channels[ch_idx].load_time.get_value();
        const auto& uload_time = channels[ch_idx].uload_time.get_value();

        if (ch_solenoid == GPIO_NUM_NC) {
            webfpr("通道" + Exstring(channel_id) + "未配置电磁离合IO");
            return;
        }

        release_all_solenoids();
        mstd::delay_ms(10);

        mesp::gpio_out(ch_solenoid, true);
        active_channel.store(channel_id);
        mstd::delay_ms(50);

        stop_motor();
        mstd::delay_ms(5);

        if (fwd) {
            mesp::gpio_out(main_motor.in1, true);
            mesp::gpio_out(main_motor.in2, false);
            webfpr("通道" + Exstring(channel_id) + "正转进料");
        } else {
            mesp::gpio_out(main_motor.in1, false);
            mesp::gpio_out(main_motor.in2, true);
            webfpr("通道" + Exstring(channel_id) + "反转退料");
        }

        bool interrupted = false;
        if constexpr (sizeof...(T) == 0) {
            auto total_ms = fwd ? load_time : uload_time;
            auto start = std::chrono::steady_clock::now();
            while (true) {
                if (emergency_stop.load()) {
                    interrupted = true;
                    break;
                }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= total_ms) break;
                mstd::delay_ms(10);
            }
        } else {
            auto duration = std::chrono::milliseconds(0);
            ((duration = std::chrono::duration_cast<std::chrono::milliseconds>(t)), ...);
            auto start = std::chrono::steady_clock::now();
            while (true) {
                if (emergency_stop.load()) {
                    interrupted = true;
                    break;
                }
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - start).count();
                if (elapsed >= duration.count()) break;
                mstd::delay_ms(10);
            }
        }

        stop_motor();
        mstd::delay_ms(20);

        release_all_solenoids();

        if (interrupted) {
            webfpr("操作被紧急停止中断");
        } else {
            webfpr("通道" + Exstring(channel_id) + (fwd ? "进料完成" : "退料完成"));
        }
    }


    inline volatile int32_t hw_switch = 0;//@_@应该是atomic
    inline mesp::ws_nvs_value<int32_t> extruder("extruder", 1);// 1-4, 0表示无耗材
    inline std::atomic<bool> pause_lock{false};// 暂停锁
    std::atomic<int32_t> ams_status = -1;
    std::atomic<int32_t> nozzle_target_temper = -1;



    void change_filament(const Mqttclient& client, int32_t old_extruder, int32_t new_extruder) {
        webfpr("开始换料");

        if (topams::channels[new_extruder - 1].load_time.get_value() > 0) {
            webfpr("使用固定时间进料");
            client.publish(bambu::msg::runGcode(
                "M109 S" + Exstring(topams::channels[old_extruder - 1].temper.get_value()) + "\nM620 S255\nT255\nM621 S255\n"));
            webfpr("发送了退料命令,等待退料完成");
            mstd::atomic_wait_un(ams_status, bambu::status::退料完成需要退线);
            webfpr("退料完成,需要退线,等待退线完");

            motor_run(old_extruder, false);

            mstd::atomic_wait_un(ams_status, bambu::status::退料完成);

            int32_t new_nozzle_temper = topams::channels[new_extruder - 1].temper.get_value();
            client.publish(bambu::msg::runGcode("M109 S" + Exstring(new_nozzle_temper)));
            while (nozzle_target_temper.load() < new_nozzle_temper - 5) {
                mstd::delay(500ms);
            }

            webfpr("进线");
            client.publish(bambu::msg::runGcode("G1 E150 F500"));
            mstd::delay(3s);
            motor_run(new_extruder, true);

            extruder.set_value(new_extruder);

            client.publish(bambu::msg::print_resume);
        } else {
            webfpr("小绿点判定进料");
            webfpr("还没写");
        }
    }


    void load_filament(int32_t new_extruder) {
        if (!mqttclient.is_connected()) {
            webfpr("MQTT未连接,无法上料");
            return;
        }

        if (new_extruder < 1 || new_extruder > MAX_CHANNELS) {
            webfpr("不支持的上料通道");
            return;
        }

        webfpr("开始进料");

        {
            mqttclient.publish(bambu::msg::get_status);
            mstd::delay(3s);
            if (hw_switch == 1) {
                int32_t old_extruder = extruder;
                if (old_extruder == 0) {
                    webfpr("请设置当前所使用通道,否则无法退料再进料");
                    return;
                }
                if (old_extruder == new_extruder) {
                    webfpr("当前通道已经是" + Exstring(new_extruder) + "无需上料");
                    return;
                }

                mqttclient.publish(bambu::msg::runGcode(
                    "M109 S" + Exstring(topams::channels[old_extruder - 1].temper.get_value()) + "\nM620 S255\nT255\nM621 S255\n"));
                webfpr("发送了退料命令,等待退料完成");
                mstd::atomic_wait_un(ams_status, bambu::status::退料完成需要退线);
                webfpr("退料完成,需要退线,等待退线完");

                motor_run(old_extruder, false);

                mstd::atomic_wait_un(ams_status, bambu::status::退料完成);
                webfpr("退线完成");
            }


            {
                int32_t new_nozzle_temper = topams::channels[new_extruder - 1].temper.get_value();
                mqttclient.publish(bambu::msg::runGcode("M109 S" + Exstring(new_nozzle_temper)));
                while (nozzle_target_temper.load() < new_nozzle_temper - 5) {
                    mstd::delay(500ms);
                }

                webfpr("进线");
                int32_t try_num = 0;
                do {
                    mqttclient.publish(bambu::msg::runGcode("G1 E150 F500"));
                    mstd::delay(3s);
                    motor_run(new_extruder, true);
                    mqttclient.publish(bambu::msg::get_status);
                    mstd::delay(3s);
                    if (hw_switch != 1)
                        webfpr("没检测到小绿点,尝试再次进料");
                    if (++try_num == 5) {
                        webfpr("尝试多次仍未检测到小绿点,进料失败,请重新检查参数");
                        return;
                    }
                } while (hw_switch != 1);


                extruder.set_value(new_extruder);


                mqttclient.publish(
                    bambu::msg::runGcode(
                        Exstring("G1 E100 F180\n")
                        + Exstring("M400\n") + Exstring("M106 P1 S255\n")
                        + Exstring("M400 S3\n")
                        + Exstring("G1 X -3.5 F18000\nG1 X -13.5 F3000\nG1 X -3.5 F18000\nG1 X -13.5 F3000\nG1 X -3.5 F18000\nG1 X -13.5 F3000\n")
                        + Exstring("M400\nM106 P1 S0\nM109 S90\n")));
            }
            webfpr("上料完成");
        }

        return;

    }


    //mqtt事件处理
    inline void data_event(const mesp::Mqttclient& client, const mesp::Mqttclient::data_cache_type& data) {

        {//json回传
            static mesp::ws_nvs_value<bool> printer_json{"printer_json", false};
            if (printer_json.get_value())
                webfpr_lager(data);
            Exstring start_time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            // webfpr(start_time);
        }//json回传


        static StaticJsonDocument<mesp::Mqttclient::data_cache_type::max_size() + 1024> doc;
        doc.clear();
        DeserializationError error = deserializeJson(doc, data.c_str());

        static int32_t bed_target_temper = -1;
        static int32_t bed_target_temper_max = 0;
        // static int32_t nozzle_target_temper = -1;

        bed_target_temper = doc["print"]["bed_target_temper"] | bed_target_temper;
        nozzle_target_temper.store(doc["print"]["nozzle_target_temper"] | nozzle_target_temper.load());
        mstd::Exstring<32> gcode_state = doc["print"]["gcode_state"] | "unkonw";
        hw_switch = doc["print"]["hw_switch_state"] | hw_switch;

        {

            if (bed_target_temper > 0 && bed_target_temper <= MAX_CHANNELS) {

                if (gcode_state == "PAUSE") {
                    if (bed_target_temper_max > 0) {
                        client.publish(
                            bambu::msg::runGcode("M190 S" + mstd::Exstring(bed_target_temper_max)));
                    }

                    int32_t old_extruder = extruder.get_value();
                    int32_t new_extruder = bed_target_temper;
                    if (old_extruder == 0) {
                        webfpr("当前通道未知或为首次换料,将目标通道记为当前通道,请手动进退料确保耗材正确,之后点击恢复继续打印");
                        extruder.set_value(new_extruder);
                        if (bed_target_temper_max > 0)
                            client.publish(bambu::msg::runGcode("M190 S" + mstd::Exstring(bed_target_temper_max)));
                    } else if (old_extruder != new_extruder) {
                        mstd::fpr("唤醒换料程序");
                        pause_lock = true;
                        async_channel.emplace([&client, old_extruder, new_extruder]() {
                            change_filament(client, old_extruder, new_extruder);
                        });
                    } else if (!pause_lock.load()) {
                        mstd::fpr("同一耗材,无需换料");
                        client.publish(bambu::msg::runGcode("M190 S" + mstd::Exstring(bed_target_temper_max)));
                        mstd::delay(1000ms);
                        client.publish(bambu::msg::print_resume);
                    }
                    if (bed_target_temper_max > 0)
                        bed_target_temper = bed_target_temper_max;

                }
            } else if (bed_target_temper == 0)
                bed_target_temper_max = 0;// 打印结束
            else
                bed_target_temper_max = std::max(bed_target_temper, bed_target_temper_max);// 不同材料可能底板温度不一样,这里选择维持最高的
        }

        int ams_status_now = doc["print"]["ams_status"] | -1;
        if (ams_status_now != -1) {
            fpr("asm_status_now:", ams_status_now);
            if (ams_status.exchange(ams_status_now) != ams_status_now)
                ams_status.notify_one();
        }

    }//date_event




    inline mesp::smartWIFI* g_wifi = nullptr;

    //注册command命令
    inline mstd::call_once register_command(
        []() {
            mesp::command_emplace<+[](int32_t motor_id) {
                motor_run(motor_id, true);
            }>("motor_forward");

            mesp::command_emplace<+[](int32_t motor_id) {
                motor_run(motor_id, false);
            }>("motor_backward");

            mesp::command_emplace<+[](int32_t extruder_id) {
                load_filament(extruder_id);
            }>("load_filament");

            mesp::command_emplace<+[]() {
                mqttclient.topic_publish = mqtt_config::topic_publish();
                mqttclient.topic_subscribe = mqtt_config::topic_subscribe();
                mqttclient.connect(
                    mqtt_config::server_ip(),
                    mqtt_config::client(),
                    mqtt_config::passward());
            }>("MQTT_connect");

            mesp::command_emplace<+[]() {
                webfpr("WiFi配置已保存,3秒后重启...");
                mstd::delay(3s);
                ESP.restart();
            }>("wifi_save_restart");

            mesp::command_emplace<+[]() {
                if (g_wifi) {
                    g_wifi->reset();
                }
                webfpr("WiFi配置已清空,3秒后重启进入AP模式...");
                mstd::delay(3s);
                ESP.restart();
            }>("wifi_reset");

            mesp::command_emplace<+[](Exstring<512> url) {
                async_channel.emplace([url]() {
                    ota_from_url(url);
                });
            }>("ota_url");
        });





    void Task1(void* param) {
        mesp::gpio_set_in(topams::estop_button);

        int32_t last_level = 1;
        auto last_press = std::chrono::steady_clock::now();

        while (true) {
            int32_t level = gpio_get_level(topams::estop_button);

            if (level == 0 && last_level == 1) {
                auto now = std::chrono::steady_clock::now();
                if (now - last_press > std::chrono::milliseconds(200)) {
                    last_press = now;
                    if (emergency_stop.load()) {
                        emergency_stop.store(false);
                        webfpr("紧急停止已解除");
                    } else {
                        emergency_stop_all();
                    }
                }
            }

            last_level = level;
            mstd::delay(20ms);
        }
    }

    TaskHandle_t Task1_handle;


}//topams




extern "C" void app_main(void) {
    using namespace mesp;

    {
        const gpio_num_t pins[] = {
            topams::main_motor.in1,
            topams::main_motor.in2,
            topams::channels[0].solenoid,
            topams::channels[1].solenoid,
            topams::channels[2].solenoid,
            topams::channels[3].solenoid
        };
        for (auto IO : pins) {
            if (IO == GPIO_NUM_NC) continue;
            gpio_config_t io_conf = {
                (1ULL << IO),
                GPIO_MODE_OUTPUT,
                GPIO_PULLUP_DISABLE,
                GPIO_PULLDOWN_ENABLE,
                GPIO_INTR_DISABLE
            };
            gpio_config(&io_conf);
            gpio_set_level(IO, 0);
        }
    }

    ws_command_init();

    fpr("main函数开始");
    fpr("wsValue数量:", wsValue_state.map.size());

    mesp::gpio_out(topams::main_motor.in1, false);
    mesp::gpio_out(topams::main_motor.in2, false);
    for (size_t i = 0; i < topams::MAX_CHANNELS; i++) {
        auto& x = topams::channels[i];
        mesp::gpio_out(x.solenoid, false);
    }



    //异步任务处理,mini线程池
    std::thread async_thread([]() {
        while (true) {
            auto task = async_channel.pop();
            task();
        }
    });
    xTaskCreate(topams::Task1, "Task1", 2048, NULL, 1, &topams::Task1_handle);//微动任务


    static smartWIFI wifi;
    topams::g_wifi = &wifi;
    // smartWIFI wifi("SSID", "PASS");//也可以先写死
    wifi.connected();

    AsyncWebServer server(80);
    AsyncWebSocket& ws = mesp::ws_server;


    topams::mqttclient.data_event = topams::data_event;


    mesp::ws_nvs_value<bool> boot_connect{"boot_connect", false};//开机连接
    if (boot_connect.get_value()) {
        topams::mqttclient.topic_publish = topams::mqtt_config::topic_publish();
        topams::mqttclient.topic_subscribe = topams::mqtt_config::topic_subscribe();
        topams::mqttclient.connect(
            topams::mqtt_config::server_ip(),
            topams::mqtt_config::client(),
            topams::mqtt_config::passward());
    }


    {//服务器配置部分
        server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
            // request->send(200, "text/html", web.c_str());
            request->send(200, "text/html", web.data());
        });

        server.on("/update", HTTP_POST,
            [](AsyncWebServerRequest* request) {
                AsyncWebServerResponse* response;
                if (Update.hasError()) {
                    webfpr("OTA更新失败,错误:" + Exstring(Update.getError()));
                    response = request->beginResponse(500, "application/json",
                        "{\"success\":false,\"message\":\"OTA更新失败\"}");
                } else {
                    webfpr("固件上传完成,准备重启...");
                    response = request->beginResponse(200, "application/json",
                        "{\"success\":true,\"message\":\"固件上传成功,即将重启\"}");
                }
                request->send(response);
                if (!Update.hasError()) {
                    mstd::delay(2s);
                    ESP.restart();
                }
            },
            [](AsyncWebServerRequest* request, const String& filename, size_t index, uint8_t* data, size_t len, bool final) {
                if (!index) {
                    fpr("OTA文件上传开始,文件名:", filename.c_str());
                    webfpr("开始OTA文件上传,文件名:");
                    webfpr(filename.c_str());
                    ota_in_progress = true;
                    size_t uploadSize = request->contentLength();
                    if (!Update.begin(uploadSize, U_FLASH)) {
                        webfpr("OTA begin失败,错误:" + Exstring(Update.getError()));
                        Update.end(false);
                        ota_in_progress = false;
                        return;
                    }
                    webfpr("固件大小:" + Exstring(uploadSize));
                }
                if (len) {
                    size_t written = Update.write(data, len);
                    if (written != len) {
                        webfpr("OTA写入失败,已写:" + Exstring(written) + "期望:" + Exstring(len));
                    }
                }
                if (final) {
                    if (Update.end(true)) {
                        webfpr("OTA写入完成");
                        ota_in_progress = false;
                    } else {
                        webfpr("OTA结束失败,错误:" + Exstring(Update.getError()));
                        Update.end(false);
                        ota_in_progress = false;
                    }
                }
            }
        );

        server.addHandler(&ws);

        // 设置未找到路径的处理
        server.onNotFound([](AsyncWebServerRequest* request) {
            request->send(404, "text/plain", "404: Not found");
        });

        // 启动服务器
        server.begin();
        fpr("HTTP 服务器已启动");
    }


    fpr_mem();

    //loop
    size_t count = 0;
    for (;;) {
        ws.cleanupClients();//清理断开的ws客户端
        mstd::delay(2s);
        if (count % 100 == 0)
            fpr_mem();
        ++count;
    }
    return;
}
