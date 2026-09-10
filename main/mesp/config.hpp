/*
* Copyright (C) 2025-2026 by nccrrv
* SPDX-License-Identifier: AGPL-3.0-or-later
* 
* config.hpp
* 不同esp的配置文件
*/

#pragma once
#include <cstddef>
#include <driver/gpio.h>

namespace mesp::config {

    inline constexpr size_t MAX_GPIO = 20;

    inline gpio_num_t LED_R = GPIO_NUM_NC;
    inline gpio_num_t LED_L = GPIO_NUM_NC;

    inline gpio_num_t WIFI_LED = GPIO_NUM_NC;

}//mesp::config