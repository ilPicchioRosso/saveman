#include "ui.h"

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <switch.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace ui {
namespace {
SDL_Window* g_win = nullptr;
SDL_Renderer* g_ren = nullptr;
TTF_Font* g_font[4] = {nullptr, nullptr, nullptr, nullptr};
const int kSizes[4] = {18, 24, 34, 54};

struct Cached {
    SDL_Texture* tex;
    int w, h;
};
std::unordered_map<std::string, Cached> g_cache;

void clearCache() {
    for (auto& kv : g_cache) SDL_DestroyTexture(kv.second.tex);
    g_cache.clear();
}

Cached getText(const std::string& s, Color c, Font f) {
    char k[16];
    snprintf(k, sizeof k, "%d|%02x%02x%02x|", (int)f, c.r, c.g, c.b);
    std::string key = std::string(k) + s;
    auto it = g_cache.find(key);
    if (it != g_cache.end()) return it->second;
    if (g_cache.size() > 300) clearCache();

    Cached out{nullptr, 0, 0};
    if (!g_font[f] || s.empty()) return out;
    SDL_Color sc = {c.r, c.g, c.b, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(g_font[f], s.c_str(), sc);
    if (!surf) return out;
    out.tex = SDL_CreateTextureFromSurface(g_ren, surf);
    out.w = surf->w;
    out.h = surf->h;
    SDL_FreeSurface(surf);
    if (out.tex) g_cache[key] = out;
    return out;
}

void setColor(Color c) {
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(g_ren, c.r, c.g, c.b, c.a);
}

void popUtf8(std::string& s) {
    if (s.empty()) return;
    s.pop_back();
    while (!s.empty() && ((unsigned char)s.back() & 0xC0) == 0x80) s.pop_back();
    if (!s.empty() && ((unsigned char)s.back() & 0x80)) s.pop_back();  // byte di testa orfano
}
}  // namespace

bool init() {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;
    if (TTF_Init() != 0) return false;
    IMG_Init(IMG_INIT_JPG | IMG_INIT_PNG);

    g_win = SDL_CreateWindow("saveman", 0, 0, 1280, 720, SDL_WINDOW_SHOWN);
    if (!g_win) return false;
    g_ren = SDL_CreateRenderer(g_win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) return false;

    PlFontData fd;
    if (R_FAILED(plGetSharedFontByType(&fd, PlSharedFontType_Standard))) return false;
    for (int i = 0; i < 4; i++) g_font[i] = TTF_OpenFontRW(SDL_RWFromMem(fd.address, fd.size), 0, kSizes[i]);
    return g_font[0] && g_font[1] && g_font[2] && g_font[3];
}

void shutdown() {
    clearCache();
    for (auto& f : g_font) if (f) { TTF_CloseFont(f); f = nullptr; }
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
}

void background() {
    for (int y = 0; y < 720; y += 4) {
        float t = y / 720.0f;
        Color c = {(uint8_t)(18 - 10 * t), (uint8_t)(21 - 12 * t), (uint8_t)(40 - 22 * t), 255};
        fillRect(0, y, 1280, 4, c);
    }
}

void present() { SDL_RenderPresent(g_ren); }

void fillRect(int x, int y, int w, int h, Color c) {
    setColor(c);
    SDL_Rect r = {x, y, w, h};
    SDL_RenderFillRect(g_ren, &r);
}

void fillRound(int x, int y, int w, int h, int r, Color c) {
    if (r * 2 > h) r = h / 2;
    if (r * 2 > w) r = w / 2;
    setColor(c);
    SDL_Rect mid = {x, y + r, w, h - 2 * r};
    SDL_RenderFillRect(g_ren, &mid);
    for (int i = 0; i < r; i++) {
        float dy = r - i - 0.5f;
        int dx = r - (int)std::sqrt((float)(r * r) - dy * dy);
        SDL_Rect top = {x + dx, y + i, w - 2 * dx, 1};
        SDL_Rect bot = {x + dx, y + h - 1 - i, w - 2 * dx, 1};
        SDL_RenderFillRect(g_ren, &top);
        SDL_RenderFillRect(g_ren, &bot);
    }
}

void text(const std::string& s, int x, int y, Color c, Font f) {
    Cached t = getText(s, c, f);
    if (!t.tex) return;
    SDL_Rect d = {x, y, t.w, t.h};
    SDL_RenderCopy(g_ren, t.tex, nullptr, &d);
}

int textWidth(const std::string& s, Font f) {
    int w = 0, h = 0;
    if (g_font[f] && !s.empty()) TTF_SizeUTF8(g_font[f], s.c_str(), &w, &h);
    return w;
}

void textRight(const std::string& s, int xRight, int y, Color c, Font f) { text(s, xRight - textWidth(s, f), y, c, f); }
void textCenter(const std::string& s, int xCenter, int y, Color c, Font f) { text(s, xCenter - textWidth(s, f) / 2, y, c, f); }

std::string fit(const std::string& s, int maxW, Font f) {
    if (textWidth(s, f) <= maxW) return s;
    std::string o = s;
    while (!o.empty() && textWidth(o + "...", f) > maxW) popUtf8(o);
    return o + "...";
}

std::vector<std::string> wrap(const std::string& s, int maxW, Font f) {
    std::vector<std::string> lines;
    size_t pos = 0;
    while (pos <= s.size()) {
        size_t nl = s.find('\n', pos);
        std::string para = s.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        std::string cur;
        size_t i = 0;
        while (i < para.size()) {
            size_t sp = para.find(' ', i);
            std::string word = para.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
            std::string trial = cur.empty() ? word : cur + " " + word;
            if (!cur.empty() && textWidth(trial, f) > maxW) {
                lines.push_back(cur);
                cur = word;
            } else {
                cur = trial;
            }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(cur);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
    return lines;
}

SDL_Texture* textureFromImage(const uint8_t* data, size_t len) {
    SDL_Surface* s = IMG_Load_RW(SDL_RWFromConstMem(data, (int)len), 1);
    if (!s) return nullptr;
    SDL_Texture* t = SDL_CreateTextureFromSurface(g_ren, s);
    SDL_FreeSurface(s);
    return t;
}

void drawTexture(SDL_Texture* t, int x, int y, int w, int h) {
    if (!t) return;
    SDL_Rect d = {x, y, w, h};
    SDL_RenderCopy(g_ren, t, nullptr, &d);
}

int hint(int x, int y, const std::string& button, const std::string& label) {
    int bw = std::max(34, textWidth(button, Small) + 20);
    fillRound(x, y, bw, 34, 17, {58, 66, 110, 255});
    textCenter(button, x + bw / 2, y + 5, {236, 240, 255, 255}, Small);
    text(label, x + bw + 10, y + 3, {170, 178, 210, 255}, Normal);
    return x + bw + 10 + textWidth(label, Normal) + 34;
}
}  // namespace ui
