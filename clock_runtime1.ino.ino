/***** Word-Clock for Arduino UNO: WS2812B 16×16 + DS3231 (NO SD, IN-MEMORY) *****
 * Files removed: map.csv, steps.txt
 * - Word map and steps are compiled into PROGMEM.
 *
 * Wiring (UNO):
 *   LEDs  : DATA D6 (changeable)
 *   RTC   : DS3231 on I2C (A4=SDA, A5=SCL)
 *******************************************************************************/

#include <FastLED.h>
#include <avr/pgmspace.h>

// ---------------- Hardware ----------------
#define DATA_PIN     6
#define LED_TYPE     WS2812B
#define COLOR_ORDER  GRB
#define MATRIX_W     16
#define MATRIX_H     16
#define NUM_LEDS     (MATRIX_W * MATRIX_H)
#define SERPENTINE   1
#define BRIGHTNESS   10
//#define ACTIVE_COLOR CRGB::White
//#define ACTIVE_COLOR CRGB(255, 214, 170)
#define ACTIVE_COLOR CRGB::Blue

#define INACTIVE_COLOR CRGB::Black
#define DEBUG_LED_PIN LED_BUILTIN
#define ROTATION_DEG 0
#define ROWS_TOP_TO_BOTTOM 1

CRGB leds[NUM_LEDS];

const char W_TWELVE[]  PROGMEM = "TWELVE";
const char W_OCLOCK[]  PROGMEM = "O'CLOCK";
const char W_five[]    PROGMEM = "five";     // minutes (lowercase)
const char W_PAST[]    PROGMEM = "PAST";
const char W_ten[]     PROGMEM = "ten";      // minutes (lowercase)
const char W_QUARTER[] PROGMEM = "QUARTER";
const char W_TWENTY[]  PROGMEM = "TWENTY";
const char W_HALF[]    PROGMEM = "HALF";
const char W_TO[]      PROGMEM = "TO";
const char W_ONE[]     PROGMEM = "ONE";
const char W_TWO[]     PROGMEM = "TWO";
const char W_THREE[]   PROGMEM = "THREE";
const char W_FOUR[]    PROGMEM = "FOUR";
const char W_FIVE[]    PROGMEM = "FIVE";     // hour (uppercase)
const char W_SIX[]     PROGMEM = "SIX";
const char W_SEVEN[]   PROGMEM = "SEVEN";
const char W_EIGHT[]   PROGMEM = "EIGHT";
const char W_NINE[]    PROGMEM = "NINE";
const char W_TEN[]     PROGMEM = "TEN";      // hour (uppercase)
const char W_ELEVEN[]  PROGMEM = "ELEVEN";

// ---------------- Map / Parsing ----------------
struct WordEntry {
  const char *name_P;  // points to PROGMEM string
  uint8_t row, col, len;
  int8_t dx, dy;
};

// Direction helpers
#define DIR_E  1,0
#define DIR_S  0,1
#define DIR_SE 1,1

// ----- DEFINE YOUR WORD MAP HERE -----
// Tip: keep everything zero-based for row/col.
// Example rows/cols below are placeholders; replace with your real map.
// ----- REPLACE YOUR WORDS[] WITH THIS -----

const WordEntry WORDS[] = {
  { W_TWELVE,  8, 11, 6, 0, 1 },   // S
  { W_OCLOCK, 14,  3, 7, 1, 0 },   // E
  { W_five,    2,  1, 4, 1, 1 },   // SE (minutes)
  { W_PAST,    3, 11, 4, 1, 0 },   // E
  { W_ten,     2,  4, 3, 1, 0 },   // E (minutes)
  { W_QUARTER, 1,  3, 7, 1, 0 },   // E
  { W_TWENTY,  1,  7, 6, 0, 1 },   // S
  { W_HALF,    2,  3, 4, 1, 1 },   // SE
  { W_TO,      6, 10, 2, 1, 0 },   // E
  { W_ONE,     9, 12, 3, 1, 1 },   // SE
  { W_TWO,     8, 10, 3, 1, 1 },   // E  (change back to 2 if your stencil really has only 'TW')
  { W_THREE,  10,  1, 5, 0, 1 },   // S
  { W_FOUR,    7,  2, 4, 1, 1 },   // SE
  { W_FIVE,    2,  1, 4, 1, 1 },   // SE (hour word, shared letters)
  { W_SIX,     7,  8, 3, 1, 1 },   // SE
  { W_SEVEN,  12,  6, 5, 1, 0 },   // E
  { W_EIGHT,  10, 11, 5, 1, 1 },   // SE
  { W_NINE,    8,  6, 4, 0, 1 },   // S
  { W_TEN,     9,  3, 3, 0, 1 },   // S (hour word)
  { W_ELEVEN,  8,  2, 6, 0, 1 },   // S
};
const uint8_t WORD_COUNT = sizeof(WORDS) / sizeof(WORDS[0]);

static WordEntry ACTIVE_WORDS[sizeof(WORDS)/sizeof(WORDS[0])];

static inline void rotatePoint(uint8_t x, uint8_t y, uint8_t &rx, uint8_t &ry) {
    switch (ROTATION_DEG) {
      case 0:   rx = x;                    ry = y;                    break;
      case 90:  rx = MATRIX_W - 1 - y;     ry = x;                    break; // CW
      case 180: rx = MATRIX_W - 1 - x;     ry = MATRIX_H - 1 - y;     break;
      case 270: rx = y;                    ry = MATRIX_H - 1 - x;     break; // CCW
      default:  rx = x;                    ry = y;                    break;
    }
}

static inline void rotateVec(int8_t dx, int8_t dy, int8_t &rdx, int8_t &rdy) {
  switch (ROTATION_DEG) {
    case 0:   rdx = dx;           rdy = dy;           break;
    case 90:  rdx = -dy;          rdy = dx;           break; // CW
    case 180: rdx = -dx;          rdy = -dy;          break;
    case 270: rdx = dy;           rdy = -dx;          break; // CCW
    default:  rdx = dx;           rdy = dy;           break;
  }
}

static void applyRotation() {
  for (uint8_t i = 0; i < WORD_COUNT; i++) {
    const WordEntry &w = WORDS[i];
    WordEntry out = w; // copy name_P, len
    rotatePoint(w.col, w.row, out.col, out.row);
    rotateVec(w.dx, w.dy, out.dx, out.dy);
    ACTIVE_WORDS[i] = out;
  }
}

// ---------------- Steps (144 lines) ----------------
// Each step is the space-separated list of WORD names to light, case-sensitive,
// exactly matching the names in WORDS (e.g., "IT IS FIVE PAST TEN_H").
// Store each line as a PROGMEM string, then index via the STEPS[] table.
/// ----- STEPS: 144 lines in PROGMEM (matches your steps.txt) -----
const char STEP_000[] PROGMEM = "TWELVE, O'CLOCK";
const char STEP_001[] PROGMEM = "five, PAST, TWELVE";
const char STEP_002[] PROGMEM = "ten, PAST, TWELVE";
const char STEP_003[] PROGMEM = "QUARTER, PAST, TWELVE";
const char STEP_004[] PROGMEM = "TWENTY, PAST, TWELVE";
const char STEP_005[] PROGMEM = "TWENTY, five, PAST, TWELVE";
const char STEP_006[] PROGMEM = "HALF, PAST, TWELVE";
const char STEP_007[] PROGMEM = "TWENTY, five, TO, ONE";
const char STEP_008[] PROGMEM = "TWENTY, TO, ONE";
const char STEP_009[] PROGMEM = "QUARTER, TO, ONE";
const char STEP_010[] PROGMEM = "ten, TO, ONE";
const char STEP_011[] PROGMEM = "five, TO, ONE";

const char STEP_012[] PROGMEM = "ONE, O'CLOCK";
const char STEP_013[] PROGMEM = "five, PAST, ONE";
const char STEP_014[] PROGMEM = "ten, PAST, ONE";
const char STEP_015[] PROGMEM = "QUARTER, PAST, ONE";
const char STEP_016[] PROGMEM = "TWENTY, PAST, ONE";
const char STEP_017[] PROGMEM = "TWENTY, five, PAST, ONE";
const char STEP_018[] PROGMEM = "HALF, PAST, ONE";
const char STEP_019[] PROGMEM = "TWENTY, five, TO, TWO";
const char STEP_020[] PROGMEM = "TWENTY, TO, TWO";
const char STEP_021[] PROGMEM = "QUARTER, TO, TWO";
const char STEP_022[] PROGMEM = "ten, TO, TWO";
const char STEP_023[] PROGMEM = "five, TO, TWO";

const char STEP_024[] PROGMEM = "TWO, O'CLOCK";
const char STEP_025[] PROGMEM = "five, PAST, TWO";
const char STEP_026[] PROGMEM = "ten, PAST, TWO";
const char STEP_027[] PROGMEM = "QUARTER, PAST, TWO";
const char STEP_028[] PROGMEM = "TWENTY, PAST, TWO";
const char STEP_029[] PROGMEM = "TWENTY, five, PAST, TWO";
const char STEP_030[] PROGMEM = "HALF, PAST, TWO";
const char STEP_031[] PROGMEM = "TWENTY, five, TO, THREE";
const char STEP_032[] PROGMEM = "TWENTY, TO, THREE";
const char STEP_033[] PROGMEM = "QUARTER, TO, THREE";
const char STEP_034[] PROGMEM = "ten, TO, THREE";
const char STEP_035[] PROGMEM = "five, TO, THREE";

const char STEP_036[] PROGMEM = "THREE, O'CLOCK";
const char STEP_037[] PROGMEM = "five, PAST, THREE";
const char STEP_038[] PROGMEM = "ten, PAST, THREE";
const char STEP_039[] PROGMEM = "QUARTER, PAST, THREE";
const char STEP_040[] PROGMEM = "TWENTY, PAST, THREE";
const char STEP_041[] PROGMEM = "TWENTY, five, PAST, THREE";
const char STEP_042[] PROGMEM = "HALF, PAST, THREE";
const char STEP_043[] PROGMEM = "TWENTY, five, TO, FOUR";
const char STEP_044[] PROGMEM = "TWENTY, TO, FOUR";
const char STEP_045[] PROGMEM = "QUARTER, TO, FOUR";
const char STEP_046[] PROGMEM = "ten, TO, FOUR";
const char STEP_047[] PROGMEM = "five, TO, FOUR";

const char STEP_048[] PROGMEM = "FOUR, O'CLOCK";
const char STEP_049[] PROGMEM = "five, PAST, FOUR";
const char STEP_050[] PROGMEM = "ten, PAST, FOUR";
const char STEP_051[] PROGMEM = "QUARTER, PAST, FOUR";
const char STEP_052[] PROGMEM = "TWENTY, PAST, FOUR";
const char STEP_053[] PROGMEM = "TWENTY, five, PAST, FOUR";
const char STEP_054[] PROGMEM = "HALF, PAST, FOUR";
const char STEP_055[] PROGMEM = "TWENTY, five, TO, FIVE";
const char STEP_056[] PROGMEM = "TWENTY, TO, FIVE";
const char STEP_057[] PROGMEM = "QUARTER, TO, FIVE";
const char STEP_058[] PROGMEM = "ten, TO, FIVE";
const char STEP_059[] PROGMEM = "five, TO, FIVE";

const char STEP_060[] PROGMEM = "FIVE, O'CLOCK";
const char STEP_061[] PROGMEM = "five, PAST, FIVE";
const char STEP_062[] PROGMEM = "ten, PAST, FIVE";
const char STEP_063[] PROGMEM = "QUARTER, PAST, FIVE";
const char STEP_064[] PROGMEM = "TWENTY, PAST, FIVE";
const char STEP_065[] PROGMEM = "TWENTY, five, PAST, FIVE";
const char STEP_066[] PROGMEM = "HALF, PAST, FIVE";
const char STEP_067[] PROGMEM = "TWENTY, five, TO, SIX";
const char STEP_068[] PROGMEM = "TWENTY, TO, SIX";
const char STEP_069[] PROGMEM = "QUARTER, TO, SIX";
const char STEP_070[] PROGMEM = "ten, TO, SIX";
const char STEP_071[] PROGMEM = "five, TO, SIX";

const char STEP_072[] PROGMEM = "SIX, O'CLOCK";
const char STEP_073[] PROGMEM = "five, PAST, SIX";
const char STEP_074[] PROGMEM = "ten, PAST, SIX";
const char STEP_075[] PROGMEM = "QUARTER, PAST, SIX";
const char STEP_076[] PROGMEM = "TWENTY, PAST, SIX";
const char STEP_077[] PROGMEM = "TWENTY, five, PAST, SIX";
const char STEP_078[] PROGMEM = "HALF, PAST, SIX";
const char STEP_079[] PROGMEM = "TWENTY, five, TO, SEVEN";
const char STEP_080[] PROGMEM = "TWENTY, TO, SEVEN";
const char STEP_081[] PROGMEM = "QUARTER, TO, SEVEN";
const char STEP_082[] PROGMEM = "ten, TO, SEVEN";
const char STEP_083[] PROGMEM = "five, TO, SEVEN";

const char STEP_084[] PROGMEM = "SEVEN, O'CLOCK";
const char STEP_085[] PROGMEM = "five, PAST, SEVEN";
const char STEP_086[] PROGMEM = "ten, PAST, SEVEN";
const char STEP_087[] PROGMEM = "QUARTER, PAST, SEVEN";
const char STEP_088[] PROGMEM = "TWENTY, PAST, SEVEN";
const char STEP_089[] PROGMEM = "TWENTY, five, PAST, SEVEN";
const char STEP_090[] PROGMEM = "HALF, PAST, SEVEN";
const char STEP_091[] PROGMEM = "TWENTY, five, TO, EIGHT";
const char STEP_092[] PROGMEM = "TWENTY, TO, EIGHT";
const char STEP_093[] PROGMEM = "QUARTER, TO, EIGHT";
const char STEP_094[] PROGMEM = "ten, TO, EIGHT";
const char STEP_095[] PROGMEM = "five, TO, EIGHT";

const char STEP_096[] PROGMEM = "EIGHT, O'CLOCK";
const char STEP_097[] PROGMEM = "five, PAST, EIGHT";
const char STEP_098[] PROGMEM = "ten, PAST, EIGHT";
const char STEP_099[] PROGMEM = "QUARTER, PAST, EIGHT";
const char STEP_100[] PROGMEM = "TWENTY, PAST, EIGHT";
const char STEP_101[] PROGMEM = "TWENTY, five, PAST, EIGHT";
const char STEP_102[] PROGMEM = "HALF, PAST, EIGHT";
const char STEP_103[] PROGMEM = "TWENTY, five, TO, NINE";
const char STEP_104[] PROGMEM = "TWENTY, TO, NINE";
const char STEP_105[] PROGMEM = "QUARTER, TO, NINE";
const char STEP_106[] PROGMEM = "ten, TO, NINE";
const char STEP_107[] PROGMEM = "five, TO, NINE";

const char STEP_108[] PROGMEM = "NINE, O'CLOCK";
const char STEP_109[] PROGMEM = "five, PAST, NINE";
const char STEP_110[] PROGMEM = "ten, PAST, NINE";
const char STEP_111[] PROGMEM = "QUARTER, PAST, NINE";
const char STEP_112[] PROGMEM = "TWENTY, PAST, NINE";
const char STEP_113[] PROGMEM = "TWENTY, five, PAST, NINE";
const char STEP_114[] PROGMEM = "HALF, PAST, NINE";
const char STEP_115[] PROGMEM = "TWENTY, five, TO, TEN";
const char STEP_116[] PROGMEM = "TWENTY, TO, TEN";
const char STEP_117[] PROGMEM = "QUARTER, TO, TEN";
const char STEP_118[] PROGMEM = "ten, TO, TEN";
const char STEP_119[] PROGMEM = "five, TO, TEN";

const char STEP_120[] PROGMEM = "TEN, O'CLOCK";
const char STEP_121[] PROGMEM = "five, PAST, TEN";
const char STEP_122[] PROGMEM = "ten, PAST, TEN";
const char STEP_123[] PROGMEM = "QUARTER, PAST, TEN";
const char STEP_124[] PROGMEM = "TWENTY, PAST, TEN";
const char STEP_125[] PROGMEM = "TWENTY, five, PAST, TEN";
const char STEP_126[] PROGMEM = "HALF, PAST, TEN";
const char STEP_127[] PROGMEM = "TWENTY, five, TO, ELEVEN";
const char STEP_128[] PROGMEM = "TWENTY, TO, ELEVEN";
const char STEP_129[] PROGMEM = "QUARTER, TO, ELEVEN";
const char STEP_130[] PROGMEM = "ten, TO, ELEVEN";
const char STEP_131[] PROGMEM = "five, TO, ELEVEN";

const char STEP_132[] PROGMEM = "ELEVEN, O'CLOCK";
const char STEP_133[] PROGMEM = "five, PAST, ELEVEN";
const char STEP_134[] PROGMEM = "ten, PAST, ELEVEN";
const char STEP_135[] PROGMEM = "QUARTER, PAST, ELEVEN";
const char STEP_136[] PROGMEM = "TWENTY, PAST, ELEVEN";
const char STEP_137[] PROGMEM = "TWENTY, five, PAST, ELEVEN";
const char STEP_138[] PROGMEM = "HALF, PAST, ELEVEN";
const char STEP_139[] PROGMEM = "TWENTY, five, TO, TWELVE";
const char STEP_140[] PROGMEM = "TWENTY, TO, TWELVE";
const char STEP_141[] PROGMEM = "QUARTER, TO, TWELVE";
const char STEP_142[] PROGMEM = "ten, TO, TWELVE";
const char STEP_143[] PROGMEM = "five, TO, TWELVE";

// Pointer table to all 144 lines (order matters!)
const char * const STEPS[144] PROGMEM = {
  STEP_000, STEP_001, STEP_002, STEP_003, STEP_004, STEP_005, STEP_006, STEP_007, STEP_008, STEP_009, STEP_010, STEP_011,
  STEP_012, STEP_013, STEP_014, STEP_015, STEP_016, STEP_017, STEP_018, STEP_019, STEP_020, STEP_021, STEP_022, STEP_023,
  STEP_024, STEP_025, STEP_026, STEP_027, STEP_028, STEP_029, STEP_030, STEP_031, STEP_032, STEP_033, STEP_034, STEP_035,
  STEP_036, STEP_037, STEP_038, STEP_039, STEP_040, STEP_041, STEP_042, STEP_043, STEP_044, STEP_045, STEP_046, STEP_047,
  STEP_048, STEP_049, STEP_050, STEP_051, STEP_052, STEP_053, STEP_054, STEP_055, STEP_056, STEP_057, STEP_058, STEP_059,
  STEP_060, STEP_061, STEP_062, STEP_063, STEP_064, STEP_065, STEP_066, STEP_067, STEP_068, STEP_069, STEP_070, STEP_071,
  STEP_072, STEP_073, STEP_074, STEP_075, STEP_076, STEP_077, STEP_078, STEP_079, STEP_080, STEP_081, STEP_082, STEP_083,
  STEP_084, STEP_085, STEP_086, STEP_087, STEP_088, STEP_089, STEP_090, STEP_091, STEP_092, STEP_093, STEP_094, STEP_095,
  STEP_096, STEP_097, STEP_098, STEP_099, STEP_100, STEP_101, STEP_102, STEP_103, STEP_104, STEP_105, STEP_106, STEP_107,
  STEP_108, STEP_109, STEP_110, STEP_111, STEP_112, STEP_113, STEP_114, STEP_115, STEP_116, STEP_117, STEP_118, STEP_119,
  STEP_120, STEP_121, STEP_122, STEP_123, STEP_124, STEP_125, STEP_126, STEP_127, STEP_128, STEP_129, STEP_130, STEP_131,
  STEP_132, STEP_133, STEP_134, STEP_135, STEP_136, STEP_137, STEP_138, STEP_139, STEP_140, STEP_141, STEP_142, STEP_143
};
// ---------------- Helpers ----------------
static inline uint16_t xyToIndex(uint8_t x, uint8_t y) {
  if (SERPENTINE) {
    return (y & 1) ? (y * MATRIX_W + (MATRIX_W - 1 - x)) : (y * MATRIX_W + x);
  }
  return y * MATRIX_W + x;
}

static inline void clearMatrix() { fill_solid(leds, NUM_LEDS, INACTIVE_COLOR); }

static inline void paintWord(const WordEntry &w, const CRGB &color) {
  int x = w.col, y = w.row;
  for (uint8_t i = 0; i < w.len; i++) {
    if ((uint8_t)x < MATRIX_W && (uint8_t)y < MATRIX_H) {
      uint8_t yy = ROWS_TOP_TO_BOTTOM ? (uint8_t)(MATRIX_H - 1 - (uint8_t)y) : (uint8_t)y;
      leds[xyToIndex((uint8_t)x, yy)] = color;
    }
    x += w.dx; y += w.dy;
  }
}

// Compare RAM string to PROGMEM string
static bool str_eq_ram_pgm(const char *ram, const char *pgm) {
  while (true) {
    char cr = *ram++;
    char cp = pgm_read_byte(pgm++);
    if (cr != cp) return false;
    if (cr == 0) return true;
  }
}

// Find word index by exact name (RAM token vs WORDS[i].name_P in PROGMEM)
static int16_t findWordIndex(const char *nameExactRAM) {
  for (uint8_t i = 0; i < WORD_COUNT; i++) {
    if (str_eq_ram_pgm(nameExactRAM, ACTIVE_WORDS[i].name_P)) return i;
  }
  return -1;
}

// 0..143 from time
static inline uint16_t stepFromTime(uint8_t hour, uint8_t minute) {
  uint8_t h12 = hour % 12;
  return (h12 * 60u + (uint16_t)minute) / 5u;  // 0..143
}

// Render a specific step by tokenizing the PROGMEM step line
bool renderStep(uint16_t step) {
  if (step >= 144) return false;

  // Copy the step line from PROGMEM to a RAM buffer, then tokenize
  char line[200];
  const char *ptrPGM = (const char*)pgm_read_ptr(&STEPS[step]);
  if (!ptrPGM) return false;
  strcpy_P(line, ptrPGM);

  // Normalize commas to spaces (if you kept commas)
  for (char *q = line; *q; ++q) if (*q == ',') *q = ' ';

  clearMatrix();

  char *ctx = NULL;
  char *tok = strtok_r(line, " \t", &ctx);
  while (tok) {
    int16_t wi = findWordIndex(tok);   // case-sensitive exact match
    if (wi >= 0) {
      paintWord(ACTIVE_WORDS[wi], ACTIVE_COLOR);
    }
    tok = strtok_r(NULL, " \t", &ctx);
  }

  FastLED.show();
  return true;
}

// ---------------- Timing ----------------
unsigned long lastTick = 0;
uint16_t currentStep = 0xFFFF;


// ---------------- Setup / Loop ----------------
void setup() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(BRIGHTNESS);
  clearMatrix(); FastLED.show();
  applyRotation();
  Serial.begin(115200);
  delay(20);

  // Initial draw
  renderStep(0);
  pinMode(LED_BUILTIN, OUTPUT);

}

void loop() {

  
  unsigned long nowMs = millis();
  if (nowMs - lastTick < 1000) return;
  lastTick = nowMs;

  uint8_t hh, mm;
  static uint32_t startMs = millis();
  uint32_t mins = (millis() - startMs) / 500UL;
  hh = (mins / 60) % 24;
  mm = mins % 60;

  Serial.print(F("  time=")); Serial.print(hh);
  Serial.print(F(":"));
  if (mm < 10) Serial.print('0');
  Serial.println(mm);


  uint16_t step = stepFromTime(hh, mm);
  if (step != currentStep) {
    currentStep = step;
    renderStep(step);
    digitalWrite(DEBUG_LED_PIN, HIGH);
    delay(120);
    digitalWrite(DEBUG_LED_PIN, LOW);

  }
}