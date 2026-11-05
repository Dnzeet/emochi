// Emochi - emoji ala Dasai Mochi untuk LilyGo T-Display (ESP32, 240x135)
// Port dari emochi_final2.js (Bruce JS) + ekspresi tambahan
// Tombol: GPIO35 = colek (1-2x kaget, 3-4x marah, 5x+ pusing) | GPIO0 = ganti mood manual

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>

#define BTN_POKE 35
#define BTN_MOOD 0
#define ROTATION 1   // ubah ke 3 kalau tampilan terbalik

TFT_eSPI tft;
TFT_eSprite spr(&tft);

const uint16_t BG = TFT_BLACK;
const uint16_t FG = TFT_WHITE;

// ---- ID ekspresi ----
// 0 normal | 1 melirik | 3 kaget | 4 ngantuk | 5 sedih | 7 curiga | 9 tidur | 10 marah | 11 ketawa
// 12 jatuh cinta | 13 kedip sebelah | 14 nangis | 15 pusing | 16 bosan | 17 malu
// 18 mata bintang | 19 mati (X_X) | 20 takut | 21 mikir | 22 smirk | 23 tenang | 24 cium | 25 berbinar

const int NUM_MOODS = 6;
const int POOL_SIZE = 8;
// Mood: 0 normal, 1 happy, 2 sleepy, 3 grumpy, 4 malu/cinta, 5 sedih
const uint8_t POOL[NUM_MOODS][POOL_SIZE] = {
  {0, 1, 21, 16, 7, 13, 23, 0},
  {23, 12, 13, 25, 18, 11, 24, 0},
  {4, 9, 16, 4, 9, 5, 21, 9},
  {10, 22, 16, 7, 10, 5, 1, 16},
  {17, 12, 24, 13, 23, 17, 25, 0},
  {5, 14, 5, 21, 1, 16, 5, 14}
};

const int8_t SINE[14] = {0, 1, 2, 3, 3, 2, 1, 0, -1, -2, -3, -3, -2, -1};

int W, H, centerX, centerY, eyeL_X, eyeR_X;

int emojiState = 0, stateTimer = 0, loopCounter = 0;
int sineIndex = 0, floatingY = 0, baseEyeY = 0, mouthY = 0;
int lookDir = 0;
int currentMood = 0, moodTimer = 0;
const int moodDuration = 500;
int blinkPhase = 0, blinkTimer = 0;
const int blinkCooldown = 80;
int reactionTimer = 0, pressCount = 0, pressDecay = 0;
int zOffset = 0, zCounter = 0;

bool prevPoke = true, prevMood = true;
uint32_t lastFrame = 0;

// ================= helper gambar =================

// pil vertikal: persegi + lingkaran atas & bawah
static void tall(int cx, int y1, int y2) {
  spr.fillRect(cx - 17, y1, 34, y2 - y1, FG);
  spr.fillCircle(cx, y1, 17, FG);
  spr.fillCircle(cx, y2, 17, FG);
}

// pil horizontal: persegi + lingkaran kiri & kanan
static void wide(int cx, int ry, int rh, int cy) {
  spr.fillRect(cx - 17, ry, 34, rh, FG);
  spr.fillCircle(cx - 17, cy, 8, FG);
  spr.fillCircle(cx + 17, cy, 8, FG);
}

// garis tebal dengan ujung bulat
static void thickLine(int x0, int y0, int x1, int y1, int t) {
  float dx = x1 - x0, dy = y1 - y0;
  float len = sqrtf(dx * dx + dy * dy);
  if (len > 0.5f) {
    float nx = -dy / len * t / 2.0f, ny = dx / len * t / 2.0f;
    spr.fillTriangle(x0 + nx, y0 + ny, x0 - nx, y0 - ny, x1 + nx, y1 + ny, FG);
    spr.fillTriangle(x0 - nx, y0 - ny, x1 - nx, y1 - ny, x1 + nx, y1 + ny, FG);
  }
  spr.fillCircle(x0, y0, t / 2, FG);
  spr.fillCircle(x1, y1, t / 2, FG);
}

// cincin penuh
static void ring(int cx, int cy, int r, int th) {
  spr.fillCircle(cx, cy, r, FG);
  spr.fillCircle(cx, cy, r - th, BG);
}

// busur atas (bentuk ∩)
static void arch(int cx, int cy, int r, int th) {
  ring(cx, cy, r, th);
  spr.fillRect(cx - r - 1, cy + 1, 2 * r + 3, r + 2, BG);
}

// busur bawah (bentuk ∪)
static void smile(int cx, int cy, int r, int th) {
  ring(cx, cy, r, th);
  spr.fillRect(cx - r - 1, cy - r - 1, 2 * r + 3, r + 1, BG);
}

// mulut terbuka bentuk D (setengah lingkaran bawah)
static void bigD(int cx, int cy, int r) {
  spr.fillCircle(cx, cy, r, FG);
  spr.fillRect(cx - r - 1, cy - r - 1, 2 * r + 3, r + 1, BG);
}

static void heart(int cx, int cy, int r) {
  spr.fillCircle(cx - r, cy, r, FG);
  spr.fillCircle(cx + r, cy, r, FG);
  int ex = (int)(1.707f * r);
  int ey = (int)(0.707f * r);
  spr.fillRect(cx - r, cy, 2 * r + 1, ey + 1, FG);
  spr.fillTriangle(cx - ex, cy + ey, cx + ex, cy + ey, cx, cy + (int)(2.414f * r), FG);
}

static void star(int cx, int cy, int s, uint16_t c) {
  int k = (int)(s * 0.32f) + 1;
  spr.fillTriangle(cx, cy - s, cx + k, cy - k, cx - k, cy - k, c);
  spr.fillTriangle(cx + s, cy, cx + k, cy - k, cx + k, cy + k, c);
  spr.fillTriangle(cx, cy + s, cx + k, cy + k, cx - k, cy + k, c);
  spr.fillTriangle(cx - s, cy, cx - k, cy - k, cx - k, cy + k, c);
  spr.fillRect(cx - k, cy - k, 2 * k + 1, 2 * k + 1, c);
}

static void wavy(int cx, int y, int w, int amp, int segs) {
  int px = cx - w / 2, py = y - amp;
  for (int i = 1; i <= segs; i++) {
    int x = cx - w / 2 + (w * i) / segs;
    int yy = y + ((i % 2) ? amp : -amp);
    thickLine(px, py, x, yy, 4);
    px = x; py = yy;
  }
}

static void spiral(int cx, int cy, int phase) {
  int px = cx, py = cy;
  for (int i = 1; i < 38; i++) {
    float a = i * 0.55f + phase * 0.35f;
    float r = i * 0.47f;
    int x = cx + (int)(cosf(a) * r), y = cy + (int)(sinf(a) * r);
    thickLine(px, py, x, y, 3);
    px = x; py = y;
  }
}

static void drop(int x, int y) {
  spr.fillCircle(x, y, 5, FG);
  spr.fillTriangle(x - 5, y - 1, x + 5, y - 1, x, y - 13, FG);
}

static bool canBlink(int s) {
  return s == 0 || s == 1 || s == 5 || s == 7 || s == 16 || s == 21 || s == 22 || s == 25;
}

// ================= render =================

void render() {
  spr.fillSprite(BG);
  int by = baseEyeY, my = mouthY;
  int cx = centerX, L = eyeL_X, R = eyeR_X;
  int p = SINE[sineIndex];                 // -3..3 untuk denyut

  if (blinkPhase == 1 || blinkPhase == 3) {
    wide(L, by - 4, 18, by + 14);
    wide(R, by - 4, 18, by + 14);
    spr.fillRect(cx - 6, my, 12, 5, FG);

  } else if (blinkPhase == 2) {
    spr.fillRect(L - 17, by, 34, 6, FG);
    spr.fillRect(R - 17, by, 34, 6, FG);
    spr.fillRect(cx - 6, my, 12, 5, FG);

  } else switch (emojiState) {

    case 0:                                   // normal
      tall(L, by - 22, by + 22);
      tall(R, by - 22, by + 22);
      spr.fillRect(cx - 9, my, 18, 6, FG);
      spr.fillRect(cx - 13, my - 4, 5, 5, FG);
      spr.fillRect(cx + 8, my - 4, 5, 5, FG);
      break;

    case 1: {                                 // melirik
      int lx = (lookDir == 0) ? -10 : 10;
      tall(L + lx, by - 22, by + 22);
      tall(R + lx, by - 22, by + 22);
      spr.fillRect(cx - 6, my, 12, 6, FG);
      break;
    }

    case 3:                                   // kaget
      spr.fillCircle(L, by, 20, FG);
      spr.fillCircle(L, by, 10, BG);
      spr.fillCircle(R, by, 20, FG);
      spr.fillCircle(R, by, 10, BG);
      spr.fillCircle(cx, my + 4, 8, FG);
      spr.fillCircle(cx, my + 4, 4, BG);
      break;

    case 4:                                   // mengantuk
      spr.fillRect(L - 17, by, 34, 26, FG);
      spr.fillCircle(L, by + 26, 17, FG);
      spr.fillRect(R - 17, by, 34, 26, FG);
      spr.fillCircle(R, by + 26, 17, FG);
      spr.fillRect(cx - 9, my + 2, 18, 5, FG);
      break;

    case 5:                                   // sedih
      tall(L, by - 22, by + 33);
      tall(R, by - 22, by + 33);
      spr.fillRect(cx - 9, my + 6, 18, 5, FG);
      spr.fillRect(cx - 9, my + 1, 5, 5, FG);
      spr.fillRect(cx + 4, my + 1, 5, 5, FG);
      break;

    case 7:                                   // curiga
      tall(L, by - 22, by + 22);
      tall(R, by - 36, by + 8);
      spr.fillRect(cx - 8, my + 2, 16, 5, FG);
      break;

    case 9: {                                 // tidur zzz
      spr.fillRect(L - 11, by + 2, 22, 4, FG);
      spr.fillRect(R - 11, by + 2, 22, 4, FG);
      spr.fillRect(cx - 4, my + 3, 8, 3, FG);
      int z = zOffset;
      spr.fillRect(cx + 14, by - 18 - z, 8, 2, FG);
      spr.fillRect(cx + 18, by - 16 - z, 2, 2, FG);
      spr.fillRect(cx + 16, by - 14 - z, 2, 2, FG);
      spr.fillRect(cx + 14, by - 12 - z, 8, 2, FG);
      spr.fillRect(cx + 20, by - 30 - z, 10, 2, FG);
      spr.fillRect(cx + 25, by - 28 - z, 2, 3, FG);
      spr.fillRect(cx + 23, by - 25 - z, 2, 3, FG);
      spr.fillRect(cx + 20, by - 22 - z, 10, 2, FG);
      spr.fillRect(cx + 10, by - 46 - z, 13, 2, FG);
      spr.fillRect(cx + 17, by - 44 - z, 2, 4, FG);
      spr.fillRect(cx + 14, by - 40 - z, 2, 4, FG);
      spr.fillRect(cx + 10, by - 36 - z, 13, 2, FG);
      break;
    }

    case 10:                                  // marah
      wide(L, by - 8, 16, by);
      wide(R, by - 8, 16, by);
      spr.fillRect(L - 17, by - 20, 12, 4, FG);
      spr.fillRect(L - 6, by - 17, 12, 4, FG);
      spr.fillRect(L + 5, by - 14, 12, 4, FG);
      spr.fillRect(R - 17, by - 14, 12, 4, FG);
      spr.fillRect(R - 6, by - 17, 12, 4, FG);
      spr.fillRect(R + 5, by - 20, 12, 4, FG);
      spr.fillRect(cx - 9, my, 18, 5, FG);
      spr.fillRect(cx - 13, my + 2, 5, 5, FG);
      spr.fillRect(cx + 8, my + 2, 5, 5, FG);
      break;

    case 11: {                                // ketawa (badan bergetar)
      int j = (loopCounter % 2) ? 2 : -1;
      arch(L, by + 6 + j, 19, 7);
      arch(R, by + 6 + j, 19, 7);
      bigD(cx, my - 14 + j, 17);
      break;
    }

    case 12: {                                // jatuh cinta
      int r = 11 + p / 2;
      heart(L, by - 8, r);
      heart(R, by - 8, r);
      smile(cx, my - 8, 9, 4);
      break;
    }

    case 13:                                  // kedip sebelah
      arch(L, by + 8, 18, 6);
      tall(R, by - 22, by + 22);
      smile(cx, my - 12, 13, 5);
      break;

    case 14: {                                // nangis
      tall(L, by - 10, by + 12);
      tall(R, by - 10, by + 12);
      thickLine(L - 16, by - 36, L + 14, by - 44, 4);
      thickLine(R - 14, by - 44, R + 16, by - 36, 4);
      int t1 = (loopCounter * 2) % 40;
      int t2 = (loopCounter * 2 + 20) % 40;
      drop(L, by + 24 + t1);
      drop(R, by + 24 + t2);
      int w = (loopCounter % 4 < 2) ? 0 : 1;
      arch(cx, my + 10 + w, 12, 5);
      break;
    }

    case 15:                                  // pusing
      spiral(L, by, loopCounter);
      spiral(R, by, loopCounter);
      wavy(cx, my + 2, 30, 4, 5);
      break;

    case 16: {                                // bosan
      int px = lookDir ? 7 : -7;
      spr.fillCircle(L, by, 19, FG);
      spr.fillRect(L - 20, by - 20, 40, 18, BG);
      spr.fillCircle(L + px, by + 8, 6, BG);
      spr.fillCircle(R, by, 19, FG);
      spr.fillRect(R - 20, by - 20, 40, 18, BG);
      spr.fillCircle(R + px, by + 8, 6, BG);
      spr.fillRect(cx - 10, my + 2, 20, 4, FG);
      break;
    }

    case 17:                                  // malu
      arch(L, by + 4, 17, 6);
      arch(R, by + 4, 17, 6);
      for (int i = 0; i < 3; i++) {
        thickLine(L - 12 + i * 9, by + 32, L - 6 + i * 9, by + 22, 3);
        thickLine(R - 12 + i * 9, by + 32, R - 6 + i * 9, by + 22, 3);
      }
      smile(cx, my - 6, 7, 3);
      break;

    case 18:                                  // mata bintang
      star(L, by, 22 + p / 2, FG);
      star(R, by, 22 + p / 2, FG);
      bigD(cx, my - 12, 14);
      break;

    case 19:                                  // mati X_X
      thickLine(L - 14, by - 14, L + 14, by + 14, 6);
      thickLine(L - 14, by + 14, L + 14, by - 14, 6);
      thickLine(R - 14, by - 14, R + 14, by + 14, 6);
      thickLine(R - 14, by + 14, R + 14, by - 14, 6);
      spr.fillRect(cx - 12, my, 24, 5, FG);
      spr.fillRect(cx - 5, my + 4, 11, 9, FG);
      spr.fillCircle(cx, my + 13, 6, FG);
      break;

    case 20: {                                // takut
      int dx = (loopCounter % 2) ? 2 : -2;
      int jx = (loopCounter % 3) - 1;
      ring(L + dx, by, 19, 6);
      ring(R + dx, by, 19, 6);
      spr.fillCircle(L + dx + jx, by, 4, FG);
      spr.fillCircle(R + dx + jx, by, 4, FG);
      wavy(cx + dx, my + 2, 28, 3, 4);
      drop(cx + 62 + dx, by - 28 + (loopCounter % 16));
      break;
    }

    case 21: {                                // mikir
      tall(L + 8, by - 30, by + 8);
      tall(R + 8, by - 30, by + 8);
      thickLine(cx + 2, my + 4, cx + 16, my - 2, 4);
      int n = (loopCounter / 8) % 4;
      for (int i = 0; i < n; i++) spr.fillCircle(cx + 62 + i * 11, by - 36, 3, FG);
      break;
    }

    case 22:                                  // smirk
      tall(L, by - 22, by + 22);
      tall(R, by - 6, by + 18);
      thickLine(cx - 12, my + 4, cx + 8, my + 4, 5);
      thickLine(cx + 8, my + 4, cx + 16, my - 4, 5);
      break;

    case 23:                                  // tenang
      smile(L, by - 4, 17, 6);
      smile(R, by - 4, 17, 6);
      smile(cx, my - 10, 12, 4);
      break;

    case 24: {                                // cium
      thickLine(L - 12, by - 14, L + 10, by, 6);
      thickLine(L + 10, by, L - 12, by + 14, 6);
      thickLine(R + 12, by - 14, R - 10, by, 6);
      thickLine(R - 10, by, R + 12, by + 14, 6);
      ring(cx, my + 2, 8, 4);
      int hy = my - 12 - ((loopCounter * 2) % 56);
      heart(cx + SINE[(loopCounter / 2) % 14], hy, 5);
      break;
    }

    case 25: {                                // berbinar
      tall(L, by - 22, by + 22);
      tall(R, by - 22, by + 22);
      star(L - 5, by - 10, 8 + (p > 0 ? 1 : 0), BG);
      star(R - 5, by - 10, 8 + (p > 0 ? 1 : 0), BG);
      smile(cx, my - 8, 10, 4);
      break;
    }
  }

  spr.pushSprite(0, 0);
}

void setup() {
  pinMode(BTN_POKE, INPUT);        // GPIO35: input only, pullup eksternal di board
  pinMode(BTN_MOOD, INPUT_PULLUP);

  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(BG);

  W = tft.width();
  H = tft.height();
  centerX = W / 2;
  centerY = H / 2;
  eyeL_X = centerX - 32;
  eyeR_X = centerX + 32;

  if (!spr.createSprite(W, H)) {
    tft.setTextColor(FG, BG);
    tft.drawString("Sprite gagal (RAM)", 10, 10, 2);
    while (true) delay(1000);
  }
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < 40) return;   // ~25 fps
  lastFrame = now;

  // --- tombol (deteksi tekan) ---
  bool poke = digitalRead(BTN_POKE);
  bool moodBtn = digitalRead(BTN_MOOD);

  if (!poke && prevPoke) {
    pressCount++;
    pressDecay = 60;
    stateTimer = 0;
    if (pressCount >= 5)      { emojiState = 15; reactionTimer = 80; }
    else if (pressCount >= 3) { emojiState = 10; reactionTimer = 60; }
    else                      { emojiState = 3;  reactionTimer = 50; }
  }
  if (!moodBtn && prevMood) {
    currentMood = (currentMood + 1) % NUM_MOODS;
    moodTimer = 0;
    stateTimer = 100;   // langsung ganti ekspresi sesuai mood baru
  }
  prevPoke = poke;
  prevMood = moodBtn;

  loopCounter++;
  stateTimer++;
  blinkTimer++;
  moodTimer++;

  if (pressDecay > 0) pressDecay--; else pressCount = 0;

  // --- mood otomatis ---
  if (moodTimer > moodDuration) {
    moodTimer = 0;
    currentMood = random(0, NUM_MOODS);
  }

  // --- ganti ekspresi idle ---
  if (reactionTimer > 0) {
    reactionTimer--;
  } else if (stateTimer > 75) {
    stateTimer = 0;
    int next = emojiState;
    for (int t = 0; t < 4 && next == emojiState; t++) {
      next = POOL[currentMood][random(0, POOL_SIZE)];
    }
    emojiState = next;
  }

  // --- kedip (hanya ekspresi bermata terbuka) ---
  if (reactionTimer == 0 && canBlink(emojiState)) {
    if (blinkPhase == 0 && blinkTimer > blinkCooldown) {
      if (random(1, 5) == 1) { blinkPhase = 1; blinkTimer = 0; }
    } else if (blinkPhase == 1 && blinkTimer > 3) { blinkPhase = 2; blinkTimer = 0; }
    else if (blinkPhase == 2 && blinkTimer > 4)   { blinkPhase = 3; blinkTimer = 0; }
    else if (blinkPhase == 3 && blinkTimer > 3)   { blinkPhase = 0; blinkTimer = 0; }
  } else {
    blinkPhase = 0; blinkTimer = 0;
  }

  // --- zzz ---
  if (++zCounter >= 3) { zOffset = (zOffset + 1) % 28; zCounter = 0; }

  // --- melayang (sine) ---
  if (loopCounter % 2 == 0) sineIndex = (sineIndex + 1) % 14;
  floatingY = SINE[sineIndex];
  baseEyeY = centerY + 1 + floatingY;
  mouthY = centerY + 37 + floatingY;

  // --- melirik ---
  if (loopCounter % 100 == 0) lookDir = 1 - lookDir;

  render();
}
