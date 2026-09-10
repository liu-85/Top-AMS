/*
* Copyright (C) 2025-2026 by nccrrv
* SPDX-License-Identifier: AGPL-3.0-or-later
* 
* espIO.hpp
* 简单的esp输入输出包装
*/

#pragma once

#include "../mstd/other.hpp"
#include "config.hpp"


namespace mesp {

    inline std::array<gpio_config_t, config::MAX_GPIO> gpio_state{};

    inline mstd::call_once __init_gpio(
        []() {
            for (size_t i = 0; i < config::MAX_GPIO; ++i) {
                gpio_state[i] = {
                    1ull << i,// 设置要操作的接口,掩码结构
                    GPIO_MODE_DISABLE,// 设置是输入还是输出
                    GPIO_PULLUP_DISABLE,// 开关上拉
                    GPIO_PULLDOWN_DISABLE,// 开关下拉
                    GPIO_INTR_DISABLE// 开关中断
                };
            }
        });


    inline bool gpio_valid(gpio_num_t IO) noexcept {
        if (IO == GPIO_NUM_NC) return false;
        if (IO < 0 || static_cast<size_t>(IO) >= config::MAX_GPIO) return false;
        return true;
    }

    //普通输出
    inline void gpio_out(gpio_num_t IO, bool value) {
        if (!gpio_valid(IO)) return;

        if (gpio_state[IO].mode != GPIO_MODE_OUTPUT) {
            gpio_config_t io_conf = {
                1ull << IO,
                GPIO_MODE_OUTPUT,
                GPIO_PULLUP_DISABLE,
                GPIO_PULLDOWN_ENABLE,
                GPIO_INTR_DISABLE
            };
            gpio_config(&io_conf);
            gpio_set_level(IO, 0);
        }

        gpio_set_level(IO, value ? 1 : 0);
    }//gpio_out

    //开漏输出
    inline void gpio_out_OD(gpio_num_t IO, bool value) {
        if (!gpio_valid(IO)) return;

        if (gpio_state[IO].mode != GPIO_MODE_OUTPUT_OD) {
            gpio_config_t io_conf = {
                1ull << IO,
                GPIO_MODE_OUTPUT_OD,
                GPIO_PULLUP_DISABLE,
                GPIO_PULLDOWN_DISABLE,
                GPIO_INTR_DISABLE
            };
            gpio_config(&io_conf);
            gpio_set_level(IO, 0);
        }

        gpio_set_level(IO, value ? 1 : 0);
    }//gpio_out_OD



    using gpin_t = std::tuple<gpio_num_t, std::chrono::steady_clock::time_point>;//可能叫button更好

    inline QueueHandle_t gpio_channle = xQueueCreate(1, sizeof(gpin_t));//中断队列

    //统一将IO脚编号加入队列
    inline void IRAM_ATTR __gpio_isr_handler(void* arg) {
        gpio_num_t gpio_num = (gpio_num_t)(uintptr_t)(arg);
        gpin_t in{gpio_num, std::chrono::steady_clock::now()};//@_@可能不需要
        xQueueSendFromISR(gpio_channle, &in, NULL);
    }

    inline void gpio_set_in(gpio_num_t IO) {
        if (!gpio_valid(IO)) return;

        uint64_t pin_mask = 1ull << IO;
        gpio_config_t io_conf = {
            pin_mask,
            GPIO_MODE_INPUT,
            GPIO_PULLUP_ENABLE,
            GPIO_PULLDOWN_DISABLE,
            GPIO_INTR_ANYEDGE
        };
        gpio_config(&io_conf);
        gpio_set_intr_type(IO, GPIO_INTR_ANYEDGE);

        gpio_isr_handler_add(IO, __gpio_isr_handler, (void*)IO);
    }//gpio_set_in

    inline mstd::call_once __gpio_set_init([] {
        gpio_install_isr_service(0);
    });

}//mesp