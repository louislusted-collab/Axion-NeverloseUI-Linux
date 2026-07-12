#pragma once
#include "../imgui.h"
#include "../imgui_internal.h"

// ImLength — missing from NeverloseUI's imgui_internal (has ImLengthSqr but not ImLength)
static inline float ImLength(const ImVec2& v, float /*fail_value*/ = 0.f)
{
    return ImSqrt(v.x * v.x + v.y * v.y);
}

namespace edited
{
    // Tab — custom Axion tab button
    static inline bool Tab(bool selected, const char* label, const char* /*icon*/ = nullptr,
                           const char* /*tooltip*/ = nullptr, ImVec2 size = {0,0})
    {
        ImGui::PushStyleColor(ImGuiCol_Header,        ImVec4(0.20f,0.20f,0.22f,1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f,0.25f,0.28f,1.f));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  ImVec4(0.28f,0.28f,0.32f,1.f));
        bool clicked = ImGui::Selectable(label, selected, 0, size);
        ImGui::PopStyleColor(3);
        return clicked;
    }

    static inline bool BeginChild(const char* id, ImVec2 size = {0,0}, bool border = false,
                                  ImGuiWindowFlags flags = 0)
    {
        return ImGui::BeginChild(id, size, border, flags);
    }

    static inline void EndChild() { ImGui::EndChild(); }

    static inline bool Button(const char* label, ImVec2 size = {0,0}, int /*style_id*/ = 0)
    {
        return ImGui::Button(label, size);
    }

    static inline bool Checkbox(const char* label, const char* /*desc*/ = nullptr, bool* v = nullptr)
    {
        return v ? ImGui::Checkbox(label, v) : false;
    }

    static inline bool Combo(const char* label, const char* /*desc*/ = nullptr, int* current = nullptr,
                             const char* const items[] = nullptr, int count = 0, int /*max_height*/ = -1)
    {
        if (!current || !items) return false;
        return ImGui::Combo(label, current, items, count);
    }

    static inline bool MultiCombo(const char* label, unsigned int* flags,
                                  const char* const items[], int count)
    {
        if (!flags || !items) return false;
        bool changed = false;
        if (ImGui::BeginCombo(label, "..."))
        {
            for (int i = 0; i < count; i++)
            {
                bool sel = (*flags & (1u << i)) != 0;
                if (ImGui::Selectable(items[i], sel))
                {
                    *flags ^= (1u << i);
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    static inline bool SliderFloat(const char* label, const char* /*desc*/ = nullptr, float* v = nullptr,
                                   float mn = 0.f, float mx = 1.f, const char* fmt = "%.2f")
    {
        return v ? ImGui::SliderFloat(label, v, mn, mx, fmt) : false;
    }

    static inline bool SliderInt(const char* label, const char* /*desc*/ = nullptr, int* v = nullptr,
                                 int mn = 0, int mx = 100, const char* fmt = "%d")
    {
        return v ? ImGui::SliderInt(label, v, mn, mx, fmt) : false;
    }

    // Color — accepts Color_t* or any type that has BaseAlpha(float[4]) and a float[4] constructor
    template<typename ColorT>
    static inline bool Color(const char* label, const char* /*desc*/, ColorT* col,
                             ImGuiColorEditFlags flags = 0)
    {
        if (!col) return false;
        float arr[4];
        col->BaseAlpha(arr);
        bool changed = ImGui::ColorEdit4(label, arr, flags);
        if (changed) *col = ColorT(arr[0], arr[1], arr[2], arr[3]);
        return changed;
    }

    static inline bool KeyBind(const char* label, unsigned int* key)
    {
        if (!key) return false;
        ImGui::Text("%s: [0x%X]", label, *key);
        return false;
    }

    // pointbox — hitbox selector visualization widget stub
    static inline void pointbox(const char* id, bool* values, int /*slot*/, float x, float y)
    {
        if (!values) return;
        ImGui::SetCursorPos({x, y});
        ImGui::Checkbox(id, values);
    }

    // SmallCheckbox — compact checkbox (NeverloseUI extension, stub as regular Checkbox)
    static inline bool SmallCheckbox(const char* label, bool* v, int /*style*/ = 0)
    {
        return ImGui::Checkbox(label, v);
    }
}

// ImVec2 scalar operators (NeverloseUI extension)
#ifndef IMGUI_VEC2_SCALAR_OPS
#define IMGUI_VEC2_SCALAR_OPS
static inline ImVec2 operator+(const ImVec2& v, float s) { return { v.x + s, v.y + s }; }
static inline ImVec2 operator-(const ImVec2& v, float s) { return { v.x - s, v.y - s }; }
static inline ImVec2& operator+=(ImVec2& v, float s) { v.x += s; v.y += s; return v; }
static inline ImVec2& operator-=(ImVec2& v, float s) { v.x -= s; v.y -= s; return v; }
#endif
