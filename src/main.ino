/* 
  ESP32 Game Console - Single File Build
  Display: 1.8" TFT ST7735 (128x160) over SPI
  Control: Bluetooth Serial (Android/iOS terminal). Name: "ESP32-Console"

  Games:
    1) Snake
    2) Flappy Bird
    3) Fish Game

  Controls over Bluetooth:
    - 'U' = Up
    - 'D' = Down
    - 'L' = Left
    - 'R' = Right
    - 'S' = Select / Action (tap for Flappy)
    - 'B' = Back to menu / Quit
    - 'N' = Next menu item
    - 'P' = Previous menu item

  Libraries (install in Arduino IDE → Library Manager):
    - Adafruit GFX Library
    - Adafruit ST7735 and ST7789 Library

  Wiring (example — adjust if different):
    TFT_CS   -> GPIO 5
    TFT_DC   -> GPIO 2
    TFT_RST  -> GPIO 4
    TFT_SCLK -> GPIO 18 (SCK)
    TFT_MOSI -> GPIO 23 (SDA/MOSI)
    GND/VCC  -> GND / 3.3V (LED via resistor if needed)
*/

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include "BluetoothSerial.h"

// ====== TFT Pins (change to your wiring) ======
#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// ST7735 1.8" 128x160
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

// ====== Bluetooth ======
BluetoothSerial SerialBT;

// ====== Screen ======
const int SCREEN_W = 128;
const int SCREEN_H = 160;

// ====== Colors (565) ======
#define COL_BG       ST77XX_BLACK
#define COL_MENU     ST77XX_WHITE
#define COL_HILIGHT  ST77XX_YELLOW
#define COL_SNAKE    ST77XX_GREEN
#define COL_FOOD     ST77XX_RED
#define COL_PIPE     ST77XX_CYAN
#define COL_BIRD     ST77XX_YELLOW
#define COL_WATER    0x7D7C  // light blue-ish
#define COL_FISH     ST77XX_ORANGE
#define COL_BUBBLE   ST77XX_WHITE

// ====== App States ======
enum AppState {
  STATE_MENU,
  STATE_SNAKE,
  STATE_FLAPPY,
  STATE_FISH
};
AppState appState = STATE_MENU;

// ====== Input ======
struct InputState {
  bool up=false, down=false, left=false, right=false, select=false, back=false;
} input;

void clearInput() {
  input = InputState();
}

void readBluetooth() {
  while (SerialBT.available()) {
    char c = SerialBT.read();
    c = toupper(c);
    if (c == 'U') input.up = true;
    else if (c == 'D') input.down = true;
    else if (c == 'L') input.left = true;
    else if (c == 'R') input.right = true;
    else if (c == 'S') input.select = true;  // action / jump / select
    else if (c == 'B') input.back = true;    // back to menu
    else if (c == 'N') { input.down = true; } // menu next
    else if (c == 'P') { input.up = true; }   // menu prev
  }
}

// ====== Utility ======
void centerText(const String& s, int16_t y, uint16_t color=COL_MENU, uint8_t size=1) {
  int16_t x1,y1; uint16_t w,h;
  tft.setTextSize(size);
  tft.setTextColor(color);
  tft.getTextBounds(const_cast<char*>(s.c_str()), 0, y, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_W - w)/2;
  tft.setCursor(x, y);
  tft.print(s);
}

// =====================================================
//                         MENU
// =====================================================
const char* menuItems[] = {"Snake", "Flappy Bird", "Fish Game"};
int menuIndex = 0;

void drawMenu() {
  tft.fillScreen(COL_BG);
  centerText("ESP32 Console", 8, COL_HILIGHT, 1);
  centerText("BT: ESP32-Console", 20, ST77XX_BLUE, 1);
  centerText("Controls: U/D/L/R", 32, ST77XX_GREEN, 1);
  centerText("S=Select  B=Back", 44, ST77XX_GREEN, 1);

  for (int i=0; i<3; i++) {
    int y = 70 + i*20;
    String label = String(i==menuIndex ? "> " : "  ") + menuItems[i];
    centerText(label, y, i==menuIndex ? COL_HILIGHT : COL_MENU, 2);
  }
}

void menuLoop() {
  static bool first = true;
  if (first) { drawMenu(); first=false; }

  readBluetooth();

  bool changed=false;
  if (input.up)   { menuIndex = (menuIndex + 2) % 3; changed=true; }
  if (input.down) { menuIndex = (menuIndex + 1) % 3; changed=true; }

  if (changed) drawMenu();

  if (input.select) {
    if (menuIndex == 0) { appState = STATE_SNAKE; }
    if (menuIndex == 1) { appState = STATE_FLAPPY; }
    if (menuIndex == 2) { appState = STATE_FISH; }
    first = true;
    clearInput();
    delay(150);
    return;
  }

  clearInput();
  delay(16);
}

// =====================================================
//                        SNAKE
// =====================================================
const int CELL = 8; // 16x20 grid
const int GRID_W = SCREEN_W / CELL;  // 16
const int GRID_H = SCREEN_H / CELL;  // 20
const int MAX_SNAKE = GRID_W * GRID_H;

int snakeX[MAX_SNAKE];
int snakeY[MAX_SNAKE];
int snakeLen;
int snakeDx = 1, snakeDy = 0;
int foodX, foodY;
unsigned long lastSnakeTick;
int snakeSpeed = 120; // ms per move
bool snakeAlive;

void spawnFood() {
  bool ok=false;
  while(!ok) {
    foodX = random(0, GRID_W);
    foodY = random(0, GRID_H);
    ok = true;
    for (int i=0;i<snakeLen;i++) {
      if (snakeX[i]==foodX && snakeY[i]==foodY) { ok=false; break; }
    }
  }
}

void snakeDrawCell(int gx, int gy, uint16_t col) {
  tft.fillRect(gx*CELL, gy*CELL, CELL-1, CELL-1, col);
}

void snakeReset() {
  tft.fillScreen(COL_BG);
  snakeLen = 4;
  int sx = GRID_W/2;
  int sy = GRID_H/2;
  for (int i=0;i<snakeLen;i++) {
    snakeX[i] = sx - i;
    snakeY[i] = sy;
  }
  snakeDx = 1; snakeDy = 0;
  snakeAlive = true;
  spawnFood();
  lastSnakeTick = millis();
  // draw initial
  for (int i=0;i<snakeLen;i++) snakeDrawCell(snakeX[i], snakeY[i], COL_SNAKE);
  snakeDrawCell(foodX, foodY, COL_FOOD);
  centerText("Snake  B=Menu", 2, ST77XX_BLUE, 1);
}

void snakeLoop() {
  static bool first=true;
  if (first) { snakeReset(); first=false; }

  readBluetooth();
  if (input.back) {
    appState = STATE_MENU; first=true; clearInput(); delay(150); return;
  }

  // Change direction (no reverse directly)
  if (input.up && snakeDy==0)    { snakeDx=0; snakeDy=-1; }
  if (input.down && snakeDy==0)  { snakeDx=0; snakeDy=1;  }
  if (input.left && snakeDx==0)  { snakeDx=-1; snakeDy=0; }
  if (input.right && snakeDx==0) { snakeDx=1; snakeDy=0;  }
  clearInput();

  unsigned long now = millis();
  if (now - lastSnakeTick >= snakeSpeed && snakeAlive) {
    lastSnakeTick = now;

    // erase tail
    snakeDrawCell(snakeX[snakeLen-1], snakeY[snakeLen-1], COL_BG);

    // move body
    for (int i=snakeLen-1; i>0; --i) {
      snakeX[i]=snakeX[i-1];
      snakeY[i]=snakeY[i-1];
    }
    // head
    snakeX[0]+=snakeDx; snakeY[0]+=snakeDy;

    // wrap around
    if (snakeX[0] < 0) snakeX[0]=GRID_W-1;
    if (snakeY[0] < 0) snakeY[0]=GRID_H-1;
    if (snakeX[0] >= GRID_W) snakeX[0]=0;
    if (snakeY[0] >= GRID_H) snakeY[0]=0;

    // collision with self
    for (int i=1;i<snakeLen;i++) {
      if (snakeX[i]==snakeX[0] && snakeY[i]==snakeY[0]) {
        snakeAlive=false;
      }
    }

    // draw head
    snakeDrawCell(snakeX[0], snakeY[0], COL_SNAKE);

    // food
    if (snakeX[0]==foodX && snakeY[0]==foodY) {
      snakeLen = min(snakeLen+1, MAX_SNAKE-1);
      // new cell extends from tail (no erase next tick)
      snakeX[snakeLen-1] = snakeX[snakeLen-2];
      snakeY[snakeLen-1] = snakeY[snakeLen-2];
      spawnFood();
      snakeDrawCell(foodX, foodY, COL_FOOD);
    }
  }

  if (!snakeAlive) {
    centerText("Game Over!", 70, ST77XX_RED, 2);
    centerText("S=Retry  B=Menu", 90, ST77XX_WHITE, 1);
    readBluetooth();
    if (input.select) { first=true; tft.fillScreen(COL_BG); }
    if (input.back)   { appState=STATE_MENU; first=true; }
    clearInput();
    delay(120);
  } else {
    delay(10);
  }
}

// =====================================================
//                       FLAPPY
// =====================================================
float birdY, birdV;
const float gravity = 0.12;
const float flapVel = -2.6;
int pipeX[3], gapY[3];
const int PIPE_W = 18;
const int GAP_H = 42;
int scoreFlappy;
bool flappyAlive;
unsigned long lastFlappyFrame;

void flappyReset() {
  tft.fillScreen(COL_BG);
  centerText("Flappy Bird", 2, ST77XX_BLUE, 1);
  centerText("S=Flap  B=Menu", 14, ST77XX_BLUE, 1);
  birdY = SCREEN_H/2;
  birdV = 0;
  int dist = 60;
  for (int i=0;i<3;i++) {
    pipeX[i] = SCREEN_W + i*dist;
    gapY[i] = 30 + random(0, SCREEN_H-60-GAP_H);
  }
  scoreFlappy = 0;
  flappyAlive = true;
  lastFlappyFrame = millis();
}

void drawBird(int y, uint16_t col) {
  tft.fillRect(20, y-3, 6, 6, col);
}

void drawPipe(int x, int gy, uint16_t col) {
  // top pipe: 0..gy
  tft.fillRect(x, 24, PIPE_W, gy-24, col);
  // bottom pipe: gy+GAP_H .. bottom-1
  tft.fillRect(x, gy+GAP_H, PIPE_W, SCREEN_H - (gy+GAP_H), col);
}

void flappyLoop() {
  static bool first=true;
  if (first) { flappyReset(); first=false; }

  readBluetooth();
  if (input.back) { appState=STATE_MENU; first=true; clearInput(); delay(120); return; }
  if (input.select) { birdV = flapVel; }
  clearInput();

  unsigned long now = millis();
  if (now - lastFlappyFrame >= 16) {
    lastFlappyFrame = now;

    // erase bird & pipes
    drawBird((int)birdY, COL_BG);
    for (int i=0;i<3;i++) drawPipe(pipeX[i], gapY[i], COL_BG);

    // physics
    birdV += gravity;
    birdY += birdV;

    // move pipes
    for (int i=0;i<3;i++) {
      pipeX[i] -= 2;
      if (pipeX[i] + PIPE_W < 0) {
        pipeX[i] = SCREEN_W + random(10, 40);
        gapY[i] = 30 + random(0, SCREEN_H-60-GAP_H);
        scoreFlappy++;
      }
    }

    // collisions
    if (birdY < 24 || birdY > SCREEN_H-1) flappyAlive=false;
    for (int i=0;i<3;i++) {
      if (20+6 >= pipeX[i] && 20 <= pipeX[i]+PIPE_W) {
        if (!(birdY >= gapY[i] && birdY <= gapY[i]+GAP_H)) {
          flappyAlive=false;
        }
      }
    }

    // draw HUD line
    tft.drawFastHLine(0, 22, SCREEN_W, ST77XX_DARKGREY);
    // draw pipes & bird
    for (int i=0;i<3;i++) drawPipe(pipeX[i], gapY[i], COL_PIPE);
    drawBird((int)birdY, COL_BIRD);

    // score
    tft.fillRect(0,0,SCREEN_W,20,COL_BG);
    tft.setCursor(2,2); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
    tft.print("Score: "); tft.print(scoreFlappy);
  }

  if (!flappyAlive) {
    centerText("Game Over!", 70, ST77XX_RED, 2);
    centerText("S=Retry  B=Menu", 90, ST77XX_WHITE, 1);
    readBluetooth();
    if (input.select) { first=true; }
    if (input.back)   { appState=STATE_MENU; first=true; }
    clearInput();
    delay(150);
  }
}

// =====================================================
//                        FISH
// =====================================================
int fishX, fishY;
int foodFx, foodFy;
int scoreFish;
unsigned long lastFishFrame;
int bubbleY[6];
int bubbleX[6];

void fishReset() {
  tft.fillScreen(COL_WATER);
  centerText("Fish Game", 2, ST77XX_BLUE, 1);
  centerText("U/D/L/R move", 14, ST77XX_BLUE, 1);
  centerText("B=Menu", 26, ST77XX_BLUE, 1);

  fishX = SCREEN_W/2;
  fishY = SCREEN_H/2 + 20;
  foodFx = random(10, SCREEN_W-10);
  foodFy = random(40, SCREEN_H-10);
  scoreFish = 0;

  for (int i=0;i<6;i++) {
    bubbleX[i] = random(5, SCREEN_W-5);
    bubbleY[i] = random(40, SCREEN_H);
  }

  lastFishFrame = millis();
}

void drawFish(int x, int y, uint16_t col) {
  // simple fish: body + tail
  tft.fillCircle(x, y, 6, col);
  tft.fillTriangle(x-6, y, x-12, y-4, x-12, y+4, col);
  tft.fillRect(x-6, y-2, 8, 4, col);
}

void drawFood(int x, int y, uint16_t col) {
  tft.fillCircle(x, y, 3, col);
}

void fishLoop() {
  static bool first=true;
  if (first) { fishReset(); first=false; }

  readBluetooth();
  if (input.back) { appState=STATE_MENU; first=true; clearInput(); delay(120); return; }

  // erase objects
  drawFish(fishX, fishY, COL_WATER);
  drawFood(foodFx, foodFy, COL_WATER);
  for (int i=0;i<6;i++) tft.fillCircle(bubbleX[i], bubbleY[i], 1, COL_WATER);

  // movement
  if (input.up)    fishY -= 2;
  if (input.down)  fishY += 2;
  if (input.left)  fishX -= 2;
  if (input.right) fishX += 2;
  clearInput();

  // bounds
  if (fishX < 6) fishX=6;
  if (fishX > SCREEN_W-6) fishX=SCREEN_W-6;
  if (fishY < 40+6) fishY=40+6;
  if (fishY > SCREEN_H-6) fishY=SCREEN_H-6;

  // bubbles animate upward
  for (int i=0;i<6;i++) {
    bubbleY[i] -= 1;
    if (bubbleY[i] < 40) {
      bubbleY[i] = SCREEN_H;
      bubbleX[i] = random(5, SCREEN_W-5);
    }
  }

  // collision with food
  int dx = fishX - foodFx;
  int dy = fishY - foodFy;
  if (dx*dx + dy*dy <= (6+3)*(6+3)) {
    scoreFish++;
    foodFx = random(10, SCREEN_W-10);
    foodFy = random(40, SCREEN_H-10);
  }

  // redraw water HUD
  tft.fillRect(0,0,SCREEN_W,38,COL_WATER);
  tft.setCursor(2,2); tft.setTextColor(ST77XX_WHITE); tft.setTextSize(1);
  tft.print("Score: "); tft.print(scoreFish);
  tft.setCursor(90,2); tft.print("B=Menu");
  tft.drawFastHLine(0, 38, SCREEN_W, ST77XX_WHITE);

  // draw objects
  for (int i=0;i<6;i++) tft.fillCircle(bubbleX[i], bubbleY[i], 1, COL_BUBBLE);
  drawFood(foodFx, foodFy, ST77XX_WHITE);
  drawFish(fishX, fishY, COL_FISH);

  delay(16);
}

// =====================================================
//                      SETUP/LOOP
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(100);

  // TFT init
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(COL_BG);
  tft.setTextWrap(false);

  // Bluetooth
  SerialBT.begin("ESP32-Console"); // device name
  Serial.println("Bluetooth device started as 'ESP32-Console'");
  tft.setCursor(4,70); tft.setTextColor(COL_MENU); tft.setTextSize(1);
  tft.print("Open BT terminal & send:");
  tft.setCursor(4,82); tft.print("U/D/L/R/S/B");
  delay(900);
  tft.fillScreen(COL_BG);

  randomSeed(esp_random());
}

void loop() {
  switch (appState) {
    case STATE_MENU:   menuLoop(); break;
    case STATE_SNAKE:  snakeLoop(); break;
    case STATE_FLAPPY: flappyLoop(); break;
    case STATE_FISH:   fishLoop(); break;
  }
}
