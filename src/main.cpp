// Emochi - emoji ala Dasai Mochi untuk LilyGo T-Display (ESP32, 240x135)
// Port dari emochi_final2.js (Bruce JS)
// Tombol: GPIO35 = colek (1-2x kaget, 3x+ marah) | GPIO0 = ganti mood manual

#include <Arduino.h>
#include <TFT_eSPI.h>

#define BTN_POKE 35
#define BTN_MOOD 0
#define ROTATION 1   // ubah ke 3 kalau tampilan terbalik

TFT_eSPI tft;
TFT_eSprite spr(&tft);

const uint16_t BG = TFT_BLACK;
const uint16_t FG = TFT_WHITE;

// Mood: 0 normal, 1 happy, 2 sleepy, 3 grumpy
const uint8_t POOL[4][6] = {
  {0, 1, 7, 5, 4, 11},
  {0, 11, 1, 0, 11, 0},
  {4, 5, 9, 4, 5, 9},
  {10, 5, 7, 10, 5, 7}
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

static void drawZ(int x, int y, int w, int h) {
  // huruf Z kecil dari 4 persegi
  spr.fillRect(x, y, w, 2, FG);
  spr.fillRect(x + w - 4, y + 2, 2, h - 4 > 2 ? 3 : 2, FG);
  spr.fillRect(x + w - 6, y + 5, 2, 3, FG);
  spr.fillRect(x, y + h, w, 2, FG);
}

static void render() {
  spr.fillSprite(BG);
  int by = baseEyeY, my = mouthY;

  if (blinkPhase == 1 || blinkPhase == 3) {
    wide(eyeL_X, by - 4, 18, by + 14);
    wide(eyeR_X, by - 4, 18, by + 14);
    spr.fillRect(centerX - 6, my, 12, 5, FG);

  } else if (blinkPhase == 2) {
    spr.fillRect(eyeL_X - 17, by, 34, 6, FG);
    spr.fillRect(eyeR_X - 17, by, 34, 6, FG);
    spr.fillRect(centerX - 6, my, 12, 5, FG);

  } else if (emojiState == 0) {          // normal / senang
    tall(eyeL_X, by - 22, by + 22);
    tall(eyeR_X, by - 22, by + 22);
    spr.fillRect(centerX - 9, my, 18, 6, FG);
    spr.fillRect(centerX - 13, my - 4, 5, 5, FG);
    spr.fillRect(centerX + 8, my - 4, 5, 5, FG);

  } else if (emojiState == 1) {          // melirik
    int lx = (lookDir == 0) ? -10 : 10;
    tall(eyeL_X + lx, by - 22, by + 22);
    tall(eyeR_X + lx, by - 22, by + 22);
    spr.fillRect(centerX - 6, my, 12, 6, FG);

  } else if (emojiState == 3) {          // kaget
    spr.fillCircle(eyeL_X, by, 20, FG);
    spr.fillCircle(eyeL_X, by, 10, BG);
    spr.fillCircle(eyeR_X, by, 20, FG);
    spr.fillCircle(eyeR_X, by, 10, BG);
    spr.fillCircle(centerX, my + 4, 8, FG);
    spr.fillCircle(centerX, my + 4, 4, BG);

  } else if (emojiState == 4) {          // mengantuk
    spr.fillRect(eyeL_X - 17, by, 34, 26, FG);
    spr.fillCircle(eyeL_X, by + 26, 17, FG);
    spr.fillRect(eyeR_X - 17, by, 34, 26, FG);
    spr.fillCircle(eyeR_X, by + 26, 17, FG);
    spr.fillRect(centerX - 9, my + 2, 18, 5, FG);

  } else if (emojiState == 5) {          // sedih
    tall(eyeL_X, by - 22, by + 33);
    tall(eyeR_X, by - 22, by + 33);
    spr.fillRect(centerX - 9, my + 6, 18, 5, FG);
    spr.fillRect(centerX - 9, my + 1, 5, 5, FG);
    spr.fillRect(centerX + 4, my + 1, 5, 5, FG);

  } else if (emojiState == 7) {          // curiga
    tall(eyeL_X, by - 22, by + 22);
    tall(eyeR_X, by - 36, by + 8);
    spr.fillRect(centerX - 8, my + 2, 16, 5, FG);

  } else if (emojiState == 9) {          // tidur zzz
    spr.fillRect(eyeL_X - 11, by + 2, 22, 4, FG);
    spr.fillRect(eyeR_X - 11, by + 2, 22, 4, FG);
    spr.fillRect(centerX - 4, my + 3, 8, 3, FG);
    int z = zOffset;
    spr.fillRect(centerX + 14, by - 18 - z, 8, 2, FG);
    spr.fillRect(centerX + 18, by - 16 - z, 2, 2, FG);
    spr.fillRect(centerX + 16, by - 14 - z, 2, 2, FG);
    spr.fillRect(centerX + 14, by - 12 - z, 8, 2, FG);
    spr.fillRect(centerX + 20, by - 30 - z, 10, 2, FG);
    spr.fillRect(centerX + 25, by - 28 - z, 2, 3, FG);
    spr.fillRect(centerX + 23, by - 25 - z, 2, 3, FG);
    spr.fillRect(centerX + 20, by - 22 - z, 10, 2, FG);
    spr.fillRect(centerX + 10, by - 46 - z, 13, 2, FG);
    spr.fillRect(centerX + 17, by - 44 - z, 2, 4, FG);
    spr.fillRect(centerX + 14, by - 40 - z, 2, 4, FG);
    spr.fillRect(centerX + 10, by - 36 - z, 13, 2, FG);

  } else if (emojiState == 10) {         // marah
    wide(eyeL_X, by - 8, 16, by);
    wide(eyeR_X, by - 8, 16, by);
    spr.fillRect(eyeL_X - 17, by - 20, 12, 4, FG);
    spr.fillRect(eyeL_X - 6,  by - 17, 12, 4, FG);
    spr.fillRect(eyeL_X + 5,  by - 14, 12, 4, FG);
    spr.fillRect(eyeR_X - 17, by - 14, 12, 4, FG);
    spr.fillRect(eyeR_X - 6,  by - 17, 12, 4, FG);
    spr.fillRect(eyeR_X + 5,  by - 20, 12, 4, FG);
    spr.fillRect(centerX - 9, my, 18, 5, FG);
    spr.fillRect(centerX - 13, my + 2, 5, 5, FG);
    spr.fillRect(centerX + 8, my + 2, 5, 5, FG);

  } else if (emojiState == 11) {         // ketawa
    spr.fillCircle(eyeL_X, by - 6, 17, FG);
    spr.fillRect(eyeL_X - 18, by - 6, 36, 18, BG);
    spr.fillRect(eyeL_X - 14, by + 10, 28, 3, FG);
    spr.fillCircle(eyeR_X, by - 6, 17, FG);
    spr.fillRect(eyeR_X - 18, by - 6, 36, 18, BG);
    spr.fillRect(eyeR_X - 14, by + 10, 28, 3, FG);
    spr.fillRect(centerX - 14, my, 28, 8, FG);
    spr.fillCircle(centerX - 14, my, 8, FG);
    spr.fillCircle(centerX + 14, my, 8, FG);
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
    if (pressCount >= 3) { emojiState = 10; reactionTimer = 60; }
    else                 { emojiState = 3;  reactionTimer = 50; }
  }
  if (!moodBtn && prevMood) {
    currentMood = (currentMood + 1) % 4;
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
    currentMood = random(0, 4);
  }

  // --- ganti ekspresi idle ---
  if (reactionTimer > 0) {
    reactionTimer--;
  } else if (stateTimer > 75) {
    stateTimer = 0;
    emojiState = POOL[currentMood][random(0, 6)];
  }

  // --- kedip (tidak saat tidur/ngantuk) ---
  if (reactionTimer == 0 && emojiState != 9 && emojiState != 4) {
    if (blinkPhase == 0 && blinkTimer > blinkCooldown) {
      if (random(1, 5) == 1) { blinkPhase = 1; blinkTimer = 0; }
    } else if (blinkPhase == 1 && blinkTimer > 3) { blinkPhase = 2; blinkTimer = 0; }
    else if (blinkPhase == 2 && blinkTimer > 4)   { blinkPhase = 3; blinkTimer = 0; }
    else if (blinkPhase == 3 && blinkTimer > 3)   { blinkPhase = 0; blinkTimer = 0; }
  } else if (reactionTimer > 0) {
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
