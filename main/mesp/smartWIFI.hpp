/*
* Copyright (C) 2025-2026 by nccrrv
* SPDX-License-Identifier: AGPL-3.0-or-later
* 
* smartWIFI.hpp
* Example:
* 
*   smartWIFI wifi;
*   smartWIFI wifi("SSID", "PASS");//也可以先写死
*   wifi.connected();//阻塞,等待配网,有过SSID后直接开始连接
*   wifi.reset();//重置WiFi配置
*/

#pragma once
#include "wsValue.hpp"
#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include "espIO.hpp"

namespace mesp {

    namespace ap_config_html {
        constexpr const char* PAGE = R"HTML(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Top-AMS WiFi配网</title>
    <style>
        * { box-sizing: border-box; margin: 0; padding: 0; }
        body {
            font-family: 'Segoe UI', 'PingFang SC', 'Microsoft YaHei', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            display: flex;
            align-items: center;
            justify-content: center;
            padding: 20px;
        }
        .card {
            background: white;
            border-radius: 16px;
            box-shadow: 0 20px 60px rgba(0,0,0,0.3);
            padding: 32px;
            width: 100%;
            max-width: 420px;
        }
        .header {
            text-align: center;
            margin-bottom: 28px;
        }
        .header h1 {
            color: #2c3e50;
            font-size: 24px;
            margin-bottom: 8px;
        }
        .header p {
            color: #7f8c8d;
            font-size: 14px;
        }
        .logo {
            width: 64px;
            height: 64px;
            margin: 0 auto 16px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            border-radius: 16px;
            display: flex;
            align-items: center;
            justify-content: center;
            color: white;
            font-size: 28px;
            font-weight: bold;
        }
        .form-group {
            margin-bottom: 20px;
        }
        .form-group label {
            display: block;
            margin-bottom: 8px;
            color: #34495e;
            font-weight: 500;
            font-size: 14px;
        }
        .form-group input {
            width: 100%;
            padding: 12px 16px;
            border: 2px solid #e0e0e0;
            border-radius: 8px;
            font-size: 15px;
            transition: all 0.2s;
            outline: none;
        }
        .form-group input:focus {
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        .submit-btn {
            width: 100%;
            padding: 14px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            border: none;
            border-radius: 8px;
            font-size: 16px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            margin-top: 8px;
        }
        .submit-btn:hover {
            transform: translateY(-1px);
            box-shadow: 0 8px 20px rgba(102, 126, 234, 0.4);
        }
        .submit-btn:active {
            transform: translateY(0);
        }
        .submit-btn:disabled {
            opacity: 0.6;
            cursor: not-allowed;
            transform: none;
        }
        .status-msg {
            margin-top: 20px;
            padding: 12px;
            border-radius: 8px;
            text-align: center;
            font-size: 14px;
            display: none;
        }
        .status-msg.success {
            display: block;
            background: #d4edda;
            color: #155724;
            border: 1px solid #c3e6cb;
        }
        .status-msg.error {
            display: block;
            background: #f8d7da;
            color: #721c24;
            border: 1px solid #f5c6cb;
        }
        .status-msg.info {
            display: block;
            background: #d1ecf1;
            color: #0c5460;
            border: 1px solid #bee5eb;
        }
        .wifi-list {
            margin-top: 12px;
            max-height: 200px;
            overflow-y: auto;
            border: 1px solid #e0e0e0;
            border-radius: 8px;
        }
        .wifi-item {
            padding: 10px 14px;
            cursor: pointer;
            border-bottom: 1px solid #f0f0f0;
            display: flex;
            align-items: center;
            gap: 10px;
            transition: background 0.15s;
        }
        .wifi-item:last-child { border-bottom: none; }
        .wifi-item:hover { background: #f8f9fa; }
        .wifi-name { flex: 1; font-size: 14px; color: #333; }
        .wifi-rssi { font-size: 12px; color: #999; }
        .refresh-btn {
            margin-top: 10px;
            padding: 8px 14px;
            background: #f8f9fa;
            border: 1px solid #dee2e6;
            border-radius: 6px;
            cursor: pointer;
            font-size: 13px;
            color: #495057;
        }
        .refresh-btn:hover { background: #e9ecef; }
        .scan-section { margin-bottom: 8px; }
    </style>
</head>
<body>
    <div class="card">
        <div class="header">
            <div class="logo">A</div>
            <h1>Top-AMS 配网</h1>
            <p>请输入您的WiFi信息以连接设备</p>
        </div>
        
        <div class="scan-section">
            <label style="color:#34495e;font-weight:500;font-size:14px;">附近WiFi网络（点击选择）</label>
            <div id="wifiList" class="wifi-list">
                <div style="padding:16px;text-align:center;color:#999;font-size:13px;">扫描中...</div>
            </div>
            <button class="refresh-btn" onclick="scanWifi()">🔄 刷新列表</button>
        </div>

        <form id="wifiForm">
            <div class="form-group">
                <label for="ssid">WiFi 名称 (SSID)</label>
                <input type="text" id="ssid" name="ssid" required autocomplete="off" placeholder="请输入WiFi名称">
            </div>
            <div class="form-group">
                <label for="password">WiFi 密码</label>
                <input type="password" id="password" name="password" autocomplete="off" placeholder="请输入WiFi密码（无密码留空）">
            </div>
            <button type="submit" class="submit-btn" id="submitBtn">连接 WiFi</button>
        </form>

        <div id="statusMsg" class="status-msg"></div>
    </div>

    <script>
        function scanWifi() {
            const list = document.getElementById('wifiList');
            list.innerHTML = '<div style="padding:16px;text-align:center;color:#999;font-size:13px;">扫描中...</div>';
            fetch('/scan')
                .then(r => r.json())
                .then(data => {
                    list.innerHTML = '';
                    if (data.length === 0) {
                        list.innerHTML = '<div style="padding:16px;text-align:center;color:#999;font-size:13px;">未找到WiFi，请手动输入</div>';
                        return;
                    }
                    data.forEach(net => {
                        const item = document.createElement('div');
                        item.className = 'wifi-item';
                        const bars = net.rssi > -50 ? '📶' : net.rssi > -65 ? '📶' : net.rssi > -80 ? '📶' : '📶';
                        item.innerHTML = `<span>${bars}</span><span class="wifi-name">${escapeHtml(net.ssid)}</span><span class="wifi-rssi">${net.rssi}dBm</span>`;
                        item.onclick = () => {
                            document.getElementById('ssid').value = net.ssid;
                            document.getElementById('password').focus();
                        };
                        list.appendChild(item);
                    });
                })
                .catch(() => {
                    list.innerHTML = '<div style="padding:16px;text-align:center;color:#999;font-size:13px;">扫描失败，请手动输入</div>';
                });
        }

        function escapeHtml(str) {
            const div = document.createElement('div');
            div.textContent = str;
            return div.innerHTML;
        }

        function showStatus(msg, type) {
            const el = document.getElementById('statusMsg');
            el.className = 'status-msg ' + type;
            el.textContent = msg;
        }

        document.getElementById('wifiForm').addEventListener('submit', function(e) {
            e.preventDefault();
            const ssid = document.getElementById('ssid').value.trim();
            const password = document.getElementById('password').value;
            const btn = document.getElementById('submitBtn');
            
            if (!ssid) {
                showStatus('请输入WiFi名称', 'error');
                return;
            }

            btn.disabled = true;
            btn.textContent = '正在连接...';
            showStatus('正在保存配置并尝试连接...', 'info');

            fetch('/connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid: ssid, password: password })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    showStatus('配置成功！设备将在3秒后重启并连接WiFi。请在路由器中查看设备IP。', 'success');
                    btn.textContent = '配置成功';
                } else {
                    showStatus('配置失败：' + (data.message || '未知错误'), 'error');
                    btn.disabled = false;
                    btn.textContent = '连接 WiFi';
                }
            })
            .catch(() => {
                showStatus('网络请求失败，请重试', 'error');
                btn.disabled = false;
                btn.textContent = '连接 WiFi';
            });
        });

        scanWifi();
    </script>
</body>
</html>
)HTML";
    }//namespace ap_config_html

    struct smartWIFI {

        ws_nvs_value<Exstring<32>> Wifi_ssid{"Wifi_ssid", ""};
        ws_nvs_value<Exstring<64>> Wifi_pass{"Wifi_pass", ""};

        smartWIFI() = default;

        template <typename T, typename Y>
        smartWIFI(T&& ssid, Y&& pass) {
            Wifi_ssid.set_value(Exstring<32>(std::forward<T>(ssid)));
            Wifi_pass.set_value(Exstring<64>(std::forward<Y>(pass)));
        }

        void start_ap_mode() {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("Top-AMS-Config", "12345678");

            IPAddress apIP = WiFi.softAPIP();
            fpr("AP模式已启动");
            fpr("AP SSID: Top-AMS-Config");
            fpr("AP Password: 12345678");
            fpr("AP IP Address: ", (int)apIP[0], ".", (int)apIP[1], ".", (int)apIP[2], ".", (int)apIP[3]);

            static AsyncWebServer ap_server(80);

            ap_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
                request->send(200, "text/html", ap_config_html::PAGE);
            });

            ap_server.on("/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
                int n = WiFi.scanNetworks(false, true);
                String json = "[";
                for (int i = 0; i < n; ++i) {
                    if (i > 0) json += ",";
                    String ssid = WiFi.SSID(i);
                    ssid.replace("\"", "\\\"");
                    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
                }
                json += "]";
                request->send(200, "application/json", json);
            });

            ap_server.on(
                "/connect",
                HTTP_POST,
                [this](AsyncWebServerRequest* request) {
                    request->send(200, "application/json", "{\"success\":true}");
                    mstd::delay(3s);
                    WiFi.softAPdisconnect(true);
                    ESP.restart();
                },
                nullptr,
                [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if (index == 0 && len > 0) {
                        StaticJsonDocument<512> doc;
                        DeserializationError err = deserializeJson(doc, data, len);
                        if (err) {
                            return;
                        }
                        const char* ssid = doc["ssid"] | "";
                        const char* password = doc["password"] | "";
                        if (strlen(ssid) == 0) {
                            return;
                        }
                        this->Wifi_ssid.set_value(Exstring<32>(ssid));
                        this->Wifi_pass.set_value(Exstring<64>(password));
                    }
                }
            );

            ap_server.begin();
            fpr("AP配网服务器已启动,请连接WiFi后访问 http://192.168.4.1");

            size_t cnt = 0;
            while (true) {
                gpio_out(config::WIFI_LED, cnt % 2);
                ++cnt;
                mstd::delay(500ms);
            }
        }

        void start_ap_mode() {
            WiFi.mode(WIFI_AP_STA);
            WiFi.softAP("Top-AMS-Config", "12345678");

            IPAddress apIP = WiFi.softAPIP();
            fpr("AP模式已启动");
            fpr("AP SSID: Top-AMS-Config");
            fpr("AP Password: 12345678");
            fpr("AP IP Address: ", (int)apIP[0], ".", (int)apIP[1], ".", (int)apIP[2], ".", (int)apIP[3]);

            static AsyncWebServer ap_server(80);

            ap_server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
                request->send(200, "text/html", ap_config_html::PAGE);
            });

            ap_server.on("/scan", HTTP_GET, [](AsyncWebServerRequest* request) {
                int n = WiFi.scanNetworks(false, true);
                String json = "[";
                for (int i = 0; i < n; ++i) {
                    if (i > 0) json += ",";
                    String ssid = WiFi.SSID(i);
                    ssid.replace("\"", "\\\"");
                    json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
                }
                json += "]";
                request->send(200, "application/json", json);
            });

            ap_server.on(
                "/connect",
                HTTP_POST,
                [this](AsyncWebServerRequest* request) {
                    request->send(200, "application/json", "{\"success\":true}");
                    mstd::delay(3s);
                    WiFi.softAPdisconnect(true);
                    ESP.restart();
                },
                nullptr,
                [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
                    if (index == 0 && len > 0) {
                        StaticJsonDocument<512> doc;
                        DeserializationError err = deserializeJson(doc, data, len);
                        if (err) {
                            return;
                        }
                        const char* ssid = doc["ssid"] | "";
                        const char* password = doc["password"] | "";
                        if (strlen(ssid) == 0) {
                            return;
                        }
                        this->Wifi_ssid = ssid;
                        this->Wifi_pass = password;
                    }
                }
            );

            ap_server.begin();
            fpr("AP配网服务器已启动,请连接WiFi后访问 http://192.168.4.1");

            size_t cnt = 0;
            while (true) {
                gpio_out(config::WIFI_LED, cnt % 2);
                ++cnt;
                mstd::delay(500ms);
            }
        }

        //连接,阻塞
        void connected() {
            size_t cnt = 0;

            if (Wifi_ssid == "") {
                WiFi.mode(WIFI_AP_STA);
                WiFi.beginSmartConfig();

                while (!WiFi.smartConfigDone()) {
                    gpio_out(config::WIFI_LED, cnt % 2);//慢闪,等待配网
                    ++cnt;
                    fpr("Waiting for SmartConfig");
                    mstd::delay(1000ms);
                }

                Wifi_ssid = WiFi.SSID().c_str();
                Wifi_pass = WiFi.psk().c_str();

            } else {
                WiFi.begin(Wifi_ssid.get().c_str(), Wifi_pass.get().c_str());
            }

            // 等待WiFi连接到路由器
            while (WiFi.status() != WL_CONNECTED) {
                gpio_out(config::WIFI_LED, cnt % 2);//快闪,配网中
                ++cnt;
                fpr("Waiting for WiFi Connected");
                mstd::delay(250ms);
            }

            gpio_out(config::WIFI_LED, false);//关闭,连接成功
            fpr("WiFi Connected to AP");
            fpr("STA IP Address: ", (int)WiFi.localIP()[0], ".", (int)WiFi.localIP()[1], ".", (int)WiFi.localIP()[2], ".", (int)WiFi.localIP()[3]);
            IPAddress apIP = WiFi.softAPIP();
            fpr("AP IP Address: ", (int)apIP[0], ".", (int)apIP[1], ".", (int)apIP[2], ".", (int)apIP[3]);
            fpr("AP SSID: Top-AMS-Config (始终开启,APP可直接连接)");
        }

        void reset() {
            Wifi_ssid.set_value(Exstring<32>(""));
            Wifi_pass.set_value(Exstring<64>(""));
        }

    };//smartWIFI
}//mesp
