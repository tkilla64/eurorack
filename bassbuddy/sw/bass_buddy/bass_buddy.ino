/*
 * Bass sequencer for Erica Synths BASSLINE DIY Eurorack module
 * 
 * Code by Tommy Killander (tkilla64), https://github.com/tkilla64/eurorack
 * 
 * Credits:
 * Original hardware design and software code was made by HAGIWO, see 
 * https://note.com/solder_state/n/n7c2809976698
 * Many modifications are made by me to both the hardware and software
 * but the design around the basic sequence step and MCP4725 DAC control
 * can be tracked back to the original code.
 *
 * TODO:
 *  Random pattern generation
 */

#include <EEPROM.h>
#define  ENCODER_OPTIMIZE_INTERRUPTS
#include <Encoder.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// software version numbers
#define VER_MAJOR 2         // increment if not backwards compatible 
#define VER_MINOR 4         // minor changes (bugfixes, added features etc.)

// display setting
#define SCREEN_WIDTH  128   // OLED display width, in pixels
#define SCREEN_HEIGHT 64    // OLED display height, in pixels
//#define SHOW_CYCTIME  1     // Uncomment to show loop() cycle time in ms
// MMI
#define BLINK_RATE    600   // flash period time in ms
#define MAX_SHARD     7     // OLED update is divided into shards to even out load

#define ACCENT_LEVEL  255   // Output level of ACCENT_OUT

// Menu system
enum mMenuSystem 
{
  mPatternRun = 0,        // Main view for pattern run
  mLoadPattern,           // Quick change of pattern from PatternRun view
  mEditPatternMenu,       // Select step to edit
  mEditStepMenu,          // Select step parameter to edit
  mEditNote,              // Edit Note
  mEditAccent,            // Edit Accent On/Off
  mEditVcfCV,             // Edit VCF CV
  mEditSlide,             // Edit Slide On/Off
  mEditTieNext,           // Edit Tie to next tone On/Off
  mEditPattLink,          // Edit Pattern Link
  mEditPattRept,          // Edit Pattern Repeat
  mSystemMenu,            // Select system function
  mSystemNewPtn,          // Create new - pattern
  mSystemNewStp,          // Create new - steps
  mSystemCopyFrom,        // Copy pattern - from
  mSystemCopyTo,          // Copy pattern - to
  mSystemSavePtn,         // Save patterns to EEPROM
  mSystemLoadPtn,         // Load patterns from EEPROM
  mSystemTune,            // Tune VCO
  mSystemSetVRef,         // Set ADC VRef (+5VDC rail)
};

// pin definitions
#define ENC_A      2
#define ENC_B      3
#define GATE_OUT   4
#define VCF_OUT    5
#define CLOCK_IN   6
#define SLIDE_ON   7
#define ENC_SW     8
#define ACCENT_OUT 9
#define OLED_CLK   10    
#define OLED_DC    11
#define OLED_CS    12
#define OLED_RESET 13
#define OLED_MOSI  14
#define EDIT_SW    15
#define MENU_SW    16
#define RESET_IN   17

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT,
                        OLED_MOSI, OLED_CLK, OLED_DC, OLED_RESET, OLED_CS);
                        
Encoder encoder(ENC_A, ENC_B);

// strings stored in Flash
const char tied_str[]   PROGMEM = "  TIED:%s",
           accent_str[] PROGMEM = "ACCENT:%s",
           slide_str[]  PROGMEM = " SLIDE:%s",
           vcfcv_str[]  PROGMEM = "   VCF:%d",
           yes_str[]    PROGMEM = "YES",
           no_str[]     PROGMEM = "NO ",
           on_str[]     PROGMEM = "ON ",
           off_str[]    PROGMEM = "OFF",
           new_str[]    PROGMEM = "NEW ",
           copy_str[]   PROGMEM = "COPY",
           save_str[]   PROGMEM = "SAVE",
           load_str[]   PROGMEM = "LOAD",
           tune_str[]   PROGMEM = "TUNE",
           vref_str[]   PROGMEM = "VREF",
           chain_str[]  PROGMEM = "CHAIN",
           eeprom_str[] PROGMEM = "ALL PTN?",
           new_ptrn[]   PROGMEM = "PATTERN#%2d",
           new_step[]   PROGMEM = "# STEPS:%2d",
           from_str[]   PROGMEM = "PTN FR:%2d",
           to_str[]     PROGMEM = "PTN TO:%2d",
           menu_step[]  PROGMEM = "STEP:%02d",
           menu_ptrn[]  PROGMEM = "PTRN:#%01d",
           menu_edit[]  PROGMEM = "EDIT:#%1d",
           menu_sys[]   PROGMEM = "SYSMENU",
           erase_10[]   PROGMEM = "          ",
           erase_3[]    PROGMEM = "   ",
           estep_str[]  PROGMEM = "%02d:%s %c%c%c",
           rep1_str[]   PROGMEM = "CYC:%3d",
           rep2_str[]   PROGMEM = " OF:%3d",
           repeat_str[] PROGMEM = "CYCLES:%3d",
           link_str[]   PROGMEM = "LINK TO: %1d",
           ver_str[]    PROGMEM = "Ver %d.%02d",
           volt_str[]   PROGMEM = "%1d.%03d V";
           
const char notes[12][3] PROGMEM = { "C-", "C#", "D-", "D#", "E-", "F-",
                                    "F#", "G-", "G#", "A-", "A#", "B-" };

// global variables
long oldPosition = -999;
long newPosition = -999;
#ifdef SHOW_CYCTIME
unsigned long cycle = 0;
unsigned long starttime;
#endif
int enc_step = 0;
int revert_vref;
word revert_step;
char string[16] = "";
char fmt_str[12] = "";
bool blink;
bool rising_edge = 0;
bool reset_edge = 0;
bool enc_sw = 0;
bool menu_sw = 0;
bool edit_sw = 0;
bool update_display = 0;
int shard = 0;
int menu_mode = mPatternRun;
int sys_menu = 0;
int edit_step = 0;
int edit_pattern = 0;
int edit_param0 = 0;
int edit_param1 = 1;
int step_param = 0;
int tuning = 0;

// Pattern definitions
#define MAX_PATTERN 10    // maximum patterns in memory
#define MAX_STEPS   16    // maximum steps in pattern
#define MAX_REPEATS 256   // maximum repeats of a pattern
// Step layout
// 7 bits VCF 
// 3 bits Tie, Accent, Slide
// 6 bits Note = 0-49
#define NOTE_MASK   0b0000000000111111
#define SLIDE_MASK  0b0000000001000000
#define ACCENT_MASK 0b0000000010000000
#define TIE_MASK    0b0000000100000000
#define VCF_MASK    0b1111111000000000
#define MAGIC_NBR   12355                   // Change magic number when changing contents of nonVolatileData struct!
// Pattern Length + Link
#define PLEN_MASK   0b00001111
#define PLNK_MASK   0b11110000
// Pattern
struct nonVolatileData 
{
  word pattern[MAX_PATTERN][MAX_STEPS];     // pattern storage
  byte pattlen[MAX_PATTERN];                // length of pattern and link to next
  byte pattrep[MAX_PATTERN];                // no of repeats 0-255
  int  VRef;                                // DAC VRef in mV (+5VDC rail)
  int  magic;                               // magic number to init empty EEPROM
};

nonVolatileData EE;

int curr_pattern = 0;
int next_pattern = 0;
int pattern_step = 0;
int curr_repeat = 0;
int pattern_size;
int repeat_pattern;

void setup() 
{
  eepromDataLoad();
  pattern_size = (EE.pattlen[curr_pattern] & PLEN_MASK)+1;
  repeat_pattern = EE.pattrep[curr_pattern]+1;
  display.begin(SSD1306_SWITCHCAPVCC);
  // setup pins
  pinMode(CLOCK_IN, INPUT);
  pinMode(RESET_IN, INPUT);
  pinMode(GATE_OUT, OUTPUT);
  pinMode(ACCENT_OUT, OUTPUT);
  pinMode(EDIT_SW, INPUT_PULLUP);
  pinMode(MENU_SW, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);
  pinMode(SLIDE_ON, OUTPUT);
  pinMode(VCF_OUT, OUTPUT);
  digitalWrite(GATE_OUT, LOW);
  digitalWrite(SLIDE_ON, LOW);
  analogWrite(ACCENT_OUT, 0);
  analogWrite(VCF_OUT, 0);
  // setup I2C
  Wire.begin();
  Wire.setClock(400000);
  oldPosition = encoder.read();
  // startup screen
  drawSplashScreen();
  delay(1500);
  drawPatternRunMenu();
}

void loop()
{
#ifdef SHOW_CYCTIME
  // Instrumentation:
  starttime = millis();
#endif
  // Handle RESET_IN - detect rising edge
  if (digitalRead(RESET_IN) == HIGH && reset_edge == 0)
  {
    reset_edge = 1;
    pattern_step = 0;
  }      
  // Handle CLOCK_IN 
  // Rising edge; set DAC+ACCENT+VCF+GATE
  if (digitalRead(CLOCK_IN) == HIGH && rising_edge == 0)
  {
    rising_edge = 1;
    update_display = 1;
    // Setup CV and Accent. Tuning mode will override sequencer notes
    if (tuning == 0)
      DAC(noteToCV((unsigned int)EE.pattern[curr_pattern][pattern_step] & NOTE_MASK), 0x60);
    else
      DAC(noteToCV(tuning), 0x60);
    // GATE_OUT = On (if note is not rest)
    if ((EE.pattern[curr_pattern][pattern_step] & NOTE_MASK) > 0)
    {
      digitalWrite(GATE_OUT, HIGH);
    }
    analogWrite(VCF_OUT, (int)((EE.pattern[curr_pattern][pattern_step] & VCF_MASK) >> 9) * 2);
    analogWrite(ACCENT_OUT, (int)((EE.pattern[curr_pattern][pattern_step] & ACCENT_MASK) >> 7) * ACCENT_LEVEL);
  }
  // Falling edge;  set SLIDE and Advance Pattern step
  if (digitalRead(CLOCK_IN) == LOW && rising_edge == 1)
  {
    rising_edge = 0;
    digitalWrite(SLIDE_ON, (EE.pattern[curr_pattern][pattern_step] & SLIDE_MASK) >> 6);
    // GATE_OUT = Off (if note is not tied to the next)
    if ((EE.pattern[curr_pattern][pattern_step] & TIE_MASK) == 0)
    {
      digitalWrite(GATE_OUT, LOW);        
    }    
    pattern_step++;    
    if (pattern_step >= pattern_size)
    {
      curr_repeat++;
      if (curr_repeat >= repeat_pattern)
      {
        curr_repeat = 0;
        // ignore repeat pattern if pattern have been selected by user
        if (next_pattern == curr_pattern)
        {
          next_pattern = EE.pattlen[curr_pattern]>>4;
          repeat_pattern = EE.pattrep[next_pattern]+1;
        }
      }
      pattern_step = 0;
      curr_pattern = next_pattern;
      repeat_pattern = EE.pattrep[curr_pattern]+1;
      pattern_size = (EE.pattlen[curr_pattern] & PLEN_MASK)+1;
      if (menu_mode == mPatternRun)
      {
        strcpy_P(fmt_str, menu_ptrn);
        sprintf(string, fmt_str, curr_pattern);
        drawModeIndicator(string);
        drawRepeatIndicator(curr_repeat+1, repeat_pattern);
      }
    }
  }
  // detect falling edge of RESET_IN
  if (digitalRead(RESET_IN) == LOW && reset_edge == 1)
  {
    reset_edge = 0;
  }
  
  // MMI
  // Read encoder input and calculate incr/decr
  newPosition = encoder.read();
  if (newPosition / 4 > oldPosition / 4)
  {
    oldPosition = newPosition;
    enc_step = 1;
  }
  else if (newPosition / 4 < oldPosition / 4) 
  {
    oldPosition = newPosition;
    enc_step = -1;
  }
  // Read debounced button status
  enc_sw = debounceButton(0, ENC_SW);
  edit_sw = debounceButton(1, EDIT_SW);
  menu_sw = debounceButton(2, MENU_SW);
  // Menu system
  navigateMenuSystem();
  ++shard %= MAX_SHARD;     // cycle through shards
  blink = (millis() % BLINK_RATE) / (BLINK_RATE/2); // toggle state of blink
#ifdef SHOW_CYCTIME
  // Instrumentation: 
  // Print loop cycle-time (itself adds 2ms)
  sprintf(string, "%03d ms", cycle);
  display.setCursor(90,57);
  display.setTextSize(1);
  display.print(string);
#endif
  if (shard == 0 && update_display)
  {
    display.display();
    update_display = 0;
  }
#ifdef SHOW_CYCTIME
  cycle = millis() - starttime;
#endif
}

//
// MENU FSM
//
void navigateMenuSystem(void)
{
  switch (menu_mode)
  {
    case mPatternRun:       // Main view for pattern run
      drawPatternRun(shard);
      if (!digitalRead(ENC_SW))
      {
        menu_mode = mLoadPattern;
        drawLoadPatternMenu();
      }
      if (edit_sw)
      {
        menu_mode = mEditPatternMenu;
        edit_pattern = curr_pattern;
        edit_step = constrain(edit_step, 0, pattern_size-1);
        drawPatternEditMenu();
      }
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;
      
    case mLoadPattern:      // Quick change of pattern from PatternRun view
      if (edit_sw)
      {
        enc_step = -1;
      }
      if (menu_sw)
      {
        enc_step = 1;
      }
      drawLoadPattern();
      if (digitalRead(ENC_SW))
      {
        menu_mode = mPatternRun;
        drawPatternRunMenu();
        shard = 0;
        drawPatternRun(shard);
      }
      break;
      
    case mEditPatternMenu:  // Select step to edit
      drawPatternEdit();
      if (edit_sw)
      {
        menu_mode = mPatternRun;
        drawPatternRunMenu();
        shard = 0;
        drawPatternRun(shard);
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }
      if (menu_sw)
      {
        menu_mode = mEditPattLink;
        edit_param1 = EE.pattrep[edit_pattern];
        edit_param0 = EE.pattlen[edit_pattern] >> 4;
        drawEditLinkMenu();
      }
      break;

    case mEditPattLink:     // Edit Link to next Pattern
      drawEditLinkReptMenu(0);
      if (menu_sw)
      {
        menu_mode = mEditPatternMenu;
        drawPatternEditMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditPattRept; 
      }
      break;

    case mEditPattRept:     // Edit Repeat of this Pattern
      drawEditLinkReptMenu(1);
      if (menu_sw)
      {
        menu_mode = mEditPatternMenu;
        drawPatternEditMenu();
      }
      if (enc_sw)
      {
        EE.pattrep[edit_pattern] = edit_param1;
        EE.pattlen[edit_pattern] = (EE.pattlen[edit_pattern] & ~PLNK_MASK) | (edit_param0 << 4);
        menu_mode = mEditPatternMenu;
        drawPatternEditMenu();
      }
      break;
      
    case mEditStepMenu:     // Select step parameter to edit
      drawEditStepParam(shard);
      if (edit_sw)
      {
        menu_mode = mEditPatternMenu;
        drawPatternEditMenu();
      }
      if (enc_sw)
      {
        revert_step = EE.pattern[edit_pattern][edit_step];
        switch (step_param)
        {
          case 0:
            menu_mode = mEditNote;
            break;
          case 1:
            menu_mode = mEditTieNext;
            break;                
          case 2:
            menu_mode = mEditAccent;
            break;
          case 3:
            menu_mode = mEditSlide;
            break;    
          case 4:
            menu_mode = mEditVcfCV;
            break;
          default:
            break;
        }
      }
      break;
      
    case mEditNote:         // Edit Note
      drawEditNote();
      if (edit_sw)
      {
        menu_mode = mEditStepMenu;
        EE.pattern[edit_pattern][edit_step] = revert_step; // bail out
        drawEditStepParamMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }
      break;
            
    case mEditAccent:       // Edit Accent On/Off
      drawEditAccent();
      if (edit_sw)
      {
        menu_mode = mEditStepMenu;
        EE.pattern[edit_pattern][edit_step] = revert_step;
        drawEditStepParamMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }      
      break;

    case mEditVcfCV:        // Edit VCF CV
      drawEditVcfCV();
      if (edit_sw)
      {
        menu_mode = mEditStepMenu;
        EE.pattern[edit_pattern][edit_step] = revert_step;
        drawEditStepParamMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }
      break;

    case mEditSlide:        // Edit Slide On/Off
       drawEditSlide();
       if (edit_sw)
      {
        menu_mode = mEditStepMenu;
        EE.pattern[edit_pattern][edit_step] = revert_step;
        drawEditStepParamMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }      
      break;

    case mEditTieNext:      // Edit Tie to next note On/Off
      drawEditTieNext();
      if (edit_sw)
      {
        menu_mode = mEditStepMenu;
        EE.pattern[edit_pattern][edit_step] = revert_step;
        drawEditStepParamMenu();
      }
      if (enc_sw)
      {
        menu_mode = mEditStepMenu;
        drawEditStepParamMenu();
      }      
      break;

    case mSystemMenu:       // Select system function
      drawSystemMain();
      if (menu_sw)
      {
        menu_mode = mPatternRun;
        drawPatternRunMenu();
        shard = 0;
        drawPatternRun(shard);
      }
      if (enc_sw)
      {
        switch (sys_menu)
        {
          case 0:
            menu_mode = mSystemNewPtn;
            break;
          case 1:
            menu_mode = mSystemCopyFrom;
            break;
          case 2:  
            menu_mode = mSystemSavePtn;
            break;           
          case 3:
            menu_mode = mSystemLoadPtn;
            break;         
          case 4:
            menu_mode = mSystemTune;
            tuning = 1; // turn on tuning: C-0
            break;                    
          case 5:
            menu_mode = mSystemSetVRef;
            revert_vref = EE.VRef;
            break;            
          default:
            break;
        }
      }          
      break;

    case mSystemNewPtn:          // Create new - select pattern
      drawSystemNewPtnMenu(0);
      if (enc_sw)
      {
        menu_mode = mSystemNewStp;
        edit_step = 1;
      }
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;
      
    case mSystemNewStp:          // Create new - select # steps
      drawSystemNewPtnMenu(1);
      if (enc_sw)
      {
        for (int i=0 ; i<edit_step ; i++)
          EE.pattern[edit_pattern][i] = 0;
          
        EE.pattlen[edit_pattern] = (edit_pattern<<4)+((edit_step-1) & PLEN_MASK);
        EE.pattrep[edit_pattern] = 0;
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;

    case mSystemCopyFrom:         // Copy pattern - select from
      drawSystemCopyPtnMenu(0);
      if (enc_sw)
      {
        menu_mode = mSystemCopyTo;
      }
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;

    case mSystemCopyTo:           // Copy pattern - select to
      drawSystemCopyPtnMenu(1);
      if (enc_sw)
      {
        for (int i=0 ; i<(EE.pattlen[edit_param1] & PLEN_MASK)+1 ; i++)
          EE.pattern[edit_pattern][i] = EE.pattern[edit_param1][i];
          
        EE.pattlen[edit_pattern] = (EE.pattlen[edit_param1] & PLEN_MASK)+(edit_pattern<<4);
        EE.pattrep[edit_pattern] = 0;
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;
    
    case mSystemSavePtn:         // Save patterns to EEPROM or
    case mSystemLoadPtn:         // Load patterns from EEPROM
      drawSysEpromMenu();
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();        
      }
      if (enc_sw)
      {
        if (menu_mode == mSystemSavePtn)
          eepromDataSave();
        else
          eepromDataLoad();
          
        menu_mode = mPatternRun;
        drawPatternRunMenu();
        shard = 0;
        drawPatternRun(shard);      
      }
      break;

    case mSystemTune:            // Tune VCO
      drawSystemTuning();
      if (menu_sw)
      {
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
        // turn off Tuning mode
        tuning = 0;
      }
      break;
          
    case mSystemSetVRef:         // Set ADC VRef (+5VDC rail)
      drawSystemSetVRef();
      if (menu_sw)
      {
        EE.VRef = revert_vref;
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      if (enc_sw)
      {
        eepromDataSave();
        menu_mode = mSystemMenu;
        drawSystemMainMenu();
      }
      break;
      
    default:
      break;
  }
}

// PATTERN RUN VIEW
void drawPatternRunMenu(void)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.drawRoundRect(2, 2, 76, 62, 3, WHITE); // pattern box
  strcpy_P(fmt_str, menu_ptrn);
  sprintf(string, fmt_str, curr_pattern);
  drawModeIndicator(string);
  drawRepeatIndicator(curr_repeat+1, repeat_pattern);
  update_display = 1;
}
void drawPatternRun(int shard)
{
  char notestring[] = "---";
  char temp_str[16];
  strcpy_P(temp_str, estep_str);
  
  // calculate step to display (with wraparound)
  int this_step = ((pattern_step+(shard-3)) + pattern_size) % pattern_size;
  
  if ((EE.pattern[curr_pattern][this_step] & NOTE_MASK) > 0)
    getNoteString(notestring, (int)(EE.pattern[curr_pattern][this_step] & NOTE_MASK)-1);

  if (this_step == 0)
    display.setTextColor(BLACK, WHITE);
    
  display.setCursor(5,(shard*8)+5);  
  sprintf(string, temp_str, this_step+1, notestring, 
    EE.pattern[curr_pattern][this_step] & TIE_MASK ? 'T' : '-', 
    EE.pattern[curr_pattern][this_step] & ACCENT_MASK ? 'A' : '-', 
    EE.pattern[curr_pattern][this_step] & SLIDE_MASK ? 'S' : '-');
  display.print(string);
  display.setTextColor(WHITE, BLACK);
}

// SELECT PATTERN
void drawLoadPatternMenu(void)
{
  char temp_str[10];
  strcpy_P(temp_str, menu_ptrn);
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE, BLACK);  
  display.drawRoundRect(19, 18, 90, 24, 3, WHITE); // pattern box
  display.setCursor(23, 23);
  sprintf(string, temp_str, next_pattern);
  display.print(string);
  update_display = 1;
}
void drawLoadPattern(void)
{ 
  if (enc_step != 0)
  {
    next_pattern += enc_step;
    next_pattern = constrain(next_pattern, 0, MAX_PATTERN-1);
    enc_step = 0;
    display.setTextSize(2);
    display.setCursor(95, 23);
    sprintf(string, "%1d", next_pattern);
    display.print(string);
    update_display = 1;
  }
}

// EDIT PATTERN
void drawPatternEditMenu(void)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.drawRoundRect(0, 22, 127, 20, 3, WHITE); // edit box
  strcpy_P(fmt_str, menu_edit);
  sprintf(string, fmt_str, edit_pattern);
  drawModeIndicator(string);
  drawEditStep(edit_step);
  update_display = 1;
}
void drawPatternEdit(void)
{
  if (enc_step != 0)
  {
    edit_step += enc_step;
    edit_step = constrain(edit_step, 0, pattern_size-1);
    enc_step = 0;
    drawEditStep(edit_step);
    update_display = 1;
  }
}
void drawEditStep(int step)
{
  char notestring[4] = "---";
  char temp_str[16];
  strcpy_P(temp_str, estep_str);
  
  if ((EE.pattern[edit_pattern][step] & NOTE_MASK) > 0)
    getNoteString(notestring, (int)(EE.pattern[edit_pattern][step] & NOTE_MASK)-1);

  sprintf(string, temp_str, step+1, notestring, -
  +
    (EE.pattern[edit_pattern][step] & TIE_MASK) ? 'T' : '-', 
    (EE.pattern[edit_pattern][step] & ACCENT_MASK) ? 'A' : '-', 
    (EE.pattern[edit_pattern][step] & SLIDE_MASK) ? 'S' : '-');
  display.setCursor(3, 25);  
  display.setTextSize(2);
  display.print(string);
}

// EDIT STEP PARAMS
void drawEditStepParamMenu(void)
{
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);
  display.drawRoundRect(2, 2, 76, 62, 3, WHITE); // step edit box
  strcpy_P(fmt_str, menu_step);
  sprintf(string, fmt_str, edit_step+1);
  drawModeIndicator(string);
  update_display = 1;
}
void drawEditStepParam(int shard)
{
  char alt1[5], alt2[5];
  char erase_str[12];

  if (enc_step != 0)
  {
    step_param += enc_step;
    step_param = constrain(step_param, 0, 4);
    enc_step = 0;
  }
  if (shard > 0)
  {
    display.setCursor(10, ((shard+1)*10)+5);
    display.setTextSize(1);
    strcpy_P(erase_str, erase_10);
  }
  else
  {
    display.setCursor(22, 5);
    display.setTextSize(2);
    strcpy_P(erase_str, erase_3);
  }  
  switch (shard)
  {
    case 0:
      if ((EE.pattern[curr_pattern][edit_step] & NOTE_MASK) > 0)
        getNoteString(string, ((int)EE.pattern[edit_pattern][edit_step] & NOTE_MASK)-1);
      else
        strcpy(string, "---");
      break;

    case 1:
      strcpy_P(fmt_str, tied_str);
      strcpy_P(alt1, yes_str);
      strcpy_P(alt2, no_str);
      sprintf(string, fmt_str, (EE.pattern[edit_pattern][edit_step] & TIE_MASK) ? alt1 : alt2);
      break;

    case 2:
      strcpy_P(fmt_str, accent_str);
      strcpy_P(alt1, on_str);
      strcpy_P(alt2, off_str);
      sprintf(string, fmt_str, (EE.pattern[edit_pattern][edit_step] & ACCENT_MASK) ? alt1 : alt2);
      break;

    case 3:
      strcpy_P(fmt_str, slide_str);
      strcpy_P(alt1, on_str);
      strcpy_P(alt2, off_str);
      sprintf(string, fmt_str, (EE.pattern[edit_pattern][edit_step] & SLIDE_MASK) ? alt1 : alt2);
      break;

    case 4:
      strcpy_P(fmt_str, vcfcv_str);
      sprintf(string, fmt_str, (EE.pattern[edit_pattern][edit_step] & VCF_MASK) >> 9);
      break;

    case 5:
      update_display = 1; 
      break;
      
    default:
      break;
  }
  if (shard < 5)
    display.print(((shard != step_param) || blink) ? string : erase_str); // blink if selected step_param
}
void drawEditNote(void)
{
  int note = EE.pattern[edit_pattern][edit_step] & NOTE_MASK;

  if (enc_step != 0)
  {
    note += enc_step;
    note = constrain(note, 0, 49);
    enc_step = 0;
  }
  EE.pattern[edit_pattern][edit_step] = (EE.pattern[edit_pattern][edit_step] & ~NOTE_MASK) | note;
  drawEditStepParam(0);
  update_display = 1;
}
void drawEditAccent(void)
{
  if (enc_step != 0)
  {
    if (enc_step > 0)
      EE.pattern[edit_pattern][edit_step] |= ACCENT_MASK;
    else 
      EE.pattern[edit_pattern][edit_step] &= ~ACCENT_MASK;
    enc_step = 0;
  }
  drawEditStepParam(2);
  update_display = 1;
}      
void drawEditVcfCV(void)
{
  int vcf = (EE.pattern[edit_pattern][edit_step] & VCF_MASK) >> 9;

  if (enc_step != 0)
  {
    vcf += enc_step*5;
    vcf = constrain(vcf, 0, 125);
    enc_step = 0;
  }
  EE.pattern[edit_pattern][edit_step] = (EE.pattern[edit_pattern][edit_step] & ~VCF_MASK) | (vcf << 9);
  drawEditStepParam(4);
  update_display = 1;  
}
void drawEditSlide(void)
{
  if (enc_step != 0)
  {
    if (enc_step > 0)
      EE.pattern[edit_pattern][edit_step] |= SLIDE_MASK;
    else 
      EE.pattern[edit_pattern][edit_step] &= ~SLIDE_MASK;
    enc_step = 0;
  }
  drawEditStepParam(3);
  update_display = 1;
}
void drawEditTieNext(void)
{
  if (enc_step != 0)
  {
    if (enc_step > 0)
      EE.pattern[edit_pattern][edit_step] |= TIE_MASK;
    else 
      EE.pattern[edit_pattern][edit_step] &= ~TIE_MASK;
    enc_step = 0;
  }
  drawEditStepParam(1);
  update_display = 1;
}

// PATTERN LINK AND REPEAT
void drawEditLinkMenu(void)
{
  display.clearDisplay();
  display.drawRoundRect(2, 2, 76, 62, 3, WHITE); // pattern box
  strcpy_P(fmt_str, chain_str);
  display.setTextSize(2);
  display.setCursor(11, 6);
  display.print(fmt_str);
  display.setTextSize(1);
  strcpy_P(fmt_str, menu_edit);
  sprintf(string, fmt_str, edit_pattern);
  drawModeIndicator(string);
  update_display = 1;
}
void drawEditLinkReptMenu(int val)
{
  char temp_str[12];

  if (val)
  {
    if (enc_step != 0)
    {
      edit_param1 += enc_step;
      edit_param1 = constrain(edit_param1, 0, MAX_REPEATS-1);
      enc_step = 0;
    }
    strcpy_P(temp_str, repeat_str);
    sprintf(string, temp_str, edit_param1+1);
    display.setCursor(11, 40);
  }
  else
  {
    if (enc_step != 0)
    {
      edit_param0 += enc_step;
      edit_param0 = constrain(edit_param0, 0, MAX_PATTERN-1);
      enc_step = 0;
    }
    strcpy_P(temp_str, link_str);
    sprintf(string, temp_str, edit_param0);
    display.setCursor(11, 30); 
  }
  display.setTextSize(1);
  display.print(string);
  update_display = 1;
}

// SYSTEM MENU
void drawSystemMainMenu(void)
{
  display.clearDisplay();
  display.setTextSize(1);
  display.drawRoundRect(2, 2, 76, 62, 3, WHITE); // pattern box
  strcpy_P(fmt_str, menu_sys);
  drawModeIndicator(fmt_str);
  update_display = 1;
}
void drawSystemMain(void)
{
  if (enc_step != 0)
  {
    sys_menu += enc_step;
    sys_menu = constrain(sys_menu, 0, 5);
    enc_step = 0;
  }
  drawSysMenuSelect(sys_menu);
  update_display = 1;
}
void drawSysMenuSelect(int sys_menu)
{
  char temp_str[10];
  
  display.setTextSize(2);
  display.setCursor(18, 8);
  switch (sys_menu)
  {
    case 0:
      strcpy_P(temp_str, new_str);
      display.print(temp_str);
      break;
      
    case 1:
      strcpy_P(temp_str, copy_str);
      display.print(temp_str);
      break;

    case 2:
      strcpy_P(temp_str, save_str);
      display.print(temp_str);
      break;

    case 3:
      strcpy_P(temp_str, load_str);
      display.print(temp_str);
      break;

    case 4:
      strcpy_P(temp_str,  tune_str);
      display.print(temp_str);
      break;

    case 5:
      strcpy_P(temp_str, vref_str);
      display.print(temp_str);
      break;

    default:
      break;      
  }
}
void drawSystemTuning(void)
{
  char notestring[4];

  if (enc_step != 0)
  {
    tuning += enc_step;
    tuning = constrain(tuning, 1, 49);
    enc_step = 0;
  }
  getNoteString(notestring, tuning-1);
  display.setTextSize(2);
  display.setCursor(22, 36);
  display.print(notestring);
  update_display = 1;
}
void drawSysEpromMenu(void)
{
  char temp_str[10];
    
  strcpy_P(temp_str, eeprom_str);
  display.setTextSize(1);
  display.setCursor(18, 36);
  display.print(temp_str);
  update_display = 1;
}
void drawSystemNewPtnMenu(int val)
{
  char temp_str[12];

  if (val)
  {
    if (enc_step != 0)
    {
      edit_step += enc_step;
      edit_step = constrain(edit_step, 1, MAX_STEPS);
      enc_step = 0;
    }
    strcpy_P(temp_str, new_step);
    sprintf(string, temp_str, edit_step);
    display.setCursor(12, 40);
  }
  else
  {
    if (enc_step != 0)
    {
      edit_pattern += enc_step;
      edit_pattern = constrain(edit_pattern, 0, MAX_PATTERN-1);
      enc_step = 0;
    }
    strcpy_P(temp_str, new_ptrn);
    sprintf(string, temp_str, edit_pattern);
    display.setCursor(12, 30); 
  }
  display.setTextSize(1);

  display.print(string);
  update_display = 1;
}
void drawSystemCopyPtnMenu(int val)
{
  char temp_str[12];

  if (val)
  {
    if (enc_step != 0)
    {
      edit_pattern += enc_step;
      if (edit_param1 == edit_pattern) // try copy to itself?
      { 
        if (edit_param1 == MAX_PATTERN-1)
          edit_pattern = MAX_PATTERN-2;
        else if (edit_param1 == 0)
          edit_pattern = 1;
        else
          edit_pattern += enc_step;
      }
      edit_pattern = constrain(edit_pattern, 0, MAX_PATTERN-1);
      enc_step = 0;
    }
    strcpy_P(temp_str, to_str);
    sprintf(string, temp_str, edit_pattern);
    display.setCursor(12, 40); 
  }
  else
  {
    if (enc_step != 0)
    {
      edit_param1 += enc_step;
      edit_param1 = constrain(edit_param1, 0, MAX_PATTERN-1);
      enc_step = 0;
    }
    if (edit_param1 == edit_pattern)
      ++edit_pattern %= 10; // adv dest to next pattern
      
    strcpy_P(temp_str, from_str);
    sprintf(string, temp_str, edit_param1);
    display.setCursor(12, 30);
  }
  display.setTextSize(1);
  display.print(string);
  update_display = 1;
}
void drawSystemSetVRef(void)
{
  char temp_str[12];

  if (enc_step != 0)
  {
    EE.VRef += enc_step * 5;
    EE.VRef = constrain(EE.VRef, 4750, 5250);
    enc_step = 0;
  }
  strcpy_P(temp_str, volt_str);
  sprintf(string, temp_str, EE.VRef/1000, EE.VRef%1000);
  display.setTextSize(1);
  display.setCursor(18, 36);
  display.print(string);
  update_display = 1;
}

//
// HELPER FUNCTIONS
//

// Draw the Mode Indicator window
void drawModeIndicator(char const *str)
{
  display.drawRoundRect(80, 2, 47, 11, 3, WHITE); // menu mode and current pattern
  display.setCursor(83, 4);
  display.print(str);  
}

// Draw the Repeat Indicator window
void drawRepeatIndicator(int from, int to)
{
  if (to > 1)
  {
    display.drawRoundRect(80, 14, 47, 19, 3, WHITE);
    strcpy_P(fmt_str, rep1_str);
    sprintf(string, fmt_str, from);
    display.setCursor(83, 16);
    display.print(string);
    strcpy_P(fmt_str, rep2_str);
    sprintf(string, fmt_str, to);
    display.setCursor(83, 24);
    display.print(string);
  }
  else
   display.fillRect(80, 14, 47, 19, BLACK);
}

// Update outputstring with note expressed as 3 characters
void getNoteString(char *outputstring, int note)
{
  char tmp_str[3];

  strcpy_P(tmp_str, notes[note%12]);
  sprintf(outputstring, "%s%1d", tmp_str, note/12);
}

// Debounce (active low) button, Jack Ganssle style
bool debounceButton(int index, int inputpin) 
{
  static uint16_t state[3] = {0, 0, 0};
  state[index] = (state[index]<<1) | digitalRead(inputpin) | 0xfe00;
  return (state[index] == 0xff00);
}

// Calculate 1V/oct CV based on VREF (+5V rail) and DAC resolution
// Useful info: https://www.allaboutcircuits.com/projects/diy-synth-series-vco/ 
#define CROSTEP   83333L    // cromatic step in uV for 1V/oct
unsigned int noteToCV(unsigned int note)
{
  long tmp = CROSTEP*(long)note;
  
  return (unsigned int)(tmp/(((long)EE.VRef*1000L)/4096L));
}

// Write to MCP4725 DACs on I2C
void DAC(unsigned int data, byte addr) 
{
  Wire.beginTransmission(addr);
  Wire.write((data >> 8) & 0x0F);
  Wire.write(data);
  Wire.endTransmission();
}

// EEPROM
void eepromDataLoad(void)
{
  int i,j;
  EEPROM.get(0, EE);
  if (EE.magic != MAGIC_NBR)
  {
    for (j=0 ; j<MAX_PATTERN ; j++)
    {
      EE.pattlen[j] = (j<<4) + (MAX_STEPS-1) ;
      for (i=0 ; i<MAX_STEPS ; i++)
      {
        EE.pattern[j][i] = 0;
      }
      EE.pattrep[j] = 0;
    }
    EE.VRef = 5000;
    EE.magic = MAGIC_NBR;
  }
}
void eepromDataSave(void)
{
  EEPROM.put(0, EE);
}

// Splash screen that shows software version numbers
void drawSplashScreen(void)
{
  char temp_str[12];
  strcpy_P(temp_str, ver_str);
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(WHITE, BLACK);
  display.setCursor(18, 23);
  sprintf(string, temp_str, VER_MAJOR, VER_MINOR);
  display.print(string);
  display.drawRoundRect(14, 18, 102, 24, 3, WHITE);
  display.display();
}
