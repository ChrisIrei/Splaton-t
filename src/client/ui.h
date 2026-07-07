// Splaton't client — tiny immediate-mode UI (virtual-resolution coordinates)
#pragma once
#include "raylib.h"
#include <string>

struct UIState {
    Vector2 mouse{};
    bool down = false, pressed = false;
    int focusId = -1;
    double time = 0;
    Sound* click = nullptr;
};
extern UIState g_ui;

void uiBegin(Vector2 mouse, bool down, bool pressed, double time);
void uiPanel(Rectangle r);
bool uiButton(Rectangle r, const char* label, bool enabled = true, Color accent = { 70, 74, 105, 255 });
bool uiTextBox(Rectangle r, std::string& text, int id, bool password = false,
               int maxLen = 24, const char* placeholder = nullptr);
// horizontal drag slider, val in 0..1; returns true while the user drags it
bool uiSlider(Rectangle r, float& val, int id);
