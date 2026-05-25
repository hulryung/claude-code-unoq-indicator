// Claude Code character animation on UNO Q LED matrix (8x13).
//
// States pushed from MPU via Bridge.call("set_state", "<name>") -> bool.
// States: idle | thinking | tool | done | notify | error

#include <Arduino_LED_Matrix.h>
#include <Arduino_RouterBridge.h>

Arduino_LED_Matrix matrix;

constexpr uint8_t ROWS = 8;
constexpr uint8_t COLS = 13;

enum State : uint8_t {
    ST_IDLE = 0, ST_THINKING, ST_TOOL, ST_DONE, ST_NOTIFY, ST_ERROR
};

volatile State currentState = ST_IDLE;
unsigned long stateEnteredAt = 0;

// Cube robot body (head + shoulders + body, no legs). Drawn on rows 1-4.
const uint8_t BODY[ROWS][COLS] = {
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,7,7,7,7,7,7,7,7,7,0,0},  // head top
    {0,0,7,7,0,5,5,5,0,7,7,0,0},  // head + eyes
    {7,7,7,5,5,5,5,5,5,5,7,7,7},  // shoulders
    {0,7,5,5,5,5,5,5,5,5,5,7,0},  // body
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
};
const uint8_t LEGS_STAND[COLS] = {0,0,7,0,7,0,0,0,7,0,7,0,0};
const uint8_t LEGS_A[COLS]     = {0,0,7,7,0,0,0,0,0,7,7,0,0};
const uint8_t LEGS_B[COLS]     = {0,0,0,7,7,0,0,0,7,7,0,0,0};

// Claude Code thinking spinner — 6 frames cycling. Centered 5x5 sparkle.
// Mirrors Claude Code's real spinner chars: · ✢ ✳ ∗ ✻ ✽
const uint8_t SPINNER[6][5][5] = {
    // 0: tiny center dot (·)
    {{0,0,0,0,0},
     {0,0,0,0,0},
     {0,0,7,0,0},
     {0,0,0,0,0},
     {0,0,0,0,0}},
    // 1: small plus (✢)
    {{0,0,0,0,0},
     {0,0,3,0,0},
     {0,3,7,3,0},
     {0,0,3,0,0},
     {0,0,0,0,0}},
    // 2: 8-point cluster (✳)
    {{0,0,0,0,0},
     {0,3,3,3,0},
     {0,3,7,3,0},
     {0,3,3,3,0},
     {0,0,0,0,0}},
    // 3: diagonal asterisk (∗)
    {{3,0,3,0,3},
     {0,5,5,5,0},
     {3,5,7,5,3},
     {0,5,5,5,0},
     {3,0,3,0,3}},
    // 4: full plus (✻)
    {{0,0,7,0,0},
     {0,3,7,3,0},
     {7,7,7,7,7},
     {0,3,7,3,0},
     {0,0,7,0,0}},
    // 5: heavy fill (✽)
    {{0,3,7,3,0},
     {3,7,7,7,3},
     {7,7,7,7,7},
     {3,7,7,7,3},
     {0,3,7,3,0}},
};

// "done" celebration — arms up, jumping
const uint8_t CELEBRATE[ROWS][COLS] = {
    {7,0,0,0,0,0,0,0,0,0,0,0,7},  // hand tips
    {0,7,0,7,7,7,7,7,7,7,0,7,0},  // arms + head top
    {0,7,7,7,7,0,5,0,7,7,7,7,0},  // shoulders + eyes
    {0,0,7,5,5,5,5,5,5,5,7,0,0},  // upper body
    {0,0,0,5,5,5,5,5,5,5,0,0,0},  // lower body
    {0,0,0,7,0,7,0,7,0,7,0,0,0},  // legs splayed (in air)
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
    {0,0,0,0,0,0,0,0,0,0,0,0,0},
};

// "error" confused — small head shifted down with ? above
const uint8_t CONFUSED[ROWS][COLS] = {
    {0,0,0,0,0,7,7,0,0,0,0,0,0},  // ? top
    {0,0,0,0,7,0,0,7,0,0,0,0,0},  // ? sides
    {0,0,0,0,0,0,7,0,0,0,0,0,0},  // ? curve
    {0,0,0,0,0,7,0,0,0,0,0,0,0},  // ? dot
    {0,0,7,7,7,7,7,7,7,7,7,0,0},  // small head
    {0,7,7,5,0,5,5,5,0,5,7,7,0},  // head + eyes (no shoulders this row)
    {7,7,5,5,5,5,5,5,5,5,5,7,7},  // shoulders
    {0,0,7,0,7,0,0,0,7,0,7,0,0},  // legs
};

uint8_t frame[ROWS * COLS];

static inline uint8_t scaleBrightness(uint8_t v, uint16_t scale256) {
    uint16_t scaled = (uint16_t)v * scale256 / 256;
    if (scaled > 7) scaled = 7;
    return (uint8_t)scaled;
}

void clearFrame() { memset(frame, 0, sizeof(frame)); }
void fillAll(uint8_t v) { memset(frame, v, sizeof(frame)); }

int triangle1000(unsigned long now, unsigned long periodMs) {
    unsigned long t = now % periodMs;
    long half = (long)periodMs / 2;
    long tri = (long)t - half;
    if (tri < 0) tri = -tri;
    return (int)(1000 - (tri * 1000 / half));
}

void drawSprite(const uint8_t sprite[ROWS][COLS], int yShift, int xShift,
                uint16_t brightScale256) {
    for (uint8_t r = 0; r < ROWS; r++) {
        int fy = (int)r + yShift;
        if (fy < 0 || fy >= ROWS) continue;
        for (uint8_t c = 0; c < COLS; c++) {
            int fx = (int)c + xShift;
            if (fx < 0 || fx >= COLS) continue;
            uint8_t v = sprite[r][c];
            if (v == 0) continue;
            uint8_t scaled = scaleBrightness(v, brightScale256);
            if (scaled > frame[fy * COLS + fx]) frame[fy * COLS + fx] = scaled;
        }
    }
}

void drawLegs(const uint8_t legs[COLS], int row, int xShift,
              uint16_t brightScale256) {
    if (row < 0 || row >= ROWS) return;
    for (uint8_t c = 0; c < COLS; c++) {
        int fx = (int)c + xShift;
        if (fx < 0 || fx >= COLS) continue;
        uint8_t v = legs[c];
        if (v == 0) continue;
        uint8_t scaled = scaleBrightness(v, brightScale256);
        if (scaled > frame[row * COLS + fx]) frame[row * COLS + fx] = scaled;
    }
}

void drawSpinner5x5(uint8_t frameIdx, int yTop, int xLeft,
                    uint16_t brightScale256) {
    for (uint8_t r = 0; r < 5; r++) {
        int fy = yTop + r;
        if (fy < 0 || fy >= ROWS) continue;
        for (uint8_t c = 0; c < 5; c++) {
            int fx = xLeft + c;
            if (fx < 0 || fx >= COLS) continue;
            uint8_t v = SPINNER[frameIdx][r][c];
            if (v == 0) continue;
            uint8_t scaled = scaleBrightness(v, brightScale256);
            if (scaled > frame[fy * COLS + fx]) frame[fy * COLS + fx] = scaled;
        }
    }
}

void renderIdle(unsigned long now) {
    clearFrame();
    uint16_t bright = 180 + (uint16_t)((triangle1000(now, 3000) * 76) / 1000);
    drawSprite(BODY, 0, 0, bright);
    drawLegs(LEGS_STAND, 5, 0, bright);
}

void renderThinking(unsigned long now) {
    // Claude Code's signature 6-frame spinner cycling at ~120ms.
    clearFrame();
    uint8_t idx = (uint8_t)((now / 120) % 6);
    int yTop = 1;        // spinner sits in upper half so it's visually centered
    int xLeft = 4;       // (13 - 5)/2 = 4
    drawSpinner5x5(idx, yTop, xLeft, 256);
}

void renderTool(unsigned long now) {
    // Fast walk + edge-tick arm flashes (energetic execution)
    clearFrame();
    bool stepA = ((now / 100) & 1) == 0;
    int yBob = stepA ? 0 : -1;
    drawSprite(BODY, yBob, 0, 256);
    drawLegs(stepA ? LEGS_A : LEGS_B, 5 + yBob, 0, 256);
    if ((now / 80) & 1) {
        frame[3 * COLS + 0] = 7;
        frame[3 * COLS + COLS - 1] = 7;
    }
}

void renderDone(unsigned long now) {
    // Show celebration pose for ~700ms, then fade to idle.
    unsigned long age = now - stateEnteredAt;
    clearFrame();
    if (age < 700) {
        // Bob up and down slightly during celebration
        int yBob = ((age / 140) & 1) ? 0 : -1;
        drawSprite(CELEBRATE, yBob, 0, 256);
    } else if (age < 900) {
        // Brief fadeout
        uint16_t b = 256 - (uint16_t)((age - 700) * 256 / 200);
        drawSprite(CELEBRATE, 0, 0, b);
    } else {
        currentState = ST_IDLE;
        stateEnteredAt = now;
        renderIdle(now);
    }
}

void renderNotify(unsigned long now) {
    clearFrame();
    uint16_t bright = 128 + (uint16_t)((triangle1000(now, 900) * 200) / 1000);
    drawSprite(BODY, 0, 0, bright);
    drawLegs(LEGS_STAND, 5, 0, bright);
}

void renderError(unsigned long now) {
    // Confused pose with subtle horizontal wobble + blinking ?
    clearFrame();
    int shake = ((now / 200) & 1) ? 0 : 1;
    // Blink the ? on/off every 400ms for "thinking puzzled" effect
    bool showQ = ((now / 400) & 1) == 0;
    if (showQ) {
        drawSprite(CONFUSED, 0, shake, 256);
    } else {
        // Hide ?, just show body
        for (uint8_t r = 4; r < 8; r++) {
            for (uint8_t c = 0; c < COLS; c++) {
                int fx = (int)c + shake;
                if (fx < 0 || fx >= COLS) continue;
                uint8_t v = CONFUSED[r][c];
                if (v == 0) continue;
                if (v > frame[r * COLS + fx]) frame[r * COLS + fx] = v;
            }
        }
    }
}

bool set_state(String name) {
    State next = currentState;
    if      (name == "idle")     next = ST_IDLE;
    else if (name == "thinking") next = ST_THINKING;
    else if (name == "tool")     next = ST_TOOL;
    else if (name == "done")     next = ST_DONE;
    else if (name == "notify")   next = ST_NOTIFY;
    else if (name == "error")    next = ST_ERROR;
    else return false;
    if (next != currentState) {
        currentState = next;
        stateEnteredAt = millis();
    }
    return true;
}

void setup() {
    matrix.begin();
    matrix.setGrayscaleBits(3);
    Bridge.begin();
    Bridge.provide("set_state", set_state);
    stateEnteredAt = millis();
}

void loop() {
    unsigned long now = millis();
    switch (currentState) {
        case ST_IDLE:     renderIdle(now);     break;
        case ST_THINKING: renderThinking(now); break;
        case ST_TOOL:     renderTool(now);     break;
        case ST_DONE:     renderDone(now);     break;
        case ST_NOTIFY:   renderNotify(now);   break;
        case ST_ERROR:    renderError(now);    break;
    }
    matrix.draw(frame);
    delay(20);
}
