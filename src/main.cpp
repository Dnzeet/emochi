// Emochi - emoji ala Dasai Mochi untuk LilyGo T-Display (ESP32, 240x135)
// Port dari emochi_final2.js (Bruce JS) + ekspresi tambahan
// Tombol atas (GPIO35): klik = info baterai | dobel klik = power off | klik lagi = nyala | tahan = atur kecerahan
// Tombol bawah (GPIO0): klik = colek (1-2x bingung/senang, 3-4x marah, 5x+ pusing) | tahan = ganti mood
// Mode kecerahan: tombol atas = terang (+), tombol bawah = redup (-), otomatis tersimpan

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <esp_sleep.h>
#include <Preferences.h>

#define BTN_POWER 35   // tombol atas
#define BTN_POKE  0    // tombol bawah
#define BL_CH      0   // kanal PWM backlight
#define BAT_PIN   34   // ADC baterai (pembagi 2:1)
#define BAT_EN    14   // enable pengukuran baterai (diabaikan kalau tidak terpasang di board)
#define ROTATION 1   // ubah ke 3 kalau tampilan terbalik

TFT_eSPI tft;
TFT_eSprite spr(&tft);

const uint16_t BG = TFT_BLACK;
const uint16_t FG = TFT_WHITE;
const uint16_t RED = 0xF800;
const uint16_t YEL = 0xFFE0;
const uint16_t GRAY = 0x8C71;

// ---- ID ekspresi ----
// 0 normal | 1 melirik | 4 ngantuk | 5 sedih | 7 curiga | 9 tidur | 10 marah | 11 ketawa
// 12 jatuh cinta | 13 kedip sebelah | 14 nangis | 15 pusing | 16 bosan | 17 malu
// 18 mata bintang | 19 mati (X_X) | 20 takut | 21 mikir | 22 smirk | 23 tenang | 24 cium | 25 berbinar
// 26 bingung (reaksi colek 1-2x)
// 27 lemas | 28 minta cas (baterai besar merah) | 29 tunjuk colokan  <- varian baterai hampir habis

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
int blinkPhase = 0, blinkTimer = 0;
const int blinkCooldown = 80;
int reactionTimer = 0, pressCount = 0, pressDecay = 0;
int zOffset = 0, zCounter = 0;

uint32_t lastFrame = 0;

// interaksi ala pet: perhatian 0..100, turun pelan, naik saat dicolek
int attention = 75, attnTimer = 0, moodHold = 0;

// tombol
bool prevTop = true, prevBot = true;
uint32_t lastTopPress = 0, botDownAt = 0;
bool haveTop = false, longDone = false;

// kecerahan (1..10 = 10%..100%), tersimpan di NVS
Preferences prefs;
uint8_t brightness = 10;
bool setMode = false, brightDirty = false, topLongDone = false;
uint32_t setUntil = 0, topDownAt = 0;

// baterai
int vbat = 0, battPct = 0, critCount = 0;
bool battValid = false, lowBattery = false, battShowOn = false;
uint32_t battShowUntil = 0;

// transisi antar ekspresi (wajah menciut ke garis mata lalu membuka lagi)
bool trans = false;
int transFrame = 0, pendingState = 0;
const uint8_t TRANS_PCT[5] = {75, 45, 20, 45, 75};

// ================= helper gambar =================

// skala tinggi mata saat kedip (100 = normal)
static int blinkPct = 100;

// pil vertikal: persegi + lingkaran atas & bawah (mengecil saat kedip)
static void tall(int cx, int y1, int y2) {
  if (blinkPct < 100) {
    int tot = (y2 - y1) + 34;
    int nt = tot * blinkPct / 100;
    if (nt < 34) nt = 34;
    int cy = (y1 + y2) / 2;
    int len = nt - 34;
    y1 = cy - len / 2;
    y2 = y1 + len;
  }
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

// tanda tanya; r = radius lengkung
static void qmark(int x, int y, int r) {
  int th = r / 2;
  if (th < 3) th = 3;
  ring(x, y, r, th);
  spr.fillRect(x - r - 1, y + 1, r + 1, r + 2, BG);   // buang kuadran kiri-bawah
  thickLine(x + r - th / 2, y + 2, x, y + r + 4, th);
  thickLine(x, y + r + 4, x, y + r + 8, th);
  spr.fillCircle(x, y + r + 15, th / 2 + 1, FG);
}

// ikon baterai ala iOS: outline abu, isi membulat, kepala setengah bulat
static void batteryIcon(int x, int y, int w, int h, int pct, uint16_t col) {
  int r = h / 3;
  int cy = y + h / 2;
  spr.fillCircle(x + w, cy, h / 5 + 1, GRAY);                 // kepala baterai
  spr.fillRoundRect(x, y, w, h, r, GRAY);                     // outline
  spr.fillRoundRect(x + 2, y + 2, w - 4, h - 4, r > 2 ? r - 2 : 1, BG);
  int iw = ((w - 8) * pct) / 100;
  if (pct > 0 && iw < 3) iw = 3;
  if (iw > 0) spr.fillRoundRect(x + 4, y + 4, iw, h - 8, r > 4 ? r - 4 : 1, col);
}

// petir
static void bolt(int cx, int cy, int s, uint16_t c) {
  spr.fillTriangle(cx + s / 4, cy - s, cx - s / 2, cy + s / 8, cx + s / 10, cy + s / 8, c);
  spr.fillTriangle(cx - s / 10, cy - s / 8, cx + s / 2, cy - s / 8, cx - s / 4, cy + s, c);
}

static bool canBlink(int s) {
  return s == 0 || s == 1 || s == 5 || s == 7 || s == 16 || s == 21 || s == 22 || s == 25;
}

// ================= render =================

static void drawFace() {
  spr.fillSprite(BG);
  int by = baseEyeY, my = mouthY;
  int cx = centerX, L = eyeL_X, R = eyeR_X;
  int p = SINE[sineIndex];                 // -3..3 untuk denyut

  blinkPct = (blinkPhase == 1 || blinkPhase == 3) ? 85 : (blinkPhase == 2 ? 70 : 100);

  switch (emojiState) {

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

    case 26: {                                // bingung (kepala miring + tanda tanya)
      int t = ((loopCounter / 14) % 2) ? 4 : -4;
      tall(L + 5, by - 14 - t, by + 10 - t);
      tall(R + 5, by - 14 + t, by + 10 + t);
      wavy(cx, my + 4, 20, 3, 3);
      qmark(cx + 76 + SINE[(loopCounter / 2) % 14], by - 34 + p, 12);
      if ((loopCounter / 10) % 2 == 0) qmark(cx - 78, by - 24 - p, 8);
      break;
    }

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
      int lid = 18 + (100 - blinkPct) * 20 / 100;
      spr.fillCircle(L, by, 19, FG);
      spr.fillRect(L - 20, by - 20, 40, lid, BG);
      spr.fillCircle(L + px, by + 8, 6, BG);
      spr.fillCircle(R, by, 19, FG);
      spr.fillRect(R - 20, by - 20, 40, lid, BG);
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

    case 27: {                                // lemas (baterai hampir habis)
      int w = (loopCounter % 6 < 3) ? 0 : 1;
      spr.fillCircle(L, by + 14, 17, FG);
      spr.fillRect(L - 18, by - 4, 36, 18, BG);
      spr.fillCircle(R, by + 14, 17, FG);
      spr.fillRect(R - 18, by - 4, 36, 18, BG);
      arch(cx, my + 12 + w, 10, 4);
      break;
    }

    case 28: {                                // minta cas: mata memelas + baterai merah besar
      int on = (loopCounter / 12) % 2;
      tall(L, by - 30, by - 16);
      tall(R, by - 30, by - 16);
      thickLine(L - 16, by - 52, L + 14, by - 60, 4);
      thickLine(R - 14, by - 60, R + 16, by - 52, 4);
      int bw = 76, bh = 38, bx = cx - bw / 2 - 4, bY = by + 16 + (p > 0 ? 1 : 0);
      batteryIcon(bx, bY, bw, bh, battValid ? battPct : 5, RED);
      if (on) bolt(bx + bw / 2, bY + bh / 2, 13, YEL);
      break;
    }

    case 29: {                                // tunjuk colokan
      int dx = -30;
      int nudge = ((loopCounter / 6) % 2) ? 5 : 0;
      tall(L + dx, by - 10, by + 12);
      tall(R + dx, by - 10, by + 12);
      thickLine(L + dx - 16, by - 36, L + dx + 14, by - 44, 4);
      thickLine(R + dx - 14, by - 44, R + dx + 16, by - 36, 4);
      arch(cx + dx, my + 12, 10, 4);
      int px = cx + 50 - nudge, py = by - 4;
      spr.fillRoundRect(px, py, 30, 24, 6, FG);              // badan colokan
      spr.fillRect(px - 9, py + 4, 9, 4, FG);                // kaki
      spr.fillRect(px - 9, py + 16, 9, 4, FG);
      thickLine(px + 30, py + 12, px + 52, py + 12, 5);      // kabel
      if ((loopCounter / 10) % 2 == 0) bolt(px + 15, py - 16, 9, YEL);
      break;
    }
  }

}

// persempit wajah secara vertikal terhadap baris c (in-place, tanpa RAM tambahan)
static void squashRows(int c, int pct) {
  uint16_t* b = (uint16_t*)spr.getPointer();
  if (!b || pct >= 100) return;
  for (int d = 0; d <= c; d++) {            // bagian atas
    int dst = c - d, src = c - (d * 100) / pct;
    uint16_t* dp = b + dst * W;
    if (src < 0) memset(dp, 0, W * 2);
    else if (src != dst) memcpy(dp, b + src * W, W * 2);
  }
  for (int d = 1; c + d < H; d++) {         // bagian bawah
    int dst = c + d, src = c + (d * 100) / pct;
    uint16_t* dp = b + dst * W;
    if (src >= H) memset(dp, 0, W * 2);
    else memcpy(dp, b + src * W, W * 2);
  }
}

static void drawBatteryOverlay(bool withText) {
  bool red = battValid && battPct <= 20;
  uint16_t col = red ? RED : FG;
  int x = W - 48, y = 5;
  batteryIcon(x, y, 38, 18, battValid ? battPct : 0, col);
  if (withText) {
    char b[8];
    if (battValid) snprintf(b, sizeof b, "%d%%", battPct);
    else snprintf(b, sizeof b, "--");
    spr.setTextColor(col, BG);
    spr.setTextSize(2);
    spr.drawString(b, x - 6 - (int)strlen(b) * 12, y + 1, 1);
  }
}

static void applyBrightness() {
  int pct = brightness * 10;
  int duty = (255 * pct * pct) / 10000;      // kurva kuadrat: terasa lebih merata di mata
  if (duty < 6) duty = 6;
  ledcWrite(BL_CH, duty);
}

static void drawBrightnessOverlay() {
  int px = 24, py = 26, pw = W - 48, ph = 84;
  spr.fillRoundRect(px, py, pw, ph, 10, GRAY);
  spr.fillRoundRect(px + 2, py + 2, pw - 4, ph - 4, 9, BG);
  char t[24];
  snprintf(t, sizeof t, "Kecerahan %d%%", brightness * 10);
  spr.setTextColor(FG, BG);
  spr.setTextSize(2);
  spr.drawString(t, px + (pw - (int)strlen(t) * 12) / 2, py + 10, 1);
  int segW = 14, gap = 3, total = 10 * segW + 9 * gap;
  int sx = px + (pw - total) / 2, sy = py + 34;
  for (int i = 0; i < 10; i++) {
    spr.fillRoundRect(sx + i * (segW + gap), sy, segW, 16, 3, i < brightness ? FG : (uint16_t)0x2104);
  }
  spr.setTextSize(1);
  const char* hint = "ATAS: terang    BAWAH: redup";
  spr.drawString(hint, px + (pw - (int)strlen(hint) * 6) / 2, py + 62, 1);
}

void render(int pct) {
  drawFace();
  if (pct < 100) squashRows(baseEyeY, pct);
  if (setMode) drawBrightnessOverlay();
  else if (battShowOn) drawBatteryOverlay(true);
  else if (lowBattery && emojiState != 28 && (loopCounter / 12) % 2 == 0) drawBatteryOverlay(false);
  spr.pushSprite(0, 0);
}

// ================= baterai =================

static int pctFromMv(int mv) {
  static const int T[9][2] = {{3300, 0}, {3500, 5}, {3600, 15}, {3700, 35}, {3800, 55},
                              {3900, 70}, {4000, 82}, {4100, 92}, {4200, 100}};
  if (mv <= T[0][0]) return 0;
  if (mv >= T[8][0]) return 100;
  for (int i = 1; i < 9; i++) {
    if (mv <= T[i][0]) {
      return T[i - 1][1] + (T[i][1] - T[i - 1][1]) * (mv - T[i - 1][0]) / (T[i][0] - T[i - 1][0]);
    }
  }
  return 100;
}

static void readBattery(bool first) {
  digitalWrite(BAT_EN, HIGH);
  delay(3);
  uint32_t sum = 0;
  for (int i = 0; i < 8; i++) { sum += analogReadMilliVolts(BAT_PIN); delay(1); }
  digitalWrite(BAT_EN, LOW);
  int mv = (int)(sum / 8) * 2;
  battValid = mv > 2800;                    // di bawah itu: tanpa baterai / tidak terbaca
  if (!battValid) { lowBattery = false; critCount = 0; return; }
  vbat = (first || vbat == 0) ? mv : (vbat * 7 + mv * 3) / 10;
  battPct = pctFromMv(vbat);
  if (!lowBattery && vbat < 3500) lowBattery = true;
  else if (lowBattery && vbat > 3600) lowBattery = false;
  if (vbat < 3300) critCount++; else critCount = 0;
}

// ================= power =================

static void powerOff() {
  // animasi tidur singkat
  emojiState = 9;
  blinkPhase = 0;
  battShowOn = false;
  for (int i = 0; i < 22; i++) {
    zOffset = (zOffset + 1) % 28;
    loopCounter++;
    render(100);
    delay(40);
  }
  while (digitalRead(BTN_POWER) == LOW) delay(10);   // tunggu tombol dilepas
  delay(80);
  ledcWrite(BL_CH, 0);
  ledcDetachPin(TFT_BL);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  tft.writecommand(TFT_DISPOFF);
  tft.writecommand(TFT_SLPIN);
  delay(120);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_35, 0);      // klik tombol atas = nyala
  esp_deep_sleep_start();
}

// ================= logika =================

static void changeState(int s) {
  if (trans && transFrame <= 2) { pendingState = s; return; }  // belum sempat ganti: ubah tujuan
  if (s == emojiState) return;
  pendingState = s;
  trans = true;
  transFrame = 0;
}

static void go(int s, int react) {
  reactionTimer = react;
  stateTimer = 0;
  changeState(s);
}

static int moodFromAttention() {
  if (attention >= 70) return random(0, 2) ? 1 : 4;
  if (attention >= 40) return 0;
  if (attention >= 20) return 3;
  if (attention >= 8) return 5;
  return 2;
}

static void doPoke() {
  pressCount++;
  pressDecay = 60;
  bool lonely = attention < 25;
  if (pressCount <= 2) {
    attention += lonely ? 25 : 8;
    if (attention > 100) attention = 100;
  }
  if (pressCount >= 5)      go(15, 80);
  else if (pressCount >= 3) go(10, 60);
  else if (lonely) {                        // senang disentuh lagi setelah lama sendirian
    static const uint8_t joy[3] = {12, 25, 18};
    go(joy[random(0, 3)], 70);
  }
  else go(26, 50);
}

void setup() {
  pinMode(BTN_POWER, INPUT);       // GPIO35: input only, pullup eksternal di board
  pinMode(BTN_POKE, INPUT_PULLUP);
  pinMode(BAT_EN, OUTPUT);
  digitalWrite(BAT_EN, LOW);

  tft.init();
  tft.setRotation(ROTATION);
  tft.fillScreen(BG);

  prefs.begin("emochi", false);
  int b = prefs.getUChar("br", 10);
  brightness = (b < 1) ? 1 : (b > 10 ? 10 : b);
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(TFT_BL, BL_CH);
  applyBrightness();

  W = tft.width();
  H = tft.height();
  centerX = W / 2;
  centerY = H / 2;
  eyeL_X = centerX - 32;
  eyeR_X = centerX + 32;
  baseEyeY = centerY + 1;
  mouthY = centerY + 37;

  if (!spr.createSprite(W, H)) {
    tft.setTextColor(FG, BG);
    tft.drawString("Sprite gagal (RAM)", 10, 10, 2);
    while (true) delay(1000);
  }

  prevTop = digitalRead(BTN_POWER);   // supaya klik yang membangunkan tidak terhitung
  prevBot = digitalRead(BTN_POKE);
  readBattery(true);
}

void loop() {
  uint32_t now = millis();
  if (now - lastFrame < 40) return;   // ~25 fps
  lastFrame = now;
  battShowOn = now < battShowUntil;

  bool top = digitalRead(BTN_POWER);
  bool bot = digitalRead(BTN_POKE);

  // --- tombol atas: klik = info baterai | dobel klik = power off | tahan = mode kecerahan ---
  if (!top && prevTop) {
    topDownAt = now;
    topLongDone = false;
    if (setMode) {                    // di mode kecerahan: terang (+)
      if (brightness < 10) brightness++;
      applyBrightness();
      brightDirty = true;
      setUntil = now + 4000;
    } else {
      if (haveTop && now - lastTopPress < 450) powerOff();
      lastTopPress = now;
      haveTop = true;
      battShowUntil = now + 3000;
      battShowOn = true;
    }
  }
  if (!top && !topLongDone && !setMode && now - topDownAt >= 800) {
    topLongDone = true;               // tahan tombol atas: masuk mode kecerahan
    setMode = true;
    setUntil = now + 4000;
    battShowUntil = 0;
    battShowOn = false;
  }
  prevTop = top;

  // --- tombol bawah: klik = colek | tahan = ganti mood | (mode kecerahan: redup) ---
  if (!bot && prevBot) {
    botDownAt = now;
    longDone = false;
    if (setMode) {
      if (brightness > 1) brightness--;
      applyBrightness();
      brightDirty = true;
      setUntil = now + 4000;
      longDone = true;                // cegah colek saat dilepas
    }
  }
  if (!setMode && !bot && !longDone && now - botDownAt >= 800) {
    longDone = true;
    currentMood = (currentMood + 1) % NUM_MOODS;
    moodHold = 1500;                  // mood manual bertahan ±60 detik
    stateTimer = 100;
  }
  if (bot && !prevBot && !longDone) doPoke();
  prevBot = bot;

  // --- keluar mode kecerahan setelah 4 detik tanpa sentuhan, simpan ---
  if (setMode && now > setUntil) {
    setMode = false;
    if (brightDirty) { prefs.putUChar("br", brightness); brightDirty = false; }
  }

  loopCounter++;
  stateTimer++;
  blinkTimer++;
  moodTimer++;

  if (pressDecay > 0) pressDecay--; else pressCount = 0;

  // --- perhatian turun pelan (±1 tiap 6 detik) ---
  if (++attnTimer >= 150) { attnTimer = 0; if (attention > 0) attention--; }

  // --- mood mengikuti perhatian (kecuali sedang diatur manual) ---
  if (moodHold > 0) moodHold--;
  else if (moodTimer > 150) { moodTimer = 0; currentMood = moodFromAttention(); }

  // --- baterai tiap ±8 detik ---
  if (loopCounter % 200 == 0) {
    readBattery(false);
    if (critCount >= 3) powerOff();   // lindungi baterai
  }

  // --- ganti ekspresi ---
  if (reactionTimer > 0) {
    reactionTimer--;
  } else if (lowBattery) {
    static const uint8_t LOW_POOL[6] = {27, 28, 29, 28, 27, 29};
    bool isLow = emojiState == 27 || emojiState == 28 || emojiState == 29;
    if (!isLow) {
      stateTimer = 0;
      changeState(27);
    } else if (stateTimer > 100) {
      stateTimer = 0;
      int next = emojiState;
      for (int t = 0; t < 4 && next == emojiState; t++) next = LOW_POOL[random(0, 6)];
      changeState(next);
    }
  } else if (stateTimer > 75) {
    stateTimer = 0;
    int next = emojiState;
    for (int t = 0; t < 4 && next == emojiState; t++) {
      next = POOL[currentMood][random(0, POOL_SIZE)];
    }
    changeState(next);
  }

  // --- kedip (hanya ekspresi bermata terbuka) ---
  if (!trans && reactionTimer == 0 && canBlink(emojiState)) {
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

  // --- transisi: 75% -> 45% -> 20% (ganti ekspresi) -> 45% -> 75% ---
  int pct = 100;
  if (trans) {
    if (transFrame == 2) emojiState = pendingState;
    pct = TRANS_PCT[transFrame];
    if (++transFrame >= 5) trans = false;
  }

  render(pct);
}
