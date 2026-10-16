#pragma once
#include <SDL2/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

struct Color {
    uint8_t r, g, b, a;
};

namespace ui {
enum Font { Small = 0, Normal = 1, Big = 2, Huge = 3 };

bool init();
void shutdown();

void background();
void present();
void fillRect(int x, int y, int w, int h, Color c);
void fillRound(int x, int y, int w, int h, int r, Color c);

void text(const std::string& s, int x, int y, Color c, Font f = Normal);
void textRight(const std::string& s, int xRight, int y, Color c, Font f = Normal);
void textCenter(const std::string& s, int xCenter, int y, Color c, Font f = Normal);
int textWidth(const std::string& s, Font f = Normal);
std::string fit(const std::string& s, int maxW, Font f = Normal);                  // accorcia con "..."
std::vector<std::string> wrap(const std::string& s, int maxW, Font f = Normal);    // va a capo (rispetta \n)

SDL_Texture* textureFromImage(const uint8_t* data, size_t len);
void drawTexture(SDL_Texture* t, int x, int y, int w, int h);
int hint(int x, int y, const std::string& button, const std::string& label);  // ritorna la x successiva
}  // namespace ui
