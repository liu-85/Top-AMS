idf_component_register(SRCS "main.cpp"
                    INCLUDE_DIRS ""
                    PRIV_REQUIRES
                        nvs_flash
                        driver
                        esp_wifi
                        esp_http_client
                        esp_https_ota
                        app_update
                        mqtt
                        espressif__arduino-esp32
                        esp32async__espasyncwebserver
                        bblanchon__arduinojson)


# 添加自定义目标执行Python脚本
add_custom_target(
    run_convert_html_to_hpp ALL  # ALL表示包含在默认构建目标中
    COMMAND ${CMAKE_COMMAND} -E env PYTHONPATH= python ${CMAKE_CURRENT_SOURCE_DIR}/convert_html_to_hpp.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}  # 设置工作目录为脚本所在位置
    COMMENT "RUN convert_html_to_hpp.py..."  # 可选：添加构建时的提示信息
)