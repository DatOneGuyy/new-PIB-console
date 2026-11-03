/*
  480 x 320 parallel TFT shield test for Arduino Mega 2560.
  Install MCUFRIEND_kbv and Adafruit GFX Library using Library Manager.
  Select Tools > Board > Arduino Mega or Mega 2560.
  Assumes an MCUFRIEND-compatible parallel shield pinout. SPI displays
  and some Mega-only 16-bit shields need a different driver/pin setup.
  No touch-screen or SD-card library is needed for this test.
*/
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>

MCUFRIEND_kbv tft;

// Leave at 0 for automatic detection. Set to the controller printed on
// your module if it cannot be read (e.g. 0x9486 for an ILI9486).
const uint16_t FORCE_DRIVER_ID = 0x9481;
const uint16_t BLACK   = 0x0000;
const uint16_t WHITE   = 0xFFFF;
const uint16_t RED     = 0xF800;
const uint16_t GREEN   = 0x07E0;
const uint16_t BLUE    = 0x001F;
const uint16_t CYAN    = 0x07FF;
const uint16_t MAGENTA = 0xF81F;
const uint16_t YELLOW  = 0xFFE0;
uint16_t driverId;

// =========================================================================
// Configuration Settings
// =========================================================================
const int BUTTON_PIN = A0;             // Analog pin for button input
const int GROUND_Y = 240;              // Horizontal ground line position (Y)
const int STEP_SPEED = 4;              // Pixels to advance each frame (0 = walk in place)
const int MIN_X = 40;                  // Leftmost X position
const int MAX_X = 440;                 // Rightmost X position before wrapping around
const unsigned long FRAME_INTERVAL_MS = 85;  // Milliseconds between animation frames
const unsigned long DEBOUNCE_MS = 50;  // Button debounce threshold in milliseconds
const uint16_t FIGURE_COLOR = WHITE;   // Stick figure color
const uint16_t GROUND_COLOR = WHITE;   // Ground line color

// =========================================================================
// Animation & State Variables
// =========================================================================
bool isWalking = true;                 // Start or stop walking (toggled by button)
int currentX = (STEP_SPEED > 0) ? MIN_X : 240;
int prevX = currentX;
int currentFrame = 0;
unsigned long lastFrameTime = 0;

// Button debounce tracking
int lastRawButton = LOW;
int stableButtonState = LOW;
unsigned long lastDebounceTime = 0;

// =========================================================================
// Stick Figure Walk Cycle Data
// =========================================================================
struct FrameData {
    int8_t bob;          // Vertical torso/hip bob offset (+/- pixels)
    int8_t lk_x, lk_y;   // Left knee offset relative to hip
    int8_t lf_x, lf_y;   // Left foot offset relative to hip
    int8_t rk_x, rk_y;   // Right knee offset relative to hip
    int8_t rf_x, rf_y;   // Right foot offset relative to hip
    int8_t la_x, la_y;   // Left hand offset relative to shoulder
    int8_t ra_x, ra_y;   // Right hand offset relative to shoulder
};

// 8-frame walk cycle with natural knee articulation and counter-arm swing
const FrameData walkFrames[8] = {
    // Frame 0: Left contact (left foot forward, right back), arms counter-swinging
    { -2,   7, 19,  14, 37,  -6, 19, -14, 37,    0, 18,   0, 18 },
    // Frame 1: Left leg supporting body, right foot lifting
    {  0,  10, 17,   7, 37,  10, 17,  -7, 29,   -7, 17,   7, 17 },
    // Frame 2: Right leg passing forward (knee bent), high point
    {  2,  10, 18,   0, 37,  16, 12,   0, 25,  -10, 15,  10, 15 },
    // Frame 3: Right leg extending forward, left leg pushing back
    {  0,   3, 20,  -7, 37,  17, 11,   7, 29,   -7, 17,   7, 17 },
    // Frame 4: Right contact (mirror of Frame 0)
    { -2,  -6, 19, -14, 37,   7, 19,  14, 37,    0, 18,   0, 18 },
    // Frame 5: Right leg supporting body, left foot lifting (mirror of Frame 1)
    {  0,  10, 17,  -7, 29,  10, 17,   7, 37,    7, 17,  -7, 17 },
    // Frame 6: Left leg passing forward (knee bent), high point (mirror of Frame 2)
    {  2,  16, 12,   0, 25,  10, 18,   0, 37,   10, 15, -10, 15 },
    // Frame 7: Left leg extending forward, right leg pushing back (mirror of Frame 3)
    {  0,  17, 11,   7, 29,   3, 20,  -7, 37,    7, 17,  -7, 17 }
};

// Standing / idle pose when stopped
const FrameData standingPose = {
    0,
    -3, 19, -4, 37,
     3, 19,  4, 37,
    -6, 18,
     6, 18
};

// =========================================================================
// Graphic Helpers
// =========================================================================

// Draws 2-pixel wide lines for high visibility on 480x320 TFT
void drawThickLine(int x0, int y0, int x1, int y1, uint16_t color) {
    tft.drawLine(x0, y0, x1, y1, color);
    if (abs(x1 - x0) > abs(y1 - y0)) {
        tft.drawLine(x0, y0 + 1, x1, y1 + 1, color);
    } else {
        tft.drawLine(x0 + 1, y0, x1 + 1, y1, color);
    }
}

// Draws the stick figure at the specified X position with the given frame pose
void drawFigure(int x, const FrameData &f, uint16_t color) {
    int bobY = f.bob;
    int hipY = GROUND_Y - 37 + bobY;
    int neckY = hipY - 32;
    int shoulderY = hipY - 28;
    int headY = hipY - 43;
    const int headRadius = 10;

    // Head (2-pixel outline)
    tft.drawCircle(x, headY, headRadius, color);
    tft.drawCircle(x, headY, headRadius - 1, color);

    // Torso (neck down to hip)
    drawThickLine(x, neckY, x, hipY, color);

    // Arms (shoulder to hands)
    drawThickLine(x, shoulderY, x + f.la_x, shoulderY + f.la_y, color);
    drawThickLine(x, shoulderY, x + f.ra_x, shoulderY + f.ra_y, color);

    // Left leg (hip -> knee -> foot)
    drawThickLine(x, hipY, x + f.lk_x, hipY + f.lk_y, color);
    drawThickLine(x + f.lk_x, hipY + f.lk_y, x + f.lf_x, hipY + f.lf_y, color);

    // Right leg (hip -> knee -> foot)
    drawThickLine(x, hipY, x + f.rk_x, hipY + f.rk_y, color);
    drawThickLine(x + f.rk_x, hipY + f.rk_y, x + f.rf_x, hipY + f.rf_y, color);
}

// Clears only the bounding box of the previous figure position to prevent screen flicker
void eraseFigure(int x) {
    int clearX = x - 28;
    int clearW = 56;
    int clearY = 142;
    int clearH = (GROUND_Y + 2) - clearY + 1; // 101 pixels high

    // Clamp coordinates inside screen bounds
    if (clearX < 0) {
        clearW += clearX;
        clearX = 0;
    }
    if (clearX + clearW > tft.width()) {
        clearW = tft.width() - clearX;
    }

    if (clearW > 0) {
        tft.fillRect(clearX, clearY, clearW, clearH, BLACK);
        // Restore ground line segment inside cleared area
        tft.drawFastHLine(clearX, GROUND_Y, clearW, GROUND_COLOR);
    }
}

// Updates status text without full screen redrawing
void drawStatus(bool walking) {
    tft.setTextSize(2);
    tft.setCursor(20, 260);
    tft.setTextColor(WHITE, BLACK);
    tft.print(F("State: "));
    if (walking) {
        tft.setTextColor(GREEN, BLACK);
        tft.print(F("WALKING"));
    } else {
        tft.setTextColor(RED, BLACK);
        tft.print(F("STOPPED"));
    }
}

// Initial scene setup (header, ground line, UI text)
void drawScene() {
    tft.fillScreen(BLACK);

    // Header banner
    tft.setTextSize(2);
    tft.setTextColor(CYAN, BLACK);
    tft.setCursor(10, 8);
    tft.print(F("Stick Figure Animation"));
    tft.drawFastHLine(0, 30, tft.width(), 0x4208);

    // Ground line
    tft.drawFastHLine(0, GROUND_Y, tft.width(), GROUND_COLOR);

    // Initial status display
    drawStatus(isWalking);

    // Instruction prompt
    tft.setTextSize(1);
    tft.setTextColor(YELLOW, BLACK);
    tft.setCursor(20, 292);
    tft.print(F("Press the button on pin A0 to Start / Stop walking"));
}

// =========================================================================
// Setup
// =========================================================================
void setup() {
    Serial.begin(115200);
    tft.reset();
    const uint16_t detectedId = tft.readID();
    Serial.print(F("Detected TFT ID: 0x"));
    Serial.println(detectedId, HEX);
    driverId = FORCE_DRIVER_ID ? FORCE_DRIVER_ID : detectedId;

    if (driverId == 0xD3D3 || driverId == 0xFFFF || driverId == 0x0000) {
        Serial.println(F("Cannot read controller. Set FORCE_DRIVER_ID to your TFT controller ID."));
        while (true) { delay(1000); }
    }

    tft.begin(driverId);
    tft.setRotation(1);  // Landscape: 480 wide by 320 high.
    tft.setTextWrap(false);

    Serial.print(F("Landscape dimensions: "));
    Serial.print(tft.width());
    Serial.print('x');
    Serial.println(tft.height());

    // Draw the static UI and ground line
    drawScene();

    // Draw the initial stick figure pose
    if (isWalking) {
        drawFigure(currentX, walkFrames[currentFrame], FIGURE_COLOR);
    } else {
        drawFigure(currentX, standingPose, FIGURE_COLOR);
    }
    prevX = currentX;

    Serial.println(F("Animation initialized."));
    Serial.println(F("Press button connected to A0 to start/stop walking."));
}

// =========================================================================
// Main Loop
// =========================================================================
void loop() {
    // 1. Read button with non-blocking debounce
    int rawButton = (analogRead(BUTTON_PIN) < 5) ? LOW : HIGH;

    if (rawButton != lastRawButton) {
        lastDebounceTime = millis();
        lastRawButton = rawButton;
    }

    if ((millis() - lastDebounceTime) > DEBOUNCE_MS) {
        if (rawButton != stableButtonState) {
            stableButtonState = rawButton;
            // Rising edge trigger on button press
            if (stableButtonState == HIGH) {
                isWalking = !isWalking;
                Serial.print(F("Button pressed -> State: "));
                Serial.println(isWalking ? F("WALKING") : F("STOPPED"));

                drawStatus(isWalking);

                if (!isWalking) {
                    // Immediately transition to standing pose at current position
                    eraseFigure(prevX);
                    drawFigure(currentX, standingPose, FIGURE_COLOR);
                    prevX = currentX;
                }
            }
        }
    }

    // 2. Advance walking animation
    if (isWalking) {
        unsigned long currentMillis = millis();
        if (currentMillis - lastFrameTime >= FRAME_INTERVAL_MS) {
            lastFrameTime = currentMillis;

            // Advance frame cycle
            currentFrame = (currentFrame + 1) % 8;

            // Advance X position
            currentX += STEP_SPEED;
            if (currentX > MAX_X) {
                currentX = MIN_X;
            }

            // Erase previous figure position and render the new frame
            eraseFigure(prevX);
            drawFigure(currentX, walkFrames[currentFrame], FIGURE_COLOR);
            prevX = currentX;
        }
    }
}
