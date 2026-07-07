#include "client/ui.h"

UIState g_ui;

void uiBegin(Vector2 mouse, bool down, bool pressed, double time) {
    g_ui.mouse = mouse;
    g_ui.down = down;
    g_ui.pressed = pressed;
    g_ui.time = time;
}

static bool hot(Rectangle r) { return CheckCollisionPointRec(g_ui.mouse, r); }

void uiPanel(Rectangle r) {
    DrawRectangleRec(r, Color{ 24, 22, 38, 235 });
    DrawRectangleLinesEx(r, 1, Color{ 90, 86, 130, 255 });
}

bool uiButton(Rectangle r, const char* label, bool enabled, Color accent) {
    bool h = enabled && hot(r);
    Color bg = enabled
        ? (h ? Color{ (unsigned char)(accent.r + 40), (unsigned char)(accent.g + 40), (unsigned char)(accent.b + 40), 255 } : accent)
        : Color{ 45, 45, 52, 255 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, h ? WHITE : Color{ 130, 126, 170, 255 });
    int fw = MeasureText(label, 10);
    DrawText(label, (int)(r.x + (r.width - fw) / 2), (int)(r.y + (r.height - 10) / 2),
             10, enabled ? WHITE : GRAY);
    bool clicked = h && g_ui.pressed;
    if (clicked && g_ui.click) PlaySound(*g_ui.click);
    return clicked;
}

bool uiTextBox(Rectangle r, std::string& text, int id, bool password, int maxLen, const char* placeholder) {
    bool h = hot(r);
    if (g_ui.pressed) g_ui.focusId = h ? id : (g_ui.focusId == id ? -1 : g_ui.focusId);
    bool focused = g_ui.focusId == id;

    bool changed = false;
    if (focused) {
        int ch;
        while ((ch = GetCharPressed()) > 0) {
            if (ch >= 32 && ch < 127 && (int)text.size() < maxLen) {
                text += (char)ch;
                changed = true;
            }
        }
        if ((IsKeyPressed(KEY_BACKSPACE) || IsKeyPressedRepeat(KEY_BACKSPACE)) && !text.empty()) {
            text.pop_back();
            changed = true;
        }
        if (IsKeyPressed(KEY_TAB)) g_ui.focusId = id + 1;
    }

    DrawRectangleRec(r, Color{ 14, 13, 24, 255 });
    DrawRectangleLinesEx(r, 1, focused ? Color{ 255, 200, 60, 255 } : Color{ 110, 106, 150, 255 });
    std::string shown = password ? std::string(text.size(), '*') : text;
    if (shown.empty() && placeholder && !focused)
        DrawText(placeholder, (int)r.x + 5, (int)(r.y + (r.height - 10) / 2), 10, Color{ 100, 100, 115, 255 });
    else
        DrawText(shown.c_str(), (int)r.x + 5, (int)(r.y + (r.height - 10) / 2), 10, WHITE);
    if (focused && fmod(g_ui.time, 1.0) < 0.55) {
        int tw = MeasureText(shown.c_str(), 10);
        DrawRectangle((int)r.x + 6 + tw, (int)r.y + 4, 1, (int)r.height - 8, WHITE);
    }
    return changed;
}
