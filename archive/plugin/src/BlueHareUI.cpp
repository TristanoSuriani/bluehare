/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Tristano Suriani (Blue Hare)
 * Part of Blue Hare, a fork of Felucca by Leo Kuroshita (Hügelton Instruments): see AUTHORS.md */
/* The plugin's window (DPF, NanoVG): the device as the native image shows it. The screen at twice its size, the
 * knobs (dragged up or down, or the wheel over one; MASTER the parameter), the 14 buttons and the 27 keys (held while
 * the mouse button is down), lit as the device lights them; above, the distribution (a click: the chooser, then a
 * question before it switches: the song goes with it) and the eight tracks' outputs (MAIN or OWN).
 * It reads the device through the plugin (DISTRHO_PLUGIN_WANT_DIRECT_ACCESS: fel_host_view, a copy the audio thread
 * keeps) and hands it the panel through fel_host_ui_* (a queue the audio thread plays, each change held 24 ms of the
 * device's time as the native image does). A host that stops running the audio (a silent instrument suspended, the
 * engine off) would leave the device still and its screen dark: the idle then runs it (fel_host_idle).
 * FEL_UI_SHOT=FILE.ppm: a drawn frame with the screen in it written there (FEL_UI_SHOT_AFTER=N: the Nth; a look at the
 * window without a screen capture). */
#include "DistrhoUI.hpp"
#include "OpenGL-include.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include "fel_host.h"
}

START_NAMESPACE_DISTRHO

fel_host_t* felPluginHost(void* plugin);                 /* (BlueHarePlugin.cpp) */

enum { P_MASTER, P_DIST, P_OUT1 };                       /* (as BlueHarePlugin.cpp) */
static const char* const BTN_NAME[14] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY",
                                         "REC", "OCT-", "OCT+"};    /* (panel.c's labels: bit = index) */
static const char* const KNOB_NAME[8] = {"MASTER", "SELECT", "PRESETS", "ALGO", "KNOB 1", "KNOB 2", "KNOB 3", "KNOB 4"};
static const int KNOB_ROLE[8] = {-1, 0, 2, 1, 3, 4, 5, 6};          /* (panel.c EN_*: SELECT ALGO PRESET K1..K4) */
static const uint8_t WHITE[16] = {0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26};
static const uint8_t BLACK[11] = {1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25};
static const uint8_t BLACK_AFTER[11] = {0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14};

#define W 1000
#define H 600
#define SX 20                                            /* the screen: 480 x 480 */
#define SY 80
#define PX 530                                           /* the panel's left edge */
#define DRAG_PX 6

struct Box { float x, y, w, h; bool in(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; } };
static Box knobBox(int i) { return {PX + (float)(i % 4) * 112.0f, 80.0f + (float)(i / 4) * 96.0f, 100.0f, 84.0f}; }
static Box btnBox(int i)
{
    if (i >= 12)
        return {PX + (float)(i - 12) * 76.0f, 276.0f + 2 * 58.0f, 70.0f, 48.0f};
    return {PX + (float)(i % 6) * 76.0f, 276.0f + (float)(i / 6) * 58.0f, 70.0f, 48.0f};
}
static Box keyBox(int k)                                 /* key k (0 F3 .. 26 G5) */
{
    for (int i = 0; i < 16; i++)
        if (WHITE[i] == k)
            return {PX + (float)i * 28.0f, 450.0f, 26.0f, 130.0f};
    for (int i = 0; i < 11; i++)
        if (BLACK[i] == k)
            return {PX + (float)BLACK_AFTER[i] * 28.0f + 17.0f, 450.0f, 20.0f, 80.0f};
    return {0, 0, 0, 0};
}
static Box distBox() { return {SX, 20.0f, 240.0f, 40.0f}; }
static Box outBox(int t) { return {SX + 260.0f + (float)t * 30.0f, 28.0f, 26.0f, 26.0f}; }

class BlueHareUI : public UI
{
public:
    BlueHareUI()
        : UI(W, H)
    {
        loadSharedResources();
        host = felPluginHost(getPluginInstancePointer());
        setGeometryConstraints(W / 2, H / 2, true, false);
        std::memset(&view, 0, sizeof view);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index == P_MASTER)
            master = value;
        else if (index == P_DIST)
            dist = (int)(value + 0.5f);
        else if (index >= P_OUT1 && index < P_OUT1 + 8u)
            outs = value >= 0.5f ? outs | 1u << (index - P_OUT1) : outs & ~(1u << (index - P_OUT1));
        repaint();
    }

    void uiIdle() override
    {
        if (host)                                    /* (a host not running the audio: the device kept going) */
            fel_host_idle(host, (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count());
        if (host && fel_host_view(host, &view, &seq)) {
            for (uint32_t i = 0; i < 240u * 240u; i++) {   /* (RGB565, high byte first -> RGBA) */
                const uint16_t v = (uint16_t)(view.screen[i] >> 8 | view.screen[i] << 8);
                rgba[4 * i] = (uint8_t)((v >> 11 & 31u) * 255u / 31u);
                rgba[4 * i + 1] = (uint8_t)((v >> 5 & 63u) * 255u / 63u);
                rgba[4 * i + 2] = (uint8_t)((v & 31u) * 255u / 31u);
                rgba[4 * i + 3] = 255;
            }
            fresh = true;
            repaint();
        }
    }

    void onNanoDisplay() override
    {
        const float s = (float)getWidth() / W;
        scale(s, s);
        beginPath();
        rect(0, 0, W, H);
        fillColor(22, 22, 26);
        fill();
        if (!host) {
            fontSize(18);
            fillColor(220, 220, 228);
            text(40, 60, "Blue Hare: no core found (the bundle's Contents/Resources/cores)", nullptr);
            return;
        }
        drawScreen();
        drawTop();
        for (int i = 0; i < 8; i++)
            drawKnob(i);
        for (int i = 0; i < 14; i++)
            drawButton(i);
        drawKeys();
        if (menu)
            drawMenu();
        if (ask >= 0)
            drawAsk();
        static int frames;
        if (screen.isValid() && !shotDone && ++frames > (std::getenv("FEL_UI_SHOT_AFTER") ? std::atoi(std::getenv("FEL_UI_SHOT_AFTER")) : 0))
            shot();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        const float s = (float)getWidth() / W, x = (float)ev.pos.getX() / s, y = (float)ev.pos.getY() / s;
        if (ev.button != 1 || !host)
            return false;
        if (!ev.press) {
            dragKnob = -1;
            if (btnHeld | keyHeld) {
                btnHeld = keyHeld = 0;
                fel_host_ui_buttons(host, 0);
                fel_host_ui_keys(host, 0);
            }
            return true;
        }
        if (ask >= 0) {                                  /* the question: SWITCH or CANCEL */
            if (Box{340, 330, 140, 40}.in(x, y)) {
                setParameterValue(P_DIST, (float)ask);
                dist = ask;
            }
            ask = -1;
            repaint();
            return true;
        }
        if (menu) {
            for (uint32_t i = 0; i < fel_host_dists(host); i++)
                if (Box{SX, 64.0f + (float)i * 34.0f, 240.0f, 32.0f}.in(x, y) && (int)i != dist)
                    ask = (int)i;
            menu = false;
            repaint();
            return true;
        }
        if (distBox().in(x, y)) {
            menu = true;
            repaint();
            return true;
        }
        for (int t = 0; t < (int)fel_host_tracks(host) && t < 8; t++)
            if (outBox(t).in(x, y)) {
                outs ^= 1u << t;
                setParameterValue(P_OUT1 + (uint32_t)t, (float)((outs >> t) & 1u));
                repaint();
                return true;
            }
        for (int i = 0; i < 8; i++)
            if (knobBox(i).in(x, y)) {
                dragKnob = i;
                dragY = y;
                return true;
            }
        for (int i = 0; i < 14; i++)
            if (btnBox(i).in(x, y)) {
                btnHeld = 1u << i;
                fel_host_ui_buttons(host, btnHeld);
                return true;
            }
        for (int i = 0; i < 11; i++)                     /* (the black keys over the white ones) */
            if (keyBox(BLACK[i]).in(x, y))
                return pressKey(BLACK[i]);
        for (int i = 0; i < 16; i++)
            if (keyBox(WHITE[i]).in(x, y))
                return pressKey(WHITE[i]);
        return false;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const float s = (float)getWidth() / W, y = (float)ev.pos.getY() / s, x = (float)ev.pos.getX() / s;
        if (dragKnob >= 0) {
            const int n = (int)((dragY - y) / DRAG_PX);  /* (up: clockwise) */
            if (n) {
                turn(dragKnob, n);
                dragY -= (float)n * DRAG_PX;
            }
            return true;
        }
        hot = -1;
        for (int i = 0; i < 8; i++)
            if (knobBox(i).in(x, y))
                hot = i;
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (hot < 0)
            return false;
        wheel += (float)ev.delta.getY();
        while (wheel >= 1.0f || wheel <= -1.0f) {
            turn(hot, wheel > 0.0f ? 1 : -1);
            wheel += wheel > 0.0f ? -1.0f : 1.0f;
        }
        return true;
    }

private:
    fel_host_t* host = nullptr;
    fel_view_t view;
    uint32_t seq = 0;
    uint8_t rgba[240 * 240 * 4];
    NanoImage screen;
    bool fresh = false, menu = false, shotDone = false;
    int ask = -1, dist = 0, dragKnob = -1, hot = -1, knobPos[8] = {0};
    float dragY = 0, wheel = 0, master = 700.0f;
    uint32_t outs = 0, btnHeld = 0, keyHeld = 0;

    void shot()
    {
        const char* path = std::getenv("FEL_UI_SHOT");
        shotDone = true;
        if (!path || !*path)
            return;
        NanoVG::endFrame();                              /* (NanoVG's drawing done, then read back) */
        const int w = (int)getWidth(), h = (int)getHeight();
        uint8_t* px = new uint8_t[(size_t)w * (size_t)h * 4u];
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
        if (FILE* f = std::fopen(path, "wb")) {
            std::fprintf(f, "P6\n%d %d\n255\n", w, h);
            for (int y = h - 1; y >= 0; y--)
                for (int x = 0; x < w; x++)
                    std::fwrite(px + ((size_t)y * (size_t)w + (size_t)x) * 4u, 1, 3, f);
            std::fclose(f);
        }
        delete[] px;
        NanoVG::beginFrame((uint)w, (uint)h);
    }

    bool pressKey(int k)
    {
        keyHeld = 1u << k;
        fel_host_ui_keys(host, keyHeld);
        return true;
    }

    void turn(int knob, int n)
    {
        knobPos[knob] += n;
        if (KNOB_ROLE[knob] < 0) {
            master = std::fmax(0.0f, std::fmin(1023.0f, master + (float)n * 24.0f));
            editParameter(P_MASTER, true);
            setParameterValue(P_MASTER, master);
            editParameter(P_MASTER, false);
        } else {
            fel_host_ui_enc(host, (uint32_t)KNOB_ROLE[knob], n);
        }
        repaint();
    }

    void drawScreen()
    {
        if (fresh) {
            if (!screen.isValid())
                screen = createImageFromRGBA(240, 240, rgba, 1 << 5);   /* (NVG_IMAGE_NEAREST: the pixels sharp) */
            else
                screen.update(rgba);
            fresh = false;
        }
        beginPath();
        rect(SX, SY, 480, 480);
        if (screen.isValid())
            fillPaint(imagePattern(SX, SY, 480, 480, 0, screen, 1.0f));
        else
            fillColor(0, 0, 0);
        fill();
    }

    void drawTop()
    {
        const Box d = distBox();
        beginPath();
        roundedRect(d.x, d.y, d.w, d.h, 6);
        fillColor(48, 48, 56);
        fill();
        fontSize(18);
        fillColor(230, 230, 236);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        text(d.x + 12, d.y + d.h / 2, fel_host_dist_tag(host, (uint32_t)dist), nullptr);
        text(d.x + d.w - 22, d.y + d.h / 2, "v", nullptr);
        fontSize(13);
        fillColor(150, 150, 160);
        text(outBox(0).x, 16, "TRACK OUTPUTS (M: main, O: own)", nullptr);
        for (int t = 0; t < (int)fel_host_tracks(host) && t < 8; t++) {
            const Box b = outBox(t);
            const bool own = (outs >> t) & 1u;
            char n[4];
            beginPath();
            roundedRect(b.x, b.y, b.w, b.h, 4);
            fillColor(own ? Color(80, 200, 220) : Color(58, 58, 66));
            fill();
            std::snprintf(n, sizeof n, "%d%c", t + 1, own ? 'O' : 'M');
            fillColor(own ? Color(20, 20, 20) : Color(220, 220, 228));
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(b.x + b.w / 2, b.y + b.h / 2, n, nullptr);
        }
        fontSize(14);
        fillColor(150, 150, 160);
        textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
        text(W - 20, 40, view.playing ? "BLUE HARE  PLAYING" : "BLUE HARE", nullptr);
    }

    void drawKnob(int i)
    {
        const Box b = knobBox(i);
        const float cx = b.x + b.w / 2, cy = b.y + 30, r = 26;
        const float a = i ? (float)knobPos[i] * (float)M_PI / 12.0f : (master / 1023.0f - 0.5f) * 1.5f * (float)M_PI;
        beginPath();
        circle(cx, cy, r + 3);
        fillColor(i == hot || i == dragKnob ? Color(120, 120, 132) : Color(90, 90, 100));
        fill();
        beginPath();
        circle(cx, cy, r);
        fillColor(44, 44, 50);
        fill();
        beginPath();
        moveTo(cx, cy);
        lineTo(cx + std::sin(a) * (r - 3), cy - std::cos(a) * (r - 3));
        strokeColor(240, 236, 220);
        strokeWidth(3);
        stroke();
        fontSize(13);
        fillColor(200, 200, 210);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(cx, b.y + b.h - 10, KNOB_NAME[i], nullptr);
    }

    void drawButton(int i)
    {
        const Box b = btnBox(i);
        const bool on = (view.lit_buttons >> i) & 1u, lo = ((view.dim_buttons | view.breath_buttons) >> i) & 1u;
        beginPath();
        roundedRect(b.x, b.y, b.w, b.h, 4);
        if (i == 10 && ((view.lit_buttons >> 14) & 1u))
            fillColor(80, 220, 120);                     /* (PLAY's green) */
        else if (on)
            fillColor(255, 236, 200);
        else if (lo)
            fillColor(130, 120, 100);
        else
            fillColor(58, 58, 66);
        fill();
        fontSize(14);
        fillColor(on ? Color(20, 20, 20) : Color(220, 220, 228));
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        text(b.x + b.w / 2, b.y + b.h / 2, BTN_NAME[i], nullptr);
    }

    void drawKeys()
    {
        for (int pass = 0; pass < 2; pass++)
            for (int k = 0; k < (pass ? 11 : 16); k++) {
                const int key = pass ? BLACK[k] : WHITE[k];
                const bool on = (view.lit_keys >> key) & 1u;
                const bool lo = ((view.dim_keys | view.breath_keys | view.mid_keys) >> key) & 1u;
                const Box b = keyBox(key);
                beginPath();
                rect(b.x, b.y, b.w, b.h);
                if (on)
                    fillColor(255, 236, 200);
                else if (lo)
                    fillColor(pass ? Color(110, 100, 80) : Color(238, 226, 196));
                else
                    fillColor(pass ? Color(30, 30, 34) : Color(210, 210, 214));
                fill();
            }
    }

    void drawMenu()
    {
        for (uint32_t i = 0; i < fel_host_dists(host); i++) {
            beginPath();
            rect(SX, 64.0f + (float)i * 34.0f, 240.0f, 32.0f);
            fillColor((int)i == dist ? Color(80, 80, 92) : Color(40, 40, 46));
            fill();
            fontSize(16);
            fillColor(230, 230, 236);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            text(SX + 12, 80.0f + (float)i * 34.0f, fel_host_dist_tag(host, i), nullptr);
        }
    }

    void drawAsk()
    {
        char line[160];
        beginPath();
        rect(0, 0, W, H);
        fillColor(Color(0, 0, 0, 0.6f));
        fill();
        beginPath();
        roundedRect(260, 200, 480, 190, 8);
        fillColor(44, 44, 50);
        fill();
        fontSize(18);
        fillColor(240, 240, 244);
        textAlign(ALIGN_LEFT | ALIGN_TOP);
        std::snprintf(line, sizeof line, "Switch to %s?", fel_host_dist_tag(host, (uint32_t)ask));
        text(284, 222, line, nullptr);
        fontSize(14);
        fillColor(190, 190, 200);
        text(284, 256, "The song goes with it. Sounds of engines it does not", nullptr);
        text(284, 276, "have play on the nearest engine it has.", nullptr);
        for (int b = 0; b < 2; b++) {
            beginPath();
            roundedRect(b ? 520.0f : 340.0f, 330, 140, 40, 6);
            fillColor(b ? Color(70, 70, 80) : Color(80, 200, 220));
            fill();
            fillColor(b ? Color(230, 230, 236) : Color(20, 20, 20));
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            text(b ? 590.0f : 410.0f, 350, b ? "CANCEL" : "SWITCH", nullptr);
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BlueHareUI)
};

UI* createUI() { return new BlueHareUI(); }

END_NAMESPACE_DISTRHO
