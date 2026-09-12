/*
* Copyright (C) 2025-2026 by nccrrv
* SPDX-License-Identifier: AGPL-3.0-or-later
* 
* rangebase.hpp
* 范围基础类的声明
*/

#pragma once

#include "Meta.hpp"
#include "other.hpp"
#include <compare>
#include <utility>

namespace mstd {

    template <typename Derived>
    struct rangebase_crtp {

      private:
        constexpr Derived* self() noexcept { return static_cast<Derived*>(this); }
        constexpr const Derived* self() const noexcept { return static_cast<const Derived*>(this); }

      public:
        constexpr decltype(auto) operator[](size_t index) & noexcept {
            return self()->data()[index];
        }
        constexpr decltype(auto) operator[](size_t index) const& noexcept {
            return self()->data()[index];
        }

        constexpr auto begin() & noexcept {
            return self()->data();
        }
        constexpr auto begin() const& noexcept {
            return self()->data();
        }
        constexpr auto end() & noexcept {
            return self()->data() + self()->size();
        }
        constexpr auto end() const& noexcept {
            return self()->data() + self()->size();
        }

        constexpr bool empty() const noexcept {
            return self()->size() == 0;
        }

        template <typename R>
        constexpr bool operator==(const R& r) const noexcept {
            if (self()->size() != r.size())
                return false;
            for (size_t i = 0; i < self()->size(); i++) {
                if ((*self())[i] != r[i])
                    return false;
            }
            return true;
        }

        template <typename R>
        constexpr std::strong_ordering operator<=>(const R& r) const noexcept {
            size_t s = self()->size();
            size_t min_size = s < r.size() ? s : r.size();
            for (size_t i = 0; i < min_size; i++) {
                if ((*self())[i] < r[i])
                    return std::strong_ordering::less;
                else if ((*self())[i] > r[i])
                    return std::strong_ordering::greater;
            }
            return s <=> r.size();
        }
    };

}//mstd;