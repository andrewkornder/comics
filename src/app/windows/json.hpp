#pragma once

#include "../utils.h"

#include <format>
#include <ranges>
#include <string_view>
#include <type_traits>

#include <imgui/imgui.h>

#include <glaze/glaze.hpp>
#include <glaze/core/meta.hpp>


template<typename T>
void render_json(const T& value, bool expanded);


namespace detail {
    constexpr auto json_child_flags = ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AutoResizeX | ImGuiChildFlags_AlwaysAutoResize;
    constexpr auto json_wchild_flags = 0;


    template<typename T, template <typename...> typename Template>
    struct is_instantiation_of : std::false_type {};

    template<template<typename...> typename Template, typename... Args>
    struct is_instantiation_of<Template<Args...>, Template> : std::true_type {};

    template<typename T, template<typename...> typename Template>
    concept instance_of = is_instantiation_of<T, Template>::value;

    template<typename T>
    concept is_primitive = std::is_integral_v<T> || std::is_floating_point_v<T> || std::is_same_v<T, bool>;

    template<typename T>
    concept is_string_like = std::is_same_v<T, std::string> || std::is_same_v<T, std::string_view> || std::is_same_v<T, const char*>;

    template<typename T>
    concept is_object_like = std::ranges::sized_range<T> && instance_of<std::remove_cvref_t<std::ranges::range_value_t<T>>, std::pair>;

    template<typename S>
    void copy_button(const std::string_view label, const S& value) {
        if (ImGui::Button(label.data())) {
            copy_to_clipboard(value);
            ImGui::CloseCurrentPopup();
        }
    }

    template<typename V>
    void render_inner_no_tree(const V& v) {
        if constexpr (is_primitive<V> || is_string_like<V>) {
            const std::string s = glz::write_json(v).value_or("??");
            ImGui::TextUnformatted(s.c_str(), s.c_str() + s.size());
        } else if constexpr (instance_of<V, std::variant>) {
            std::visit([](const auto& c) { return render_inner_no_tree(c); }, v);
        } else if constexpr (instance_of<V, std::optional>) {
            render_inner_no_tree(*v);
        } else if constexpr (std::ranges::sized_range<V>) {
            for (std::size_t i = 0; const auto& item : v) {
                ImGui::PushID(i);
                render_json(item, false);
                ImGui::PopID();

                ++i;
            }
        } else if constexpr (instance_of<V, std::pair>) {
            render_inner_no_tree(v.second);
        } else {
            using info = glz::reflect<V>;
            auto tie = glz::to_tie(v);
            glz::for_each<info::size>([&]<auto I>() {
                ImGui::PushID((int) I);
                render_json(std::make_pair(info::keys[I], glz::get<I>(tie)), false);
                ImGui::PopID();
            });
        }
    };

    template<typename T>
    std::pair<std::string, bool> try_get_json_text(const T& value) {
        if constexpr (instance_of<T, std::variant>) {
            return std::visit([](const auto& v) { return try_get_json_text(v); }, value);
        } else if constexpr (instance_of<T, std::optional>) {
            return value ? try_get_json_text(*value) : std::make_pair(std::string("null"), true);
        } else if constexpr (is_primitive<T>) {
            const std::string v = glz::write_json(value).value();
            return {std::move(v), true};
        } else if constexpr (is_string_like<T>){
            if (value.size() >= 250) {
                std::string v = glz::write_json(std::string_view{value}.substr(0, 20)).value();
                v.pop_back();
                v.append("...\"");
                return {std::move(v), false};
            } else {
                const std::string v = glz::write_json(value).value();
                return {std::move(v), true};
            }
        } else {
            std::string name;
            if constexpr (std::ranges::sized_range<T>) {
                name = std::format("{}[{}]", is_object_like<T> ? "map" : "arr", value.size());
            } else {
                using info = glz::reflect<T>;
                name = std::format("{}[{}]", glz::name_v<T>, info::size);
            }
            return {std::move(name), false};
        }
    }

    template<typename T>
    void render_json_copyable(const T& value, const std::string& dump) {
        const ImVec2 area = ImGui::CalcTextSize(dump.c_str(), dump.c_str() + dump.size());
        const ImVec2 cursor = ImGui::GetCursorPos();

        ImGui::SetNextItemAllowOverlap();
        ImGui::InvisibleButton("##json_copy_btn", area);
        if (ImGui::BeginPopupContextItem("##json_copy_ctx")) {
            if constexpr (is_string_like<T>) {
                copy_button("Copy raw", value);
                ImGui::SameLine();
                copy_button("Copy JSON", dump);
            } else {
                copy_button("Copy", dump);
            }
            ImGui::EndPopup();
        }

        ImGui::SetCursorPos(cursor);
        ImGui::TextUnformatted(dump.c_str(), dump.c_str() + dump.size());
    }
}


template<typename T>
void render_json(const T& value, bool expanded) {
    using namespace detail;
    
    if constexpr (instance_of<T, std::variant>) {
        std::visit([](const auto& v) { render_json(v); }, value);
    } else {
        if (ImGui::BeginChild("##json", {}, json_child_flags, json_wchild_flags)) {
            if (expanded) {
                render_inner_no_tree(value);
            } else if constexpr (instance_of<T, std::pair>) {
                static_assert(is_string_like<std::remove_cvref_t<typename T::first_type>>);
                std::string k = glz::write_json(value.first).value();
                const auto [v, full] = try_get_json_text(value.second);
                if (full) {
                    ImGui::PushID("k"); render_json_copyable(value.first, k); ImGui::PopID();
                    ImGui::SameLine(0.0f, 0.0f); ImGui::TextUnformatted(": "); ImGui::SameLine(0.0f, 0.0f); 
                    ImGui::PushID("v"); render_json_copyable(value.second, v); ImGui::PopID();
                } else {
                    const bool opened = ImGui::TreeNode("pair_node", "%s: %s", k.c_str(), v.c_str());
                    if (ImGui::BeginPopupContextItem("##pair_ctx")) {
                        copy_button("Copy raw", value.first);
                        ImGui::SameLine();
                        copy_button("Copy JSON", k);
                        ImGui::EndPopup();
                    }
                    if (opened) {
                        render_inner_no_tree(value.second);
                        ImGui::TreePop();
                    }
                }
            } else {
                const auto [v, full] = try_get_json_text(value);
                if (full) {
                    render_json_copyable(value, v);
                } else if (ImGui::TreeNode("prim_node", "%s", v.c_str())) {
                    render_inner_no_tree(value);
                    ImGui::TreePop();
                }
            }
        }
        ImGui::EndChild();
    }
}
