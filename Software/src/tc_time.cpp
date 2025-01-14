/*
 * -------------------------------------------------------------------
 * CircuitSetup.us Time Circuits Display
 * (C) 2021-2022 John deGlavina https://circuitsetup.us
 * (C) 2022-2023 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Time-Circuits-Display-A10001986
 *
 * Time and Main Controller
 *
 * -------------------------------------------------------------------
 * License: MIT
 * 
 * Permission is hereby granted, free of charge, to any person 
 * obtaining a copy of this software and associated documentation 
 * files (the "Software"), to deal in the Software without restriction, 
 * including without limitation the rights to use, copy, modify, 
 * merge, publish, distribute, sublicense, and/or sell copies of the 
 * Software, and to permit persons to whom the Software is furnished to 
 * do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF 
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. 
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY 
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, 
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE 
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "tc_global.h"

#include <Arduino.h>
#include <time.h>
#include <WiFi.h>
#include <Wire.h>
#include <Udp.h>
#include <WiFiUdp.h>

#include "tc_keypad.h"
#include "tc_menus.h"
#include "tc_audio.h"
#include "tc_wifi.h"
#include "tc_settings.h"
#ifdef FAKE_POWER_ON
#include "input.h"
#endif

#include "tc_time.h"

// i2c slave addresses

#define DEST_TIME_ADDR 0x71 // TC displays
#define PRES_TIME_ADDR 0x72
#define DEPT_TIME_ADDR 0x74

#define KEYPAD_ADDR    0x20 // PCF8574 port expander (keypad) (redefined in keypad.cpp, here only for completeness)

#define DS3231_ADDR    0x68 // DS3231 RTC
#define PCF2129_ADDR   0x51 // PCF2129 RTC

#define SPEEDO_ADDR    0x70 // speedo display

#define GPS_ADDR       0x10 // GPS receiver

                            // temperature sensors
#define MCP9808_ADDR   0x18 // [default]
#define BMx280_ADDR    0x77 // [default]
#define SHT40_ADDR     0x44 // [default]
#define SI7021_ADDR    0x40 // [default]
#define TMP117_ADDR    0x49 // [non-default]
#define AHT20_ADDR     0x38 // [default]
#define HTU31_ADDR     0x41 // [non-default]

                            // light sensors 
#define LTR3xx_ADDR    0x29 // [default]                            
#define TSL2561_ADDR   0x29 // [default]
#define BH1750_ADDR    0x23 // [default]
#define VEML6030_ADDR  0x48 // [non-default] (can also be 0x10 but then conflicts with GPS)
#define VEML7700_ADDR  0x10 // [default] (conflicts with GPS!)

// The time between reentry sound being started and the display coming on
// Must be sync'd to the sound file used! (startup.mp3/timetravel.mp3)
#ifdef TWSOUND
#define STARTUP_DELAY 900
#else
#define STARTUP_DELAY 1050
#endif
#define TIMETRAVEL_DELAY 1500

// Time between the phases of (long) time travel
// Sum of all must be 8000
#define TT_P1_DELAY_P1  1400                                                                    // Normal
#define TT_P1_DELAY_P2  (4200-TT_P1_DELAY_P1)                                                   // Light flicker
#define TT_P1_DELAY_P3  (5800-(TT_P1_DELAY_P2+TT_P1_DELAY_P1))                                  // Off
#define TT_P1_DELAY_P4  (6800-(TT_P1_DELAY_P3+TT_P1_DELAY_P2+TT_P1_DELAY_P1))                   // Random display I
#define TT_P1_DELAY_P5  (8000-(TT_P1_DELAY_P4+TT_P1_DELAY_P3+TT_P1_DELAY_P2+TT_P1_DELAY_P1))    // Random display II
// ACTUAL POINT OF TIME TRAVEL:
#define TT_P1_POINT88   1000    // ms into "starttravel" sample, when 88mph is reached.
#define TT_SNDLAT        400    // DO NOT CHANGE

// Preprocessor config for External Time Travel Output (ETTO):
// Lead time from trigger (LOW->HIGH) to actual tt (ie when 88mph is reached)
// The external prop has ETTO_LEAD_TIME ms to play its pre-tt sequence. After
// ETTO_LEAD_TIME ms, 88mph is reached, and the actual tt takes place.
#define ETTO_LEAD_TIME      5000
// Trigger mode:
// true:  Pulse for ETTO_PULSE_DURATION ms on tt start minus leadTime
// false: LOW->HIGH on tt start minus leadTime, HIGH->LOW on start of reentry
#define ETTO_USE_PULSE      false
// If ETTO_USE_PULSE is true, pulse for approx. ETTO_PULSE_DURATION ms
#define ETTO_PULSE_DURATION 1000
// End of ETTO config

// Native NTP
#define NTP_PACKET_SIZE 48
#define NTP_DEFAULT_LOCAL_PORT 1337

unsigned long        powerupMillis = 0;
static unsigned long lastMillis = 0;
uint64_t             millisEpoch = 0;

static bool    couldHaveAuthTime = false;
static bool    haveAuthTime = false;
uint16_t       lastYear = 0;
static uint8_t resyncInt = 5;
static uint8_t dstChkInt = 5;
bool           syncTrigger = false;
bool           doAPretry = true;

// For tracking second changes
static bool x = false;  
static bool y = false;

// The startup sequence
bool                 startup      = false;
static bool          startupSound = false;
static unsigned long startupNow   = 0;

// For beep-auto-modes
uint8_t       beepMode = 0;
bool          beepTimer = false;
unsigned long beepTimeout = 30;
unsigned long beepTimerNow = 0;

// Pause auto-time-cycling if user played with time travel
static bool          autoPaused = false;
static unsigned long pauseNow = 0;
static unsigned long pauseDelay = 30*60*1000;  // Pause for 30 minutes

// "Room condition" mode
static bool          rcMode = false;
bool                 haveRcMode = false;
#define TEMP_UPD_INT_L (2*60*1000)
#define TEMP_UPD_INT_S (30*1000)

// The timetravel sequence: Timers, triggers, state
static bool          playTTsounds = true;
int                  timeTravelP0 = 0;
int                  timeTravelP2 = 0;

#ifdef TC_HAVESPEEDO
bool                 useSpeedo = false;
static unsigned long timetravelP0Now = 0;
static unsigned long timetravelP0Delay = 0;
static unsigned long ttP0Now = 0;
static uint8_t       timeTravelP0Speed = 0;
static long          pointOfP1 = 0;
static bool          didTriggerP1 = false;
static float         ttP0TimeFactor = 1.0;
#ifdef TC_HAVEGPS
static unsigned long dispGPSnow = 0;
#endif
#ifdef SP_ALWAYS_ON
static unsigned long dispIdlenow = 0;
#endif
#ifdef TC_HAVETEMP
static int           tempBrightness = DEF_TEMP_BRIGHT;
#endif
#endif // HAVESPEEDO
bool                 useTemp = false;
bool                 dispTemp = true;
bool                 tempOffNM = true;
#ifdef TC_HAVETEMP
bool                 tempUnit = DEF_TEMP_UNIT;
static unsigned long tempReadNow = 0;
static unsigned long tempDispNow = 0;
static unsigned long tempUpdInt = TEMP_UPD_INT_L;
#endif
static unsigned long timetravelP1Now = 0;
static unsigned long timetravelP1Delay = 0;
int                  timeTravelP1 = 0;
static bool          triggerP1 = false;
static unsigned long triggerP1Now = 0;
static long          triggerP1LeadTime = 0;
#ifdef EXTERNAL_TIMETRAVEL_OUT
static bool          useETTO = DEF_USE_ETTO;
static bool          useETTOWired = DEF_USE_ETTO;
static bool          ettoUsePulse = ETTO_USE_PULSE;
static long          ettoLeadTime = ETTO_LEAD_TIME;
static bool          triggerETTO = false;
static long          triggerETTOLeadTime = 0;
static unsigned long triggerETTONow = 0;
static bool          ettoPulse = false;
static unsigned long ettoPulseNow = 0;
static long          ettoLeadPoint = 0;
#endif

// The timetravel re-entry sequence
static unsigned long timetravelNow = 0;
bool                 timeTravelRE = false;

int  specDisp = 0;

uint8_t  mqttDisp = 0;
#ifdef TC_HAVEMQTT
uint8_t  mqttOldDisp = 0;
char     mqttMsg[256];
uint16_t mqttIdx = 0;
int16_t  mqttMaxIdx = 0;
bool     mqttST = false;
static unsigned long mqttStartNow = 0;
#endif

// CPU power management
static bool          pwrLow = false;
static unsigned long pwrFullNow = 0;

// For displaying times off the real time
uint64_t timeDifference = 0;
bool     timeDiffUp = false;  // true = add difference, false = subtract difference

// Persistent time travels:
// This controls the firmware's behavior as regards saving times to NVM.
// If this is true, times are saved to NVM, whenever
//  - the user enters a destination time for time travel and presses ENTER
//  - the user activates time travel (hold "0")
//  - the user returns from a time travel (hold "9")
// True means that playing around with time travel is persistent, and even
// present time is kept over a power loss (if the battery backed RTC keeps
// the time). Downside is that the user's custom destination and last
// departure times are overwritten during a time travel.
// False means that time travel games are non-persistent, and the user's
// custom times (as set up in the keypad menu) are never overwritten.
// Also, "false" reduces flash wear considerably.
bool timetravelPersistent = true;

// Alarm/HourlySound based on RTC (or presentTime's display)
static bool alarmRTC = true;

bool useGPS      = true;
bool useGPSSpeed = false;

// TZ/DST status & data
static bool checkDST        = false;
bool        couldDST[3]     = { false, false, false };   // Could use own DST management (and DST is defined in TZ)
static int  tzForYear[3]    = { 0, 0, 0 };               // Parsing done for this very year
static int8_t tzIsValid[3]  = { -1, -1, -1 };
static int8_t tzHasDST[3]   = { -1, -1, -1 };
static char *tzDSTpart[3]   = { NULL, NULL, NULL };
static int  tzDiffGMT[3]    = { 0, 0, 0 };               // Difference to UTC in nonDST time
static int  tzDiffGMTDST[3] = { 0, 0, 0 };               // Difference to UTC in DST time
static int  tzDiff[3]       = { 0, 0, 0 };               // difference between DST and non-DST in minutes
static int  DSTonMins[3]    = { -1, -1, -1 };            // DST-on date/time in minutes since 1/1 00:00 (in non-DST time)
static int  DSToffMins[3]   = { 600000, 600000, 600000}; // DST-off date/time in minutes since 1/1 00:00 (in DST time)

// WC stuff
bool        WcHaveTZ1  = false;
bool        WcHaveTZ2  = false;
bool        haveWcMode = false;
static bool wcMode     = false;
static int  wcLastMin  = -1;
bool        triggerWC  = false;
bool        haveTZName1 = false;
bool        haveTZName2 = false;
uint8_t     destShowAlt = 0, depShowAlt = 0;

// For NTP/GPS
static struct        tm _timeinfo;
static WiFiUDP       ntpUDP;
static UDP*          myUDP;
static byte          NTPUDPBuf[NTP_PACKET_SIZE];
static unsigned long NTPUpdateNow = 0;
static unsigned long NTPTSAge = 0;
static unsigned long NTPTSRQAge = 0;
static uint32_t      NTPsecsSinceTCepoch = 0;
static uint32_t      NTPmsSinceSecond = 0;
static bool          NTPPacketDue = false;
static bool          NTPWiFiUp = false;
static uint8_t       NTPfailCount = 0;
static uint8_t       NTPUDPID[4] = { 0, 0, 0, 0};
 
// The RTC object
tcRTC rtc(2, (uint8_t[2*2]){ PCF2129_ADDR, RTCT_PCF2129, 
                             DS3231_ADDR,  RTCT_DS3231 });
static unsigned long OTPRDoneNow = 0;
static bool          RTCNeedsOTPR = false;
static bool          OTPRinProgress = false;
static unsigned long OTPRStarted = 0;

// The GPS object
#ifdef TC_HAVEGPS
tcGPS myGPS(GPS_ADDR);
static unsigned long lastLoopGPS = 0;
static unsigned long GPSupdateFreq    = 1000;
static unsigned long GPSupdateFreqMin = 2000;
#endif

// The TC display objects
clockDisplay destinationTime(DISP_DEST, DEST_TIME_ADDR);
clockDisplay presentTime(DISP_PRES, PRES_TIME_ADDR);
clockDisplay departedTime(DISP_LAST, DEPT_TIME_ADDR);

// The speedo and temp.sensor objects
#ifdef TC_HAVESPEEDO
speedDisplay speedo(SPEEDO_ADDR);
#endif
#ifdef TC_HAVETEMP
tempSensor tempSens(7, 
            (uint8_t[7*2]){ MCP9808_ADDR, MCP9808,
                            BMx280_ADDR,  BMx280,
                            SHT40_ADDR,   SHT40,
                            SI7021_ADDR,  SI7021,
                            TMP117_ADDR,  TMP117,
                            AHT20_ADDR,   AHT20,
                            HTU31_ADDR,   HTU31
                          });
static bool tempOldNM = false;
#endif
#ifdef TC_HAVELIGHT
lightSensor lightSens(5, 
            (uint8_t[5*2]){ LTR3xx_ADDR,  LST_LTR3xx,   // must be before TSL2651
                            TSL2561_ADDR, LST_TSL2561,  // must be after LTR3xx
                            BH1750_ADDR,  LST_BH1750,
                            VEML6030_ADDR,LST_VEML7700,
                            VEML7700_ADDR,LST_VEML7700  // must be last
                          });
#endif

// Automatic times ("decorative mode")

static int8_t minNext  = 0;
int8_t        autoTime = 0;  // Selects date/time from array below

#ifndef TWPRIVATE //  ----------------- OFFICIAL

const dateStruct destinationTimes[NUM_AUTOTIMES] = {
    {1985, 10, 26,  1, 21},   // Einstein 1:20 -> 1:21
    {1955, 11,  5,  6,  0},   // Marty -> 1955
    {1985, 10, 26,  1, 24},   // Marty -> 1985
    {2015, 10, 21, 16, 29},   // Marty, Doc, Jennifer -> 2015
    {1955, 11, 12,  8,  0},   // Old Biff -> 1955 (time unknown)
    {2015, 10, 21, 19, 25},   // Old Biff -> 2015 (time assumed from run of scene)
    {1985, 10, 26, 21,  0},   // Marty, Doc, Jennifer -> 1985
    {1955, 11, 12,  6,  0},   // Marty, Doc -> 1955
    {1885,  1,  1,  0,  0},   // Doc in thunderstorm -> 1885 (date/time assumed from letter)
    {1885,  9,  2,  8,  0},   // Marty -> 1885
    {1985, 10, 27, 11,  0}    // Marty -> 1985
};
const dateStruct departedTimes[NUM_AUTOTIMES] = {
    {1985, 10, 26,  1, 20},   // Einstein 1:20 -> 1:21
    {1985, 10, 26,  1, 29},   // Marty -> 1955 (time assumed)
    {1955, 11, 12, 22,  4},   // Marty -> 1985
    {1985, 10, 26, 11, 35},   // Marty, Doc, Jennifer -> 2015 (time random)
    {2015, 10, 21, 19, 15},   // Old Biff -> 1955  (time assumed from run of scene)
    {1955, 11, 12, 18, 38},   // Old Biff -> 2015
    {2015, 10, 21, 19, 28},   // Marty, Doc, Jennifer -> 1985
    {1985, 10, 27,  2, 42},   // Marty, Doc -> 1955
    {1955, 11, 12, 21, 33},   // Doc in thunderstorm -> 1885 (time assumed from following events)
    {1955, 11, 15, 11, 11},   // Marty -> 1885 (Date shown in movie 11/5 - wrong! Must be after 11/12! Date&time guessed)
    {1885,  9,  7,  8, 22}    // Marty -> 1985 (time guessed/assumed)
};

#else //  --------------------------- TWPRIVATE

#include "z_twdates.h"

#endif  // ------------------------------------

// Alarm weekday masks
static const uint8_t alarmWDmasks[10] = { 
    0b01111111, 0b00111110, 0b01000001, 0b00000010, 0b00000100,
    0b00001000, 0b00010000, 0b00100000, 0b01000000, 0b00000001
};
                
// Night mode
bool           forceReEvalANM = false;
static bool    autoNightMode = false;
static uint8_t autoNightModeMode = 0;   // 0 = daily hours, 1-4 presets
static uint8_t autoNMOnHour  = 0;
static uint8_t autoNMOffHour = 0;
static int8_t  timedNightMode = -1;
static int8_t  sensorNightMode = -1;
int8_t         manualNightMode = -1;
unsigned long  manualNMNow = 0;
int32_t        luxLimit = 3;
static const uint32_t autoNMhomePreset[7] = {     // Mo-Th 5pm-11pm, Fr 1pm-1am, Sa 9am-1am, Su 9am-11pm
        0b011111111000000000000001,   //Sun
        0b111111111111111110000001,   //Mon
        0b111111111111111110000001,   //Tue
        0b111111111111111110000001,   //Wed
        0b111111111111111110000001,   //Thu
        0b111111111111100000000000,   //Fri
        0b011111111000000000000000    //Sat
};
static const uint32_t autoNMofficePreset[7] = {   // Mo-Fr 9am-5pm
        0b111111111111111111111111,   //Sun
        0b111111111000000001111111,   //Mon
        0b111111111000000001111111,   //Tue
        0b111111111000000001111111,   //Wed
        0b111111111000000001111111,   //Thu
        0b111111111000000001111111,   //Fri
        0b111111111111111111111111    //Sat
};
static const uint32_t autoNMoffice2Preset[7] = {  // Mo-Th 7am-5pm, Fr 7am-2pm
        0b111111111111111111111111,   //Sun
        0b111111100000000001111111,   //Mon
        0b111111100000000001111111,   //Tue
        0b111111100000000001111111,   //Wed
        0b111111100000000001111111,   //Thu
        0b111111100000001111111111,   //Fri
        0b111111111111111111111111    //Sat
};
static const uint32_t autoNMshopPreset[7] = {     // Mo-We 8am-8pm, Th-Fr 8am-9pm, Sa 8am-5pm
        0b111111111111111111111111,   //Sun
        0b111111110000000000001111,   //Mon
        0b111111110000000000001111,   //Tue
        0b111111110000000000001111,   //Wed
        0b111111110000000000000111,   //Thu
        0b111111110000000000000111,   //Fri
        0b111111110000000001111111    //Sat
};
static uint32_t autoNMdailyPreset = 0;
const uint32_t *autoNMPresets[AUTONM_NUM_PRESETS] = {
    autoNMhomePreset, autoNMofficePreset, 
    autoNMoffice2Preset, autoNMshopPreset
};
bool useLight = false;
#ifdef TC_HAVELIGHT
static unsigned long lastLoopLight = 0;
#endif

unsigned long ctDown = 0;
unsigned long ctDownNow = 0;

// State flags & co
static bool DSTcheckDone = false;
static bool autoIntDone = false;
static int  autoIntAnimRunning = 0;
static bool autoReadjust = false;
static unsigned long lastAuthTime = 0;
uint64_t    lastAuthTime64 = 0;
static bool authTimeExpired = false;
static bool alarmDone = false;
static bool remDone = false;
static bool hourlySoundDone = false;
static bool autoNMDone = false;

// Fake "power" switch
bool FPBUnitIsOn = true;
#ifdef FAKE_POWER_ON
static TCButton fakePowerOnKey = TCButton(FAKE_POWER_BUTTON_PIN,
    true,    // Button is active LOW
    true     // Enable internal pull-up resistor
);
static bool isFPBKeyChange = false;
static bool isFPBKeyPressed = false;
bool        waitForFakePowerButton = false;
#endif

static const uint8_t monthDays[] =
{
    31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};
static const unsigned int mon_yday[2][13] =
{
    { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
    { 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};
static const unsigned int mon_ydayt24t60[2][13] =
{
    { 0, 31*24*60,  59*24*60,  90*24*60, 120*24*60, 151*24*60, 181*24*60, 
        212*24*60, 243*24*60, 273*24*60, 304*24*60, 334*24*60, 365*24*60 },
    { 0, 31*24*60,  60*24*60,  91*24*60, 121*24*60, 152*24*60, 182*24*60, 
        213*24*60, 244*24*60, 274*24*60, 305*24*60, 335*24*60, 366*24*60 }
};
static const uint64_t mins1kYears[] =
{
             0,  262975680,  525949920,  788924160, 1051898400,
    1314874080, 1577848320, 1840822560, 2103796800, 2366772480,
    2629746720, 2892720960, 3155695200, 3418670880, 3681645120,
    3944619360, 4207593600, 4470569280, 4733543520, 4996517760
};
static const uint32_t hours1kYears[] =
{
                0,  262975680/60,  525949920/60,  788924160/60, 1051898400/60,
    1314874080/60, 1577848320/60, 1840822560/60, 2103796800/60, 2366772480/60, 
    2629746720/60, 2892720960/60, 3155695200/60, 3418670880/60, 3681645120/60, 
    3944619360/60, 4207593600/60, 4470569280/60, 4733543520/60, 4996517760/60  
};

#ifdef TC_HAVESPEEDO
static const int16_t tt_p0_delays[88] =
{
      0, 100, 100,  90,  80,  80,  80,  80,  80,  80,  // 0 - 9  10mph 0.8s    0.8=800ms
     80,  80,  80,  80,  80,  80,  80,  80,  80,  80,  // 10-19  20mph 1.6s    0.8
     90, 100, 110, 110, 110, 110, 110, 110, 120, 120,  // 20-29  30mph 2.7s    1.1
    120, 130, 130, 130, 130, 130, 130, 130, 130, 140,  // 30-39  40mph 4.0s    1.3
    150, 160, 190, 190, 190, 190, 190, 190, 210, 230,  // 40-49  50mph 5.9s    1.9
    230, 230, 240, 240, 240, 240, 240, 240, 250, 250,  // 50-59  60mph 8.3s    2.4
    250, 250, 260, 260, 270, 270, 270, 280, 290, 300,  // 60-69  70mph 11.0s   2.7
    320, 330, 350, 370, 370, 380, 380, 390, 400, 410,  // 70-79  80mph 14.7s   3.7
    410, 410, 410, 410, 410, 410, 410, 410             // 80-87  90mph 18.8s   4,1
};
static long tt_p0_totDelays[88];
#endif

static void myCustomDelay(unsigned int mydel);
static void myIntroDelay(unsigned int mydel, bool withGPS = true);
static void waitAudioDoneIntro();

static bool getNTPOrGPSTime(bool weHaveAuthTime, DateTime& dt);
static bool getNTPTime(bool weHaveAuthTime, DateTime& dt);
#ifdef TC_HAVEGPS
static bool getGPStime(DateTime& dt);
static bool setGPStime();
bool        gpsHaveFix();
static bool gpsHaveTime();
static void dispGPSSpeed(bool force = false);
#endif

#ifdef TC_HAVETEMP
static void updateTemperature(bool force = false);
#ifdef TC_HAVESPEEDO
static bool dispTemperature(bool force = false);
#endif
#endif
#ifdef TC_HAVESPEEDO
#ifdef SP_ALWAYS_ON
static void dispIdleZero(bool force = false);
#endif
#endif

static void triggerLongTT();

#ifdef EXTERNAL_TIMETRAVEL_OUT
static void ettoPulseStart();
static void ettoPulseEnd();
#endif

// Time calculations
static bool blockDSTChange(int currTimeMins);
static void localToDST(int index, int& year, int& month, int& day, int& hour, int& minute, int& isDST);
static void updateDSTFlag(int nisDST = -1);

/// Native NTP
static void ntp_setup();
static bool NTPHaveTime();
static bool NTPTriggerUpdate();
static void NTPSendPacket();
static void NTPCheckPacket();
static uint32_t NTPGetSecsSinceTCepoch();
static bool NTPGetLocalTime(int& year, int& month, int& day, int& hour, int& minute, int& second, int& isDST);
static bool NTPHaveLocalTime();


/*
 * time_boot()
 *
 */
void time_boot()
{
    // Start the displays early to clear them
    presentTime.begin();
    destinationTime.begin();
    departedTime.begin();

    // Switch LEDs on
    // give user some feedback that the unit is powered
    pinMode(LEDS_PIN, OUTPUT);
    digitalWrite(LEDS_PIN, HIGH);
    // Do not call leds_on(), is not unconditional
}

/*
 * time_setup()
 *
 */
void time_setup()
{
    DateTime dt;
    bool rtcbad = false;
    bool tzbad = false;
    bool haveGPS = false;
    #ifdef TC_HAVEGPS
    bool haveAuthTimeGPS = false;
    #endif

    Serial.println(F("Time Circuits Display version " TC_VERSION " " TC_VERSION_EXTRA));

    // Power management: Set CPU speed
    // to maximum and start timer
    #ifdef TC_DBG
    Serial.print("Initial CPU speed is ");
    Serial.println(getCpuFrequencyMhz(), DEC);
    #endif
    pwrNeedFullNow(true);

    // Pin for monitoring seconds from RTC
    pinMode(SECONDS_IN_PIN, INPUT_PULLDOWN);

    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);

    // Init fake power switch
    #ifdef FAKE_POWER_ON
    waitForFakePowerButton = ((int)atoi(settings.fakePwrOn) > 0);
    if(waitForFakePowerButton) {
        fakePowerOnKey.setPressTicks(10);     // ms after short press is assumed (default 400)
        fakePowerOnKey.setLongPressTicks(50); // ms after long press is assumed (default 800)
        fakePowerOnKey.setDebounceTicks(50);
        fakePowerOnKey.attachLongPressStart(fpbKeyPressed);
        fakePowerOnKey.attachLongPressStop(fpbKeyLongPressStop);
    }
    #endif

    // Init external time travel output ("etto")
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    pinMode(EXTERNAL_TIMETRAVEL_OUT_PIN, OUTPUT);
    ettoPulseEnd();
    #endif

    // RTC setup
    if(!rtc.begin(powerupMillis)) {

        Serial.println(F("time_setup: Couldn't find RTC. Panic!"));

        // Blink white LED forever
        pinMode(WHITE_LED_PIN, OUTPUT);
        while(1) {
            digitalWrite(WHITE_LED_PIN, HIGH);
            delay(1000);
            digitalWrite(WHITE_LED_PIN, LOW);
            delay(1000);
        }
    }

    RTCNeedsOTPR = rtc.NeedOTPRefresh();

    if(rtc.lostPower()) {

        // Lost power and battery didn't keep time, so set current time to 
        // default time 1/1/2023, 0:0
        rtc.adjust(0, 0, 0, dayOfWeek(1, 1, 2023), 1, 1, 23);
       
        #ifdef TC_DBG
        Serial.println(F("time_setup: RTC lost power, setting default time."));
        #endif

        rtcbad = true;
    }

    if(RTCNeedsOTPR) {
        rtc.OTPRefresh(true);
        delay(100);
        rtc.OTPRefresh(false);
        delay(100);
        OTPRDoneNow = millis();
    }

    // Turn on the RTC's 1Hz clock output
    rtc.clockOutEnable();  

    // Start the displays
    presentTime.begin();
    destinationTime.begin();
    departedTime.begin();

    // Initialize clock mode: 12 hour vs 24 hour
    bool mymode24 = ((int)atoi(settings.mode24) > 0);
    presentTime.set1224(mymode24);
    destinationTime.set1224(mymode24);
    departedTime.set1224(mymode24);

    // Configure presentTime as a display that will hold real time
    presentTime.setRTC(true);

    // Configure behavior in night mode
    destinationTime.setNMOff(((int)atoi(settings.dtNmOff) > 0));
    presentTime.setNMOff(((int)atoi(settings.ptNmOff) > 0));
    departedTime.setNMOff(((int)atoi(settings.ltNmOff) > 0));

    // Determine if user wanted Time Travels to be persistent
    timetravelPersistent = ((int)atoi(settings.timesPers) > 0);

    // Load present time settings (yearOffs, timeDifference, isDST)
    presentTime.load((int)atoi(settings.presTimeBright));

    if(!timetravelPersistent) {
        timeDifference = 0;
    }

    if(rtcbad) {
        presentTime.setYearOffset(0);
        presentTime.setDST(0);
    }

    // See if speedo display is to be used
    #ifdef TC_HAVESPEEDO
    {
        int temp = (int)atoi(settings.speedoType);
        if(temp >= SP_NUM_TYPES) temp = 99;
        useSpeedo = (temp != 99);
    }
    #endif

    // Set up GPS receiver
    #ifdef TC_HAVEGPS
    useGPS = true;    // Use by default if detected
    
    #ifdef TC_HAVESPEEDO
    if(useSpeedo) {
        useGPSSpeed = ((int)atoi(settings.useGPSSpeed) > 0);
    }
    #endif

    // Check for GPS receiver
    // Do so regardless of usage in order to eliminate
    // VEML7700 light sensor with identical i2c address
    if(myGPS.begin(powerupMillis, useGPSSpeed)) {

        myGPS.setCustomDelayFunc(myCustomDelay);
        haveGPS = true;
        
        if(useGPS || useGPSSpeed) {
          
            // Clear so we don't add to stampAge unnecessarily in
            // boot strap
            GPSupdateFreq = GPSupdateFreqMin = 0;
            
            // We know now we have a possible source for auth time
            couldHaveAuthTime = true;
            
            // Fetch data already in Receiver's buffer [120ms]
            for(int i = 0; i < 10; i++) myGPS.loop(true);
            
            #ifdef TC_DBG
            Serial.println(F("time_setup: GPS Receiver found and initialized"));
            #endif

        } 

    } else {
      
        useGPS = useGPSSpeed = false;
        
        #ifdef TC_DBG
        Serial.println(F("time_setup: GPS Receiver not found"));
        #endif
        
    }
    #endif

    // If a WiFi network and an NTP server are configured, we might
    // have a source for auth time.
    // (Do not involve WiFi connection status here; might change later)
    if((settings.ntpServer[0] != 0) && wifiHaveSTAConf)
        couldHaveAuthTime = true;
    
    // Try to obtain initial authoritative time

    ntp_setup();
    if((settings.ntpServer[0] != 0) && (WiFi.status() == WL_CONNECTED)) {
        int timeout = 50;
        do {
            ntp_loop();
            delay(100);
            timeout--;
        } while (!NTPHaveTime() && timeout);
    }

    // Parse TZ to check validity.
    // (Year does not matter at this point)
    if(!(parseTZ(0, 2022))) {
        tzbad = true;
        #ifdef TC_DBG
        Serial.println(F("time_setup: Failed to parse TZ"));
        #endif
    }
    
    // Set RTC with NTP time
    if(getNTPTime(true, dt)) {

        // So we have authoritative time now
        haveAuthTime = true;
        lastAuthTime = millis();
        lastAuthTime64 = lastAuthTime;
        
        #ifdef TC_DBG
        Serial.println(F("time_setup: RTC set through NTP"));        
        #endif
        
    } else {
      
        // GPS might have a fix, so try fetching time from GPS
        #ifdef TC_HAVEGPS
        if(useGPS) {

            // Pull old data from buffer
            for(int i = 0; i < 10; i++) myGPS.loop(true);

            for(int i = 0; i < 10; i++) {
                for(int j = 0; j < 4; j++) myGPS.loop(true);
                #ifdef TC_DBG
                if(i == 0) Serial.println(F("time_setup: First attempt to read time from GPS"));
                #endif
                if(getGPStime(dt)) {
                    // So we have authoritative time now
                    haveAuthTime = true;
                    lastAuthTime = millis();
                    lastAuthTime64 = lastAuthTime;
                    haveAuthTimeGPS = true;
                    #ifdef TC_DBG
                    Serial.println(F("time_setup: RTC set through GPS"));
                    #endif
                    break;
                }
                yield();
                delay(100);
            }
            // If we haven't managed to get time here, try
            // a sync in shorter intervals in time_loop.
            if(!haveAuthTimeGPS) resyncInt = 2;
        }
        #endif
    }

    if(haveAuthTime) {
        // Save YearOffs & isDST to NVM if change is detected
        if( (presentTime.getYearOffset() != presentTime.loadYOffs()) ||
            (presentTime.getDST()        != presentTime.loadDST()) ) {
            presentTime.save();
        }
    }
    
    // Reset (persistent) time travel if RTC is bad and no NTP/GPS time
    if(rtcbad && !haveAuthTime) {
        timeDifference = 0;
    }

    // Start the Config Portal. Now is a good time, we had
    // our NTP access, so a WiFiScan does not disturb anything
    // at this point.
    if(WiFi.status() == WL_CONNECTED) {
        wifiStartCP();
    }

    // Load the time for initial display
    myrtcnow(dt);

    // rtcYear: Current year as read from the RTC
    uint16_t rtcYear = dt.year() - presentTime.getYearOffset();

    // lastYear: The year when the RTC was adjusted for the last time.
    // If the RTC was just updated, everything is in place, lastYear
    // will be saved in the time_loop if changed.
    // Otherwise load from NVM, and make required re-calculations.
    if(haveAuthTime) {
        lastYear = rtcYear;
    } else {
        lastYear = presentTime.loadLastYear();
    }

    // Parse TZ and set TZ/DST parameters for current year
    if(!(parseTZ(0, rtcYear))) {
        tzbad = true;
    }

    // Parse alternate time zones for WC
    if(settings.timeZoneDest[0] != 0) {
        if(parseTZ(1, rtcYear)) {
            WcHaveTZ1 = true;
        }
    }
    if(settings.timeZoneDep[0] != 0) {
        if(parseTZ(2, rtcYear)) {
            WcHaveTZ2 = true;
        }
    }
    if((haveWcMode = (WcHaveTZ1 || WcHaveTZ2))) {
        if(WcHaveTZ1 && settings.timeZoneNDest[0] != 0) {
            destinationTime.setAltText(settings.timeZoneNDest);
            haveTZName1 = true;
        }
        if(WcHaveTZ2 && settings.timeZoneNDep[0] != 0) {
            departedTime.setAltText(settings.timeZoneNDep);
            haveTZName2 = true;
        }
    }
    #ifdef TC_DBG
    Serial.printf("time_setup: haveWcMode %d\n", haveWcMode);
    #endif
 
    // If we are switched on after a year switch, re-do
    // RTC year-translation.
    if(lastYear != rtcYear) {

        // Get RTC-fit year & offs for real year
        int16_t yOffs = 0;
        uint16_t newYear = rtcYear;
        correctYr4RTC(newYear, yOffs);

        // If year-translation changed, update RTC and save
        if((newYear != dt.year()) || (yOffs != presentTime.getYearOffset())) {

            // Update RTC
            myrtcnow(dt);
            rtc.adjust(dt.second(), 
                       dt.minute(), 
                       dt.hour(), 
                       dayOfWeek(dt.day(), dt.month(), rtcYear),
                       dt.day(), 
                       dt.month(), 
                       newYear-2000
            );

            presentTime.setYearOffset(yOffs);

            dt.setYear(newYear-2000);

            // Save YearOffs to NVM if change is detected
            if(presentTime.getYearOffset() != presentTime.loadYOffs()) {
                if(timetravelPersistent) {
                    presentTime.save();
                } else {
                    presentTime.saveYOffs();
                }
            }

        }

        presentTime.saveLastYear(rtcYear);
        lastYear = rtcYear;
    }

    // Load to display
    presentTime.setDateTimeDiff(dt);

    // If we don't have authTime, DST status (time & flag) might be 
    // wrong at this point if we're switched on after a long time. 
    // We leave DST determination to time_loop; just cut short the 
    // check interval here.
    if(!haveAuthTime) dstChkInt = 1;

    // Set GPS RTC
    // It does not matter, if DST status (time & flag) is wrong here,
    // as long as time matches DST flag setting (which it does even
    // if we have no authTime).
    #ifdef TC_HAVEGPS
    if(useGPS || useGPSSpeed) {
        if(!haveAuthTimeGPS && (haveAuthTime || !rtcbad)) {
            setGPStime();
        }
        
        // Set ms between GPS polls
        // Need more updates if speed is to be displayed
        // RMC is around 85 chars, so three fit in the 255 byte buffer. 
        // With speed, the buffer fills in 3 seconds; without in 15 seconds;
        // Then there is ZDA every 5 seconds; -> 2.4 / 12.5 seconds.
        // The update freq of 250ms means the entire buffer is read 
        // - once per second with speed,
        // - every 2 seconds without speed. 
        // Good enough.
        GPSupdateFreq    = useGPSSpeed ? 250 : 500;
        GPSupdateFreqMin = useGPSSpeed ? 500 : 500;
    }
    #endif

    // Load destination time (and set to default if invalid)
    if(!destinationTime.load((int)atoi(settings.destTimeBright))) {
        destinationTime.setYearOffset(0);
        destinationTime.setFromStruct(&destinationTimes[0]);
        destinationTime.setBrightness((int)atoi(settings.destTimeBright));
        destinationTime.save();
    }

    // Load departed time (and set to default if invalid)
    if(!departedTime.load((int)atoi(settings.lastTimeBright))) {
        departedTime.setYearOffset(0);
        departedTime.setFromStruct(&departedTimes[0]);
        departedTime.setBrightness((int)atoi(settings.lastTimeBright));
        departedTime.save();
    }

    // Load autoInterval ("time cycling interval") from settings
    loadAutoInterval();

    // Load alarm from alarmconfig file
    // Don't care if data invalid, alarm is off in that case
    loadAlarm();

    // Load yearly/monthly reminder settings
    loadReminder();

    // Auto-NightMode
    autoNightModeMode = (int)atoi(settings.autoNMPreset);
    if(autoNightModeMode > AUTONM_NUM_PRESETS) autoNightModeMode = 10;
    autoNightMode = (autoNightModeMode != 10);
    autoNMOnHour = (int)atoi(settings.autoNMOn);
    if(autoNMOnHour > 23) autoNMOnHour = 0;
    autoNMOffHour = (int)atoi(settings.autoNMOff);
    if(autoNMOffHour > 23) autoNMOffHour = 0;
    if(autoNightMode && (autoNightModeMode == 0)) {
        if((autoNightMode = (autoNMOnHour != autoNMOffHour))) {
            if(autoNMOnHour < autoNMOffHour) {
                for(int i = autoNMOnHour; i < autoNMOffHour; i++)
                    autoNMdailyPreset |= (1 << (23-i));
            } else {
                autoNMdailyPreset = 0b111111111111111111111111;
                for(int i = autoNMOffHour; i < autoNMOnHour; i++)
                    autoNMdailyPreset &= ~(1 << (23-i));
            }
        }
    }
    if(autoNightMode) forceReEvalANM = true;

    // If using auto times, put up the first one
    if(autoTimeIntervals[autoInterval]) {
        destinationTime.setFromStruct(&destinationTimes[0]);
        departedTime.setFromStruct(&departedTimes[0]);
    }

    // Set up alarm base: RTC or current "present time"
    alarmRTC = ((int)atoi(settings.alarmRTC) > 0);

    // Set up option to play/mute time travel sounds
    playTTsounds = ((int)atoi(settings.playTTsnds) > 0);

    // Set power-up setting for beep
    muteBeep = true;
    beepMode = (uint8_t)atoi(settings.beep);
    if(beepMode >= 3) {
        beepMode = 3;
        beepTimeout = BEEPM3_SECS*1000;
    } else if(beepMode == 2) 
        beepTimeout = BEEPM2_SECS*1000;

    // Set up speedo display
    #ifdef TC_HAVESPEEDO
    if(useSpeedo) {
        speedo.begin((int)atoi(settings.speedoType));

        speedo.setBrightness((int)atoi(settings.speedoBright), true);
        speedo.setDot(true);

        // Speed factor for acceleration curve
        ttP0TimeFactor = (float)atof(settings.speedoFact);
        if(ttP0TimeFactor < 0.5) ttP0TimeFactor = 0.5;
        if(ttP0TimeFactor > 5.0) ttP0TimeFactor = 5.0;

        // Calculate start point of P1 sequence
        for(int i = 1; i < 88; i++) {
            pointOfP1 += (unsigned long)(((float)(tt_p0_delays[i])) / ttP0TimeFactor);
        }
        #ifdef EXTERNAL_TIMETRAVEL_OUT
        ettoLeadPoint = pointOfP1 - ettoLeadTime;   // Can be negative!
        #endif
        pointOfP1 -= TT_P1_POINT88;
        pointOfP1 -= TT_SNDLAT;     // Correction for sound/mp3 latency

        {
            // Calculate total elapsed delay for each mph value
            // (in order to time P0/P1 relative to current actual speed)
            long totDelay = 0;
            for(int i = 0; i < 88; i++) {
                totDelay += (long)(((float)(tt_p0_delays[i])) / ttP0TimeFactor);
                tt_p0_totDelays[i] = totDelay;
            }
        }

        speedo.off();

        #ifdef SP_ALWAYS_ON
        #ifdef FAKE_POWER_ON
        if(!waitForFakePowerButton) {
        #endif
            speedo.setSpeed(0);
            speedo.on();
            speedo.show();
        #ifdef FAKE_POWER_ON
        }
        #endif
        #endif

        #ifdef TC_HAVEGPS
        if(useGPSSpeed) {
            // display (actual) speed, regardless of fake power
            dispGPSSpeed(true);
            speedo.on(); 
        }
        #endif
    }
    #endif

    // Set up temperature sensor
    #ifdef TC_HAVETEMP
    useTemp = true;   // Used by default if detected
    #ifdef TC_HAVESPEEDO
    dispTemp = ((int)atoi(settings.dispTemp) > 0);
    #else
    dispTemp = false;
    #endif
    if(tempSens.begin(powerupMillis)) {
        tempSens.setCustomDelayFunc(myCustomDelay);
        tempUnit = ((int)atoi(settings.tempUnit) > 0);
        tempSens.setOffset((float)atof(settings.tempOffs));
        #ifdef TC_HAVESPEEDO
        tempBrightness = (int)atoi(settings.tempBright);
        tempOffNM = ((int)atoi(settings.tempOffNM) > 0);
        if(!useSpeedo || useGPSSpeed) dispTemp = false;
        if(dispTemp) {
            #ifdef FAKE_POWER_ON
            if(!waitForFakePowerButton) {
            #endif
                updateTemperature(true);
                dispTemperature(true);
            #ifdef FAKE_POWER_ON
            }
            #endif
        }
        #endif
        haveRcMode = true;
    } else {
        useTemp = dispTemp = false;
    }
    #else
    useTemp = dispTemp = false;
    #endif

    #ifdef TC_HAVELIGHT
    useLight = ((int)atoi(settings.useLight) > 0);
    luxLimit = atoi(settings.luxLimit);
    if(useLight) {
        if(lightSens.begin(haveGPS, powerupMillis)) {
            lightSens.setCustomDelayFunc(myCustomDelay);
        } else {
            useLight = false;
        }
    }
    #else
    useLight = false;
    #endif

    // Set up tt trigger for external props (wired & mqtt)
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    useETTO = useETTOWired = ((int)atoi(settings.useETTO) > 0);
    #ifdef TC_HAVEMQTT
    if(useMQTT && pubMQTT) useETTO = true;
    #endif
    #endif

    // Show "REPLACE BATTERY" message if RTC battery is low or depleted
    // Note: This also shows up the first time you power-up the clock
    // AFTER a battery change.
    if(rtcbad || rtc.battLow()) {
        destinationTime.showTextDirect("REPLACE");
        presentTime.showTextDirect("BATTERY");
        destinationTime.on();
        presentTime.on();
        myIntroDelay(5000);
        allOff();
    }

    if(tzbad) {
        destinationTime.showTextDirect("BAD");
        presentTime.showTextDirect("TIME ZONE");
        destinationTime.on();
        presentTime.on();
        myIntroDelay(5000);
        allOff();
    }

    // Invoke audio file installer if SD content qualifies
    if(check_allow_CPA()) {
        #ifdef TC_DBG
        Serial.println(F("time_setup: calling doCopyAudioFiles()"));
        #endif
        destinationTime.showTextDirect("INSTALL");
        presentTime.showTextDirect("AUDIO FILES?");
        destinationTime.on();
        presentTime.on();
        doCopyAudioFiles();
        // We return here only if user cancelled or
        // if the menu timed-out
        allOff();
        waitForEnterRelease();
        isEnterKeyHeld = false;
        isEnterKeyPressed = false;
        #ifdef EXTERNAL_TIMETRAVEL_IN
        isEttKeyPressed = false;
        isEttKeyHeld = false;
        #endif
    }

    if(!audio_files_present()) {
        destinationTime.showTextDirect("PLEASE");
        presentTime.showTextDirect("INSTALL");
        departedTime.showTextDirect("AUDIO FILES");
        destinationTime.on();
        presentTime.on();
        departedTime.on();
        myIntroDelay(5000);
        allOff();
    }

    if((int)atoi(settings.playIntro)) {
        const char *t1 = "             BACK";
        const char *t2 = "TO";
        const char *t3 = "THE FUTURE";
        
        play_file("/intro.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);

        myIntroDelay(1200);
        destinationTime.setBrightnessDirect(15);
        presentTime.setBrightnessDirect(0);
        departedTime.setBrightnessDirect(0);
        presentTime.off();
        departedTime.off();
        destinationTime.showTextDirect(t1);
        presentTime.showTextDirect(t2);
        departedTime.showTextDirect(t3);
        destinationTime.on();
        for(int i = 0; i < 14; i++) {
           myIntroDelay(50, false);
           destinationTime.showTextDirect(&t1[i]);
        }
        myIntroDelay(500);
        presentTime.on();
        departedTime.on();
        for(int i = 0; i <= 15; i++) {
            presentTime.setBrightnessDirect(i);
            departedTime.setBrightnessDirect(i);
            myIntroDelay(100);
        }
        myIntroDelay(1500);
        for(int i = 15; i >= 0; i--) {
            destinationTime.setBrightnessDirect(i);
            presentTime.setBrightnessDirect(i);
            departedTime.setBrightnessDirect(i);
            myIntroDelay(20, false);
        }
        allOff();
        destinationTime.setBrightness(255);
        presentTime.setBrightness(255);
        departedTime.setBrightness(255);

        waitAudioDoneIntro();
        stopAudio();
    }

#ifdef FAKE_POWER_ON
    if(waitForFakePowerButton) {

        digitalWrite(WHITE_LED_PIN, HIGH);
        myIntroDelay(500);
        digitalWrite(WHITE_LED_PIN, LOW);
        isFPBKeyChange = false;
        FPBUnitIsOn = false;
        leds_off();

        #ifdef TC_DBG
        Serial.println(F("time_setup: waiting for fake power on"));
        #endif

    } else {
#endif

        startup = true;
        startupSound = true;
        FPBUnitIsOn = true;
        leds_on();
        if(beepMode >= 2)      startBeepTimer();
        else if(beepMode == 1) muteBeep = false;

#ifdef FAKE_POWER_ON
    }
#endif

    
}

/*
 * time_loop()
 *
 */
void time_loop()
{
    unsigned long millisNow = millis();
    #ifdef TC_DBG
    int dbgLastMin;
    #endif

    // millisEpoch counter
    if(millisNow < lastMillis) {
        millisEpoch += 0x100000000;
    }
    lastMillis = millisNow;

    #ifdef FAKE_POWER_ON
    if(waitForFakePowerButton) {
        fakePowerOnKey.scan();

        if(isFPBKeyChange) {
            if(isFPBKeyPressed) {
                if(!FPBUnitIsOn) {
                    startup = true;
                    startupSound = true;
                    FPBUnitIsOn = true;
                    leds_on();
                    if(beepMode >= 2)      startBeepTimer();
                    else if(beepMode == 1) muteBeep = false;
                    destinationTime.setBrightness(255); // restore brightnesses
                    presentTime.setBrightness(255);     // in case we got switched
                    departedTime.setBrightness(255);    // off during time travel
                    #ifdef SP_ALWAYS_ON
                    if(useSpeedo && !useGPSSpeed) {
                        speedo.setSpeed(0);
                        speedo.show();
                    }
                    #endif
                    #ifdef TC_HAVETEMP
                    updateTemperature(true);
                    #ifdef TC_HAVESPEEDO
                    dispTemperature(true);
                    #endif
                    #endif
                    triggerWC = true;
                    destShowAlt = depShowAlt = 0; // Reset TZ-Name-Animation
                }
            } else {
                if(FPBUnitIsOn) {
                    startup = false;
                    startupSound = false;
                    timeTravelP0 = 0;
                    timeTravelP1 = 0;
                    timeTravelRE = false;
                    timeTravelP2 = 0;
                    triggerP1 = 0;
                    #ifdef EXTERNAL_TIMETRAVEL_OUT
                    triggerETTO = false;
                    if(useETTO && !ettoUsePulse) {
                        ettoPulseEnd();
                        ettoPulse = false;
                    }
                    #endif
                    FPBUnitIsOn = false;
                    cancelEnterAnim(false);
                    cancelETTAnim();
                    mp_stop();
                    play_file("/shutdown.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD);
                    mydelay(130);
                    allOff();
                    leds_off();
                    #ifdef TC_HAVESPEEDO
                    if(useSpeedo && !useGPSSpeed) speedo.off();
                    #endif
                    waitAudioDone();
                    stopAudio();
                }
            }
            isFPBKeyChange = false;
        }
    }
    #endif

    // Initiate startup delay, play startup sound
    if(startupSound) {
        startupNow = pauseNow = millis();
        play_file("/startup.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD);
        startupSound = false;
        // Don't let autoInt interrupt us
        autoPaused = true;
        pauseDelay = STARTUP_DELAY + 500;
    }

    // Turn display on after startup delay
    if(startup && (millis() - startupNow >= STARTUP_DELAY)) {
        animate();
        startup = false;
        #ifdef TC_HAVESPEEDO
        if(useSpeedo && !useGPSSpeed) {
            #ifdef TC_HAVETEMP
            updateTemperature(true);
            if(!dispTemperature(true)) {
            #endif
                #ifndef SP_ALWAYS_ON
                speedo.off(); // Yes, off.
                #else
                dispIdleZero();
                #endif
            #ifdef TC_HAVETEMP
            }
            #endif  
        }
        #endif
    }

    #if defined(EXTERNAL_TIMETRAVEL_OUT) || defined(TC_HAVESPEEDO)
    // Timer for starting P1 if to be delayed
    if(triggerP1 && (millis() - triggerP1Now >= triggerP1LeadTime)) {
        triggerLongTT();
        triggerP1 = false;
    }
    #endif

    #ifdef EXTERNAL_TIMETRAVEL_OUT
    // Timer for start of ETTO signal/pulse
    if(triggerETTO && (millis() - triggerETTONow >= triggerETTOLeadTime)) {
        ettoPulseStart();
        triggerETTO = false;
        ettoPulse = true;
        ettoPulseNow = millis();
        #ifdef TC_HAVEMQTT
        if(useMQTT && pubMQTT) {
            mqttPublish("bttf/tcd/pub", "TIMETRAVEL\0", 11);
        }
        #endif
        #ifdef TC_DBG
        Serial.println(F("ETTO triggered"));
        #endif
    }
    // Timer to end ETTO pulse
    if(ettoUsePulse && ettoPulse && (millis() - ettoPulseNow >= ETTO_PULSE_DURATION)) {
        ettoPulseEnd();
        ettoPulse = false;
    }
    #endif

    // Time travel animation, phase 0: Speed counts up
    #ifdef TC_HAVESPEEDO
    if(timeTravelP0 && (millis() - timetravelP0Now >= timetravelP0Delay)) {

        unsigned long univNow = millis();
        long timetravelP0DelayT = 0;

        timeTravelP0Speed++;

        if(timeTravelP0Speed < 88) {
            unsigned long ttP0NowT = univNow;
            long ttP0LDOver = 0, ttP0LastDelay = ttP0NowT - ttP0Now;
            ttP0Now = ttP0NowT;
            ttP0LDOver = ttP0LastDelay - timetravelP0Delay;
            timetravelP0DelayT = (long)(((float)(tt_p0_delays[timeTravelP0Speed])) / ttP0TimeFactor) - ttP0LDOver;
            while(timetravelP0DelayT <= 0 && timeTravelP0Speed < 88) {
                timeTravelP0Speed++;
                timetravelP0DelayT += (long)(((float)(tt_p0_delays[timeTravelP0Speed])) / ttP0TimeFactor);
            }
        }

        if(timeTravelP0Speed < 88) {

            timetravelP0Delay = timetravelP0DelayT;
            timetravelP0Now = univNow;

        } else {

            timeTravelP0 = 0;

        }

        speedo.setSpeed(timeTravelP0Speed);
        speedo.show();
    }

    // Time travel animation, phase 2: Speed counts down
    if(timeTravelP2 && (millis() - timetravelP0Now >= timetravelP0Delay)) {
        #ifdef TC_HAVEGPS
        bool countToGPSSpeed = (useGPSSpeed && (myGPS.getSpeed() >= 0));
        uint8_t targetSpeed = countToGPSSpeed ? myGPS.getSpeed() : 0;
        #else
        uint8_t targetSpeed = 0;
        #endif
        if((timeTravelP0Speed <= targetSpeed) || (targetSpeed >= 88)) {
            timeTravelP2 = 0;
            if(!useGPSSpeed) {
                #ifndef SP_ALWAYS_ON
                speedo.off();
                #else
                dispIdleZero();
                #endif
            }
            #ifdef TC_HAVEGPS
            dispGPSSpeed(true);
            #endif
            #ifdef TC_HAVETEMP
            updateTemperature(true);
            dispTemperature(true);
            #endif
        } else {
            timetravelP0Now = millis();
            timeTravelP0Speed--;
            speedo.setSpeed(timeTravelP0Speed);
            speedo.show();
            #ifdef TC_HAVEGPS
            if(countToGPSSpeed) {
                if(targetSpeed == timeTravelP0Speed) {
                    timetravelP0Delay = 0;
                } else {
                    uint8_t tt = ((timeTravelP0Speed-targetSpeed)*100) / (88 - targetSpeed);
                    timetravelP0Delay = ((100 - tt) * 150) / 100;
                    if(timetravelP0Delay < 40) timetravelP0Delay = 40;
                }
            } else {
                timetravelP0Delay = (timeTravelP0Speed == 0) ? 4000 : 40;
            }
            #else
            timetravelP0Delay = (timeTravelP0Speed == 0) ? 4000 : 40;
            #endif
        }
    }
    #endif  // TC_HAVESPEEDO

    // Time travel animation, phase 1: Display disruption
    if(timeTravelP1 && (millis() - timetravelP1Now >= timetravelP1Delay)) {
        timeTravelP1++;
        timetravelP1Now = millis();
        switch(timeTravelP1) {
        case 2:
            allOff();
            timetravelP1Delay = TT_P1_DELAY_P2;
            break;
        case 3:
            timetravelP1Delay = TT_P1_DELAY_P3;
            break;
        case 4:
            timetravelP1Delay = TT_P1_DELAY_P4;
            break;
        case 5:
            timetravelP1Delay = TT_P1_DELAY_P5;
            break;
        default:
            timeTravelP1 = 0;
            destinationTime.setBrightness(255); // restore
            presentTime.setBrightness(255);
            departedTime.setBrightness(255);
            timeTravel(false);
        }
    }

    // Turn display back on after time traveling
    if(timeTravelRE && (millis() - timetravelNow >= TIMETRAVEL_DELAY)) {
        animate();
        timeTravelRE = false;
    }

    y = digitalRead(SECONDS_IN_PIN);
    if(y == x) {

        #ifdef TC_HAVESPEEDO
        bool didUpdSpeedo = false;
        #endif

        millisNow = millis();

        // less timing critical stuff goes here:
        // (Will be skipped in current iteration if
        // seconds change is detected)

        // Read GPS, and display GPS speed or temperature
        #ifdef TC_HAVEGPS
        if(useGPS || useGPSSpeed) {
            if(millisNow - lastLoopGPS >= GPSupdateFreq) {
                lastLoopGPS = millisNow;
                // call loop with doDelay true; delay not needed but
                // this causes a call of audio_loop() which is good
                myGPS.loop(true);
                #ifdef TC_HAVESPEEDO
                dispGPSSpeed(true);
                #endif
            }
        }
        #endif

        // Power management: CPU speed
        // Can only reduce when GPS is not used and WiFi is off
        if(!pwrLow && checkAudioDone() &&
                      #ifdef TC_HAVEGPS
                      !useGPS && !useGPSSpeed &&
                      #endif
                                 (wifiIsOff || wifiAPIsOff) && (millisNow - pwrFullNow >= 5*60*1000)) {
            setCpuFrequencyMhz(80);
            pwrLow = true;

            #ifdef TC_DBG
            Serial.printf("Reduced CPU speed to %d\n", getCpuFrequencyMhz());
            #endif
        }   

        // Beep auto modes
        if(beepTimer && (millisNow - beepTimerNow > beepTimeout)) {
            muteBeep = true;
            beepTimer = false;
        }

        // Update sensors

        #ifdef TC_HAVETEMP
        updateTemperature();
        #ifdef TC_HAVESPEEDO
        didUpdSpeedo = dispTemperature();
        #endif
        #endif

        #ifdef TC_HAVESPEEDO
        #ifdef SP_ALWAYS_ON
        if(!didUpdSpeedo && !useGPSSpeed) {
            dispIdleZero();
        }
        #endif
        #endif
        
        #ifdef TC_HAVELIGHT
        if(useLight && (millisNow - lastLoopLight >= 3000)) {
            lastLoopLight = millisNow;
            lightSens.loop();
        }
        #endif

        // End of OTPR for PCF2129
        if(OTPRinProgress) {
            if(millisNow - OTPRStarted > 100) {
                rtc.OTPRefresh(false);
                OTPRinProgress = false;
                OTPRDoneNow = millisNow;
            }
        }

    } 
    
    y = digitalRead(SECONDS_IN_PIN);
    if(y != x) {

        // Actual clock stuff
      
        if(y == 0) {

            DateTime dt;

            // Set colon
            destinationTime.setColon(true);
            presentTime.setColon(true);
            departedTime.setColon(true);

            // Play "annoying beep"(tm)
            play_beep();

            // Prepare for time re-adjustment through NTP/GPS

            if(!haveAuthTime) {
                authTimeExpired = true;
            } else if(millis() - lastAuthTime >= 7*24*60*60*1000) {
                authTimeExpired = true;
            }

            #ifdef TC_HAVEGPS
            bool GPShasTime = gpsHaveTime();
            #else
            bool GPShasTime = false;
            #endif

            // Read RTC
            myrtcnow(dt);

            // Re-adjust time periodically through NTP/GPS
            //
            // This is normally done hourly between hour:01 and hour:02. However:
            // - Exception to time rule: If there is no authTime yet, try syncing 
            //   every resyncInt+1/2 minutes or whenever GPS has time; or if
            //   syncTrigger is set (keypad "7"). (itstime)
            // - Try NTP via WiFi (doWiFi) when/if user configured a network AND
            //   -- WiFi is connected (duh!), or
            //   -- WiFi was connected and is now in power-save, but no authTime yet;
            //      this triggers a re-connect (resulting in a frozen display).
            //      This annoyance is kept to a minimum by setting the WiFi off-timer
            //      to a period longer than the period between two attempts; this in
            //      essence defeats WiFi power-save, but accurate time is more important.
            //   -- or if
            //          --- WiFi was connected and now off OR currently in AP-mode AND
            //          --- authTime has expired (after 7 days) AND
            //          --- it is night (0-6am)
            //      This also triggers a re-connect (frozen display); in this case only
            //      once every 15/90 minutes (see wifiOn) during night time until a time
            //      sync succeeds or the number of attempts exceeds a certain amount.
            // - In any case, do not interrupt any running sequences.

            bool itsTime = ( (dt.minute() == 1) || 
                             (dt.minute() == 2) ||
                             (!haveAuthTime && 
                              ( ((dt.minute() % resyncInt) == 1) || 
                                ((dt.minute() % resyncInt) == 2) ||
                                GPShasTime 
                              )
                             )                  ||
                             (syncTrigger &&
                              dt.second() == 35
                             )
                           );

            bool doWiFi = wifiHaveSTAConf &&                        // if WiFi network is configured AND
                          ( (!wifiIsOff && !wifiInAPMode)     ||    //   if WiFi-STA is on,                           OR
                            ( wifiIsOff      &&                     //   if WiFi-STA is off (after being connected), 
                              !haveAuthTime  &&                     //      but no authtime, 
                              keypadIsIdle() &&                     //      and keypad is idle (no press for >2mins)
                              checkMP3Done())                 ||    //      and no mp3 is being played                OR
                            ( (wifiIsOff ||                         //   if WiFi-STA is off (after being connected),
                               (wifiInAPMode && doAPretry)) &&      //                      or in AP-mode
                              authTimeExpired               &&      //      and authtime expired
                              checkMP3Done()                &&      //      and no mp3 being played
                              keypadIsIdle()                &&      //      and keypad is idle (no press for >2mins)
                              (dt.hour() <= 6)                      //      during night-time
                            )
                          );

            #ifdef TC_DBG
            if(dt.second() == 35) {
                Serial.printf("time_loop: %d %d %d %d %d\n", couldHaveAuthTime, itsTime, doWiFi, haveAuthTime, syncTrigger);
            }
            #endif
            
            if( couldHaveAuthTime       &&
                itsTime                 &&
                (GPShasTime || doWiFi)  &&
                !timeTravelP0 && !timeTravelP1 && !timeTravelRE && !timeTravelP2 ) {

                if(!autoReadjust) {

                    uint64_t oldT;

                    if(timeDifference) {
                        oldT = dateToMins(dt.year() - presentTime.getYearOffset(),
                                       dt.month(), dt.day(), dt.hour(), dt.minute());
                    }

                    // Note that the following call will fail if getNTPtime reconnects
                    // WiFi; no current NTP time stamp will be available. Repeated calls
                    // will eventually succeed.

                    if(getNTPOrGPSTime(haveAuthTime, dt)) {

                        bool wasFakeRTC = false;
                        uint64_t allowedDiff = 61;

                        autoReadjust = true;
                        resyncInt = 5;
                        syncTrigger = false;

                        // We have current DST determination, skip it below
                        checkDST = false;

                        if(couldDST[0]) allowedDiff = tzDiff[0] + 1;

                        haveAuthTime = true;
                        lastAuthTime = millis();
                        lastAuthTime64 = lastAuthTime + millisEpoch;
                        authTimeExpired = false;

                        // We have just adjusted, don't do it again below
                        lastYear = dt.year() - presentTime.getYearOffset();

                        #ifdef TC_DBG
                        Serial.println(F("time_loop: RTC re-adjusted using NTP or GPS"));
                        #endif

                        if(timeDifference) {
                            uint64_t newT = dateToMins(dt.year() - presentTime.getYearOffset(),
                                                    dt.month(), dt.day(), dt.hour(), dt.minute());
                            wasFakeRTC = (newT > oldT) ? (newT - oldT > allowedDiff) : (oldT - newT > allowedDiff);

                            // User played with RTC; return to actual present time
                            // (Allow a difference of 'allowedDiff' minutes to keep
                            // timeDifference in the event of a DST change)
                            if(wasFakeRTC) timeDifference = 0;
                        }

                        // Save to NVM if change is detected, or if RTC was way off
                        if( (presentTime.getYearOffset() != presentTime.loadYOffs()) || 
                            wasFakeRTC                                               ||
                            (presentTime.getDST() != presentTime.loadDST()) ) {
                            if(timetravelPersistent) {
                                presentTime.save();
                            } else {
                                presentTime.saveYOffs();
                            }
                        }

                    } else {

                        // No auth time at this point, do DST check below
                        checkDST = couldDST[0];

                        #ifdef TC_DBG
                        Serial.println(F("time_loop: RTC re-adjustment via NTP/GPS failed"));
                        #endif

                    }

                } else {

                    // We have current DST determination, skip it below
                    checkDST = false;

                }

            } else {
                
                autoReadjust = false;

                // If GPS is used for time, resyncInt is possibly set to 2 during boot;
                // reset it to 5 after 5 minutes since boot.
                if(resyncInt != 5) {
                    if(millis() - powerupMillis > 5*60*1000) {
                        resyncInt = 5;
                    }
                }

                // No auth time at this point, do DST check below
                checkDST = couldDST[0];

            }

            // Detect and handle year changes

            {
                uint16_t thisYear = dt.year() - presentTime.getYearOffset();

                if( (thisYear != lastYear) || (thisYear > 9999) ) {

                    uint16_t rtcYear;
                    int16_t yOffs = 0;
    
                    if(thisYear > 9999) {
                        #ifdef TC_DBG
                        Serial.println(F("time_loop: Rollover 9999->1 detected"));
                        #endif

                        // We do not roll over to year "0".
                        // TZ calc doesn't cope with y0.
                        thisYear = 1;
    
                        if(timeDifference) {
                            // Correct timeDifference [r-o to 0: 5259492000]
                            if(timeDifference >= 5258964960) {
                                timeDifference -= 5258964960;
                            } else {
                                timeDifference = 5258964960 - timeDifference;
                                timeDiffUp = !timeDiffUp;
                            }
                        }
                    }
    
                    // Parse TZ and set up DST data for now current year
                    if(!(parseTZ(0, thisYear))) {
                        #ifdef TC_DBG
                        Serial.println(F("time_loop: [year change] Failed to parse TZ"));
                        #endif
                    }
    
                    // Get RTC-fit year & offs for real year
                    rtcYear = thisYear;
                    correctYr4RTC(rtcYear, yOffs);
    
                    // If year-translation changed, update RTC and save
                    if((rtcYear != dt.year()) || (yOffs != presentTime.getYearOffset())) {
    
                        // Update RTC
                        rtc.adjust(dt.second(), 
                                   dt.minute(), 
                                   dt.hour(), 
                                   dayOfWeek(dt.day(), dt.month(), thisYear),
                                   dt.day(), 
                                   dt.month(), 
                                   rtcYear-2000
                        );
    
                        presentTime.setYearOffset(yOffs);

                        dt.setYear(rtcYear-2000);
    
                        // Save YearOffs to NVM if change is detected
                        if(yOffs != presentTime.loadYOffs()) {
                            if(timetravelPersistent) {
                                presentTime.save();
                            } else {
                                presentTime.saveYOffs();
                            }
                        }
    
                    }
                }
            }

            // Check if we need to switch from/to DST

            // Check every dstChkInt-th minute
            // (Do not use "checkDST && min%x == 0": checkDST
            // might vary, so the one-time-check isn't one)
            if((dt.minute() % dstChkInt) == 0) {

                if(!DSTcheckDone) {

                    // Set to 5 (might be 1 from time_setup)
                    // Safe to set here, because checkDST is only
                    // FALSE if no DST is defined or we just
                    // adjusted via NTP/GPS; in both cases the
                    // purpose of setting it to 1 is moot.
                    dstChkInt = 5;

                    if(checkDST) {

                        int oldDST = presentTime.getDST();
                        int currTimeMins = 0;
                        int myDST = timeIsDST(0, dt.year() - presentTime.getYearOffset(), dt.month(), 
                                                    dt.day(), dt.hour(), dt.minute(), currTimeMins);

                        DSTcheckDone = true;

                        if( (myDST != oldDST) &&
                            (!(blockDSTChange(currTimeMins))) ) {
                          
                            int nyear, nmonth, nday, nhour, nminute;
                            int myDiff = tzDiff[0];
                            uint64_t rtcTime;
                            uint16_t rtcYear;
                            int16_t  yOffs = 0;
        
                            #ifdef TC_DBG
                            Serial.printf("time_loop: DST change detected: %d -> %d\n", presentTime.getDST(), myDST);
                            #endif
        
                            presentTime.setDST(myDST);
        
                            // If DST status is -1 (unknown), do not adjust clock,
                            // only save corrected flag
                            if(oldDST >= 0) {
        
                                if(myDST == 0) myDiff *= -1;
            
                                rtcTime = dateToMins(dt.year() - presentTime.getYearOffset(),
                                                   dt.month(), dt.day(), dt.hour(), dt.minute());
            
                                rtcTime += myDiff;
            
                                minsToDate(rtcTime, nyear, nmonth, nday, nhour, nminute);
            
                                // Get RTC-fit year & offs for our real year
                                rtcYear = nyear;
                                correctYr4RTC(rtcYear, yOffs);
            
                                rtc.adjust(dt.second(), 
                                           nminute, 
                                           nhour, 
                                           dayOfWeek(nday, nmonth, nyear),
                                           nday, 
                                           nmonth, 
                                           rtcYear-2000
                                );
            
                                presentTime.setYearOffset(yOffs);

                                dt.set(rtcYear-2000, nmonth, nday, 
                                       nhour, nminute, dt.second());
        
                            }
        
                            // Save yearOffset & isDTS to NVM
                            if(timetravelPersistent) {
                                presentTime.save();
                            } else {
                                presentTime.saveYOffs();
                            }

                        }
                    }
                }
                
            } else {

                DSTcheckDone = false;
                
            }

            // Write time to presentTime display
            presentTime.setDateTimeDiff(dt);

            // Handle WC mode (load dates/times for dest/dep display)
            // (Restoring not needed, done elsewhere)
            if(isWcMode()) {
                if((dt.minute() != wcLastMin) || triggerWC) {
                    wcLastMin = dt.minute();
                    setDatesTimesWC(dt);
                }
                // Put city/loc name on for 3 seconds
                if(dt.second() % 10 == 3) {
                    if(haveTZName1) destShowAlt = 3*2;
                    if(haveTZName2) depShowAlt = 3*2;
                }
            }
            triggerWC = false;

            // Update "lastYear" and save to NVM
            lastYear = dt.year() - presentTime.getYearOffset();
            presentTime.saveLastYear(lastYear);

            // Debug log beacon
            #ifdef TC_DBG
            if((dt.second() == 0) && (dt.minute() != dbgLastMin)) {
                dbgLastMin = dt.minute();
                Serial.printf("%d[%d-(%d)]/%02d/%02d %02d:%02d:00 (Chip Temp %.2f) / WD of PT: %d (%d)\n",
                      lastYear, dt.year(), presentTime.getYearOffset(), dt.month(), dt.day(), dt.hour(), dbgLastMin,
                      rtc.getTemperature(),
                      dayOfWeek(presentTime.getDay(), presentTime.getMonth(), presentTime.getDisplayYear()),
                      dayOfWeek(dt.day(), dt.month(), dt.year() - presentTime.getYearOffset()));
            }
            #endif

            // Alarm, count-down timer, Sound-on-the-Hour, Reminder

            {
                #ifdef TC_HAVELIGHT
                bool switchNMoff = false;
                #endif
                int compHour = alarmRTC ? dt.hour()   : presentTime.getHour();
                int compMin  = alarmRTC ? dt.minute() : presentTime.getMinute();
                int weekDay =  alarmRTC ? dayOfWeek(dt.day(), 
                                                    dt.month(), 
                                                    dt.year() - presentTime.getYearOffset())
                                          : 
                                          dayOfWeek(presentTime.getDay(), 
                                                    presentTime.getMonth(), 
                                                    presentTime.getDisplayYear());

                // Sound to play hourly (if available)
                // Follows setting for alarm as regards the option
                // of "real actual present time" vs whatever is currently
                // displayed on presentTime.

                if(compMin == 0) {
                    if(presentTime.getNightMode() ||
                       !FPBUnitIsOn ||
                       startup      ||
                       timeTravelP0 ||
                       timeTravelP1 ||
                       timeTravelRE ||
                       (alarmOnOff && (alarmHour == compHour) && (alarmMinute == compMin))) {
                        hourlySoundDone = true;
                    }
                    if(!hourlySoundDone) {
                        play_hour_sound(compHour);
                        hourlySoundDone = true;
                    }
                } else {
                    hourlySoundDone = false;
                }

                // Handle count-down timer
                if(ctDown) {
                    if(millis() - ctDownNow > ctDown) {
                        if( (!(alarmOnOff && (alarmHour == compHour) && (alarmMinute == compMin))) ||
                            (alarmDone && checkAudioDone()) ) {
                            play_file("/timer.mp3", PA_INTRMUS|PA_ALLOWSD);
                            ctDown = 0;
                        }
                    }
                }

                // Handle reminder
                if(remMonth > 0 || remDay > 0) {
                      if( (!remMonth || remMonth == dt.month()) &&
                          (remDay == dt.day())                  &&
                          (remHour == dt.hour())                &&
                          (remMin == dt.minute()) ) {
                            if(!remDone) {
                                if( (!(alarmOnOff && (alarmHour == compHour) && (alarmMinute == compMin))) ||
                                    (alarmDone && checkAudioDone()) ) {
                                        play_file("/reminder.mp3", PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
                                        remDone = true;
                                }
                            }
                      } else {
                          remDone = false;
                      }
                }

                // Handle alarm
                if(alarmOnOff) {
                    if((alarmHour == compHour) && (alarmMinute == compMin) && 
                                  (alarmWDmasks[alarmWeekday] & (1 << weekDay))) {
                        if(!alarmDone) {
                            play_file("/alarm.mp3", PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
                            alarmDone = true;
                            #ifdef TC_HAVEMQTT
                            if(useMQTT && pubMQTT) {
                                mqttPublish("bttf/tcd/pub", "ALARM\0", 6);
                            }
                            #endif
                        }
                    } else {
                        alarmDone = false;
                    }
                }

                // Handle Auto-NightMode

                // Manually switching NM pauses automatic for 30 mins
                if(manualNightMode >= 0 && (millis() - manualNMNow > 30*60*1000)) {
                    manualNightMode = -1;
                    // Re-evaluate auto-nm immediately
                    forceReEvalANM = true;
                }
                
                if(autoNightMode && (manualNightMode < 0)) {
                    if(dt.minute() == 0 || forceReEvalANM) {
                        if(!autoNMDone || forceReEvalANM) {
                            uint32_t myField;
                            if(autoNightModeMode == 0) {
                                myField = autoNMdailyPreset;
                            } else {
                                const uint32_t *myFieldPtr = autoNMPresets[autoNightModeMode - 1];
                                myField = myFieldPtr[weekDay];
                            }                       
                            if(myField & (1 << (23 - dt.hour()))) {
                                nightModeOn();
                                timedNightMode = 1;
                            } else {
                                #ifdef TC_HAVELIGHT
                                switchNMoff = true;
                                #else
                                nightModeOff();
                                #endif
                                timedNightMode = 0;
                            }
                            autoNMDone = true;
                        }
                    } else {
                        autoNMDone = false;
                    }
                    forceReEvalANM = false;
                }
                #ifdef TC_HAVELIGHT
                // Light sensor overrules scheduled NM only in non-NightMode periods
                if(useLight && (manualNightMode < 0) && (timedNightMode < 1)) {
                    int32_t myLux = lightSens.readLux();
                    if(myLux >= 0) {
                        if(myLux > luxLimit) {
                            sensorNightMode = 0;
                            switchNMoff = true;
                        } else {
                            sensorNightMode = 1;
                            nightModeOn();
                        }
                    } else {
                        // Bad lux (probably by sensor overload), switch NM off
                        sensorNightMode = -1;
                        switchNMoff = true;
                    }
                }
                if(switchNMoff) {
                    if(sensorNightMode < 1) nightModeOff();
                    switchNMoff = false;
                }
                #endif

            }

            // Handle Time Cycling ("decorative mode")

            // Do this on previous minute:59
            minNext = (dt.minute() == 59) ? 0 : dt.minute() + 1;

            // Check if autoPause has run out
            if(autoPaused && (millis() - pauseNow >= pauseDelay)) {
                autoPaused = false;
            }

            // Only do this on second 59, check if it's time to do so
            if((dt.second() == 59)                                      &&
               (!autoPaused)                                            &&
               autoTimeIntervals[autoInterval]                          &&
               (minNext % autoTimeIntervals[autoInterval] == 0)         &&
               #ifdef TC_HAVETEMP                             // Skip in rcMode if (temp&hum available || wcMode)
               ( !(isRcMode() && (isWcMode() || tempSens.haveHum())) )  &&   
               #endif
               (!isWcMode() || !WcHaveTZ1 || !WcHaveTZ2) ) {  // Skip in wcMode if both TZs configured

                if(!autoIntDone) {

                    autoIntDone = true;

                    // cycle through pre-programmed times
                    autoTime++;
                    if(autoTime >= NUM_AUTOTIMES) autoTime = 0;

                    // Show a preset dest and departed time
                    // RcMode irrelevant; continue cycle in the background
                    if(!isWcMode() || !WcHaveTZ1) {
                        destinationTime.setFromStruct(&destinationTimes[autoTime]);
                    }
                    if(!isWcMode() || !WcHaveTZ2) {
                        departedTime.setFromStruct(&departedTimes[autoTime]);
                    }

                    allOff();

                    autoIntAnimRunning = 1;
                
                } 

            } else {

                autoIntDone = false;
                if(autoIntAnimRunning)
                    autoIntAnimRunning++;

            }

        } else {

            millisNow = millis();

            destinationTime.setColon(false);
            presentTime.setColon(false);
            departedTime.setColon(false);

            // OTPR for PCF2129 every two weeks
            if(RTCNeedsOTPR && (millisNow - OTPRDoneNow > 2*7*24*60*60*1000)) {
                rtc.OTPRefresh(true);
                OTPRinProgress = true;
                OTPRStarted = millisNow;
            }

            if(autoIntAnimRunning)
                autoIntAnimRunning++;

        }

        x = y;

        if(timeTravelP1 > 1) {
            int ii = 5, tt;
            switch(timeTravelP1) {
            case 2:
                ((rand() % 10) > 7) ? presentTime.off() : presentTime.on();
                (((rand()+millis()) % 10) > 7) ? destinationTime.off() : destinationTime.on();
                ((rand() % 10) > 7) ? departedTime.off() : departedTime.on();
                break;
            case 3:
                presentTime.off();
                destinationTime.off();
                departedTime.off();
                break;
            case 4:
                destinationTime.show();
                presentTime.show();
                departedTime.show();
                destinationTime.on();
                presentTime.on();
                departedTime.on();
                while(ii--) {
                    #ifdef IS_ACAR_DISPLAY
                    #define JAN011885 "010118851200"
                    #else
                    #define JAN011885 "JAN0118851200"
                    #endif
                    ((rand() % 10) < 4) ? destinationTime.showTextDirect(JAN011885, CDT_COLON) : destinationTime.show();
                    if(!(ii % 2)) destinationTime.setBrightnessDirect((1+(rand() % 10)) & 0x0a);
                    if(ii % 2) presentTime.setBrightnessDirect((1+(rand() % 10)) & 0x0b);
                    ((rand() % 10) < 3) ? departedTime.showTextDirect(">ACS2011GIDUW") : departedTime.show();
                    if(ii % 2) departedTime.setBrightnessDirect((1+(rand() % 10)) & 0x07);
                    mydelay(20);
                }
                break;
            case 5:
                departedTime.setBrightness(255);
                departedTime.on();
                while(ii--) {
                    tt = rand() % 10;
                    if(!(ii % 4))   presentTime.setBrightnessDirect(1+(rand() % 8));
                    if(tt < 3)      { presentTime.setBrightnessDirect(4); presentTime.lampTest(true); }
                    else if(tt < 7) { presentTime.show(); presentTime.on(); }
                    else            { presentTime.off(); }
                    tt = (rand() + millis()) % 10;
                    if(tt < 2)      { destinationTime.showTextDirect("8888888888888"); }
                    else if(tt < 6) { destinationTime.show(); destinationTime.on(); }
                    else            { if(!(ii % 2)) destinationTime.setBrightnessDirect(1+(rand() % 8)); }
                    tt = (tt + (rand() + millis())) % 10;
                    if(tt < 4)      { departedTime.setBrightnessDirect(4); departedTime.lampTest(true); }
                    else if(tt < 7) { departedTime.showTextDirect("R 2 0 1 1 T R "); }
                    else            { departedTime.show(); }
                    mydelay(10);
                    
                    #if 0 // Code of death for some red displays, seems they don't like realLampTest
                    tt = rand() % 10; 
                    presentTime.setBrightnessDirect(1+(rand() % 8));
                    if(tt < 3)      { presentTime.realLampTest(); }
                    else if(tt < 7) { presentTime.show(); presentTime.on(); }
                    else            { presentTime.off(); }
                    tt = (rand() + millis()) % 10;
                    if(tt < 2)      { destinationTime.realLampTest(); }
                    else if(tt < 6) { destinationTime.show(); destinationTime.on(); }
                    else            { destinationTime.setBrightnessDirect(1+(rand() % 8)); }  
                    tt = (rand() + millis()) % 10; 
                    if(tt < 4)      { departedTime.realLampTest(); }
                    else if(tt < 8) { departedTime.showTextDirect("00000000000000"); departedTime.on(); }
                    else            { departedTime.off(); }
                    mydelay(10);
                    #endif
                }
                break;
            default:
                allOff();
            }
            
        } else if(autoIntAnimRunning) {

            if(autoIntAnimRunning >= 3) {
                if(FPBUnitIsOn) animate();
                autoIntAnimRunning = 0;
            }
                
        } else if(!startup && !timeTravelRE && FPBUnitIsOn) {

            #ifdef TC_HAVEMQTT
            if(mqttDisp) { 
                if(!specDisp) {
                    destinationTime.showTextDirect(mqttMsg + mqttIdx);
                    if(mqttST) {
                        if(!presentTime.getNightMode()) {
                            play_file(mqttAudioFile, PA_CHECKNM|PA_ALLOWSD);
                        }
                        mqttST = false;
                    }
                    if(mqttOldDisp != mqttDisp) {
                        mqttStartNow = millis();
                        mqttOldDisp = mqttDisp;
                    }
                    if(mqttMaxIdx < 0) {
                        if(millis() - mqttStartNow > 5000) {
                            mqttDisp = mqttOldDisp = 0;
                        }
                    } else {
                        if(millis() - mqttStartNow > 200) { // useless actually, we are in .5 second steps here
                            mqttStartNow = millis();
                            mqttIdx++;
                            if(mqttIdx > mqttMaxIdx) {
                                mqttDisp = mqttOldDisp = 0;
                            }
                        }
                    }
                } else {
                    mqttOldDisp = mqttIdx = 0;
                }
            }
            #endif

            #ifdef TC_HAVETEMP
            if(isRcMode()) {
                
                if(!specDisp && !mqttDisp) {
                    if(!isWcMode() || !WcHaveTZ1) {
                        destinationTime.showTempDirect(tempSens.readLastTemp(), tempUnit);
                    } else {
                        destShowAlt ? destinationTime.showAlt() : destinationTime.show();
                    }
                }
                if(isWcMode() && WcHaveTZ1) {
                    departedTime.showTempDirect(tempSens.readLastTemp(), tempUnit);
                } else if(!isWcMode() && tempSens.haveHum()) {
                    departedTime.showHumDirect(tempSens.readHum());
                } else {
                    depShowAlt ? departedTime.showAlt() : departedTime.show();
                }

            } else {
            #endif
                if(!specDisp && !mqttDisp) {
                    destShowAlt ? destinationTime.showAlt() : destinationTime.show();
                }
                depShowAlt ? departedTime.showAlt() : departedTime.show();
            #ifdef TC_HAVETEMP
            }
            #endif

            presentTime.show();
            
        }

        if(destShowAlt) destShowAlt--;
        if(depShowAlt) depShowAlt--;
    } 
}


/* Time Travel:
 *
 *  A time travel consists of various phases:
 *  - Speedo acceleration (if applicable; if GPS is in use, starts at actual speed)
 *  - actual tt (display disruption)
 *  - re-entry
 *  - Speed de-acceleration (if applicable; if GPS is in use, counts down to actual speed)
 *  - ETTO lead time period (trigger external props, if applicable)
 *
 *  |<--------- P0: speedo acceleration ------>|                         |<P2:speedo de-accleration>|
 *  0....10....20....................xx....87..88------------------------88...87....................0
 *                                   |<-------- travelstart.mp3 -------->|<-tt.mp3>|
 *                                   11111111111111111111111111111111111122222222222
 *                                   |         |<--Actual Time Travel -->|
 *                           P1 starts         |  (Display disruption)   |
 *                                        TT starts                      Reentry phase
 *                                             |                         |
 *               |<---------ETTO lead--------->|                         |
 *               |                             |                         |
 *          ETTO: Pulse                  TT_P1_POINT88                   |
 *               or                    (ms from P1 start)                |
 *           LOW->HIGH                                            ETTO: HIGH->LOW
 *       [MQTT: "TIMETRAVEL']                                    [MQTT: "REENTRY']                          
 *
 *  As regards the mere "time" logic, a time travel means to
 *  -) copy present time into departed time (where it freezes)
 *  -) copy destination time to present time (where it continues to run as a clock)
 *
 *  This is also called from tc_keypad.cpp
 */

void timeTravel(bool doComplete, bool withSpeedo)
{
    int   tyr = 0;
    int   tyroffs = 0;
    long  currTotDur = 0;
    unsigned long ttUnivNow = millis();

    pwrNeedFullNow();

    // Disable RC and WC modes
    // Note that in WC mode, the times in red & yellow
    // displays work like "normal" times there: The user
    // travels to whatever time is currently in the red
    // display, and his current time becomes last time
    // departed - and both become stale.
    enableRcMode(false);
    enableWcMode(false);

    cancelEnterAnim();
    cancelETTAnim();

    // Stop music if we are to play time travel sounds
    if(playTTsounds) mp_stop();
    
    // Beep auto mode: Restart timer
    startBeepTimer();

    // Pause autoInterval-cycling so user can play undisturbed
    pauseAuto();

    /*
     * Complete sequence with speedo: Initiate P0 (speed count-up)
     *
     */
    #ifdef TC_HAVESPEEDO
    if(doComplete && useSpeedo && withSpeedo) {

        timeTravelP0Speed = 0;
        timetravelP0Delay = 2000;
        triggerP1 = false;
        #ifdef EXTERNAL_TIMETRAVEL_OUT
        triggerETTO = ettoPulse = false;
        if(useETTO) ettoPulseEnd();
        #endif

        #ifdef TC_HAVEGPS
        if(useGPSSpeed) {
            int16_t tempSpeed = myGPS.getSpeed();
            if(tempSpeed >= 0) {
                timeTravelP0Speed = tempSpeed;
                timetravelP0Delay = 0;
                if(timeTravelP0Speed < 88) {
                    currTotDur = tt_p0_totDelays[timeTravelP0Speed];
                }
            }
        }
        #endif

        if(timeTravelP0Speed < 88) {

            // If time needed to reach 88mph is shorter than ettoLeadTime
            // or pointOfP1: Need add'l delay before speed counter kicks in.
            // This add'l delay is put into timetravelP0Delay.

            #ifdef EXTERNAL_TIMETRAVEL_OUT
            if(useETTO) {

                triggerETTO = true;
                triggerP1 = true;
                triggerETTONow = triggerP1Now = ttUnivNow;

                if(currTotDur >= ettoLeadPoint || currTotDur >= pointOfP1) {

                    if(currTotDur >= ettoLeadPoint && currTotDur >= pointOfP1) {

                        // Both outside our remaining time period:
                        if(ettoLeadPoint <= pointOfP1) {
                            // ETTO first:
                            triggerETTOLeadTime = 0;
                            triggerP1LeadTime = pointOfP1 - ettoLeadPoint;
                            timetravelP0Delay = currTotDur - ettoLeadPoint;
                        } else if(ettoLeadPoint > pointOfP1) {
                            // P1 first:
                            triggerP1LeadTime = 0;
                            triggerETTOLeadTime = ettoLeadPoint - pointOfP1;
                            timetravelP0Delay = currTotDur - pointOfP1;
                        }

                    } else if(currTotDur >= ettoLeadPoint) {

                        // etto outside
                        triggerETTOLeadTime = 0;
                        triggerP1LeadTime = pointOfP1 - ettoLeadPoint;
                        timetravelP0Delay = currTotDur - ettoLeadPoint;

                    } else {

                        // P1 outside
                        triggerP1LeadTime = 0;
                        triggerETTOLeadTime = ettoLeadPoint - pointOfP1;
                        timetravelP0Delay = currTotDur - pointOfP1;

                    }

                } else {

                    //triggerP1LeadTime = pointOfP1 - currTotDur;
                    //triggerETTOLeadTime = ettoLeadPoint - currTotDur;
                    //timetravelP0Delay = 0;
                    triggerP1LeadTime = pointOfP1 - currTotDur + timetravelP0Delay;
                    triggerETTOLeadTime = ettoLeadPoint - currTotDur + timetravelP0Delay;

                }

            } else
            #endif
            if(currTotDur >= pointOfP1) {
                 triggerP1 = true;
                 triggerP1Now = ttUnivNow;
                 triggerP1LeadTime = 0;
                 timetravelP0Delay = currTotDur - pointOfP1;
            } else {
                 triggerP1 = true;
                 triggerP1Now = ttUnivNow;
                 triggerP1LeadTime = pointOfP1 - currTotDur + timetravelP0Delay;
            }

            speedo.setSpeed(timeTravelP0Speed);
            speedo.setBrightness(255);
            speedo.show();
            speedo.on();
            timetravelP0Now = ttP0Now = ttUnivNow;
            timeTravelP0 = 1;
            timeTravelP2 = 0;

            return;
        }
        // If (actual) speed >= 88, trigger P1 immediately
    }
    #endif

    /*
     * Complete tt: Trigger P1 (display disruption)
     *
     */
    if(doComplete) {

        #ifdef EXTERNAL_TIMETRAVEL_OUT
        if(useETTO) {
            triggerP1 = true;
            triggerETTO = true;
            triggerETTONow = triggerP1Now = ttUnivNow;

            if(ettoLeadTime >= (TT_P1_POINT88 + TT_SNDLAT)) {
                triggerETTOLeadTime = 0;
                triggerP1LeadTime = ettoLeadTime - (TT_P1_POINT88 + TT_SNDLAT);
            } else {
                triggerP1LeadTime = 0;
                triggerETTOLeadTime = (TT_P1_POINT88 + TT_SNDLAT) - ettoLeadTime;
            }

            return;
        }
        #endif

        triggerLongTT();

        return;
    }

    /*
     * Re-entry part:
     *
     */

    timetravelNow = ttUnivNow;
    timeTravelRE = true;

    if(playTTsounds) play_file("/timetravel.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);

    allOff();

    // Copy present time to last time departed
    departedTime.setYear(presentTime.getDisplayYear());
    departedTime.setMonth(presentTime.getMonth());
    departedTime.setDay(presentTime.getDay());
    departedTime.setHour(presentTime.getHour());
    departedTime.setMinute(presentTime.getMinute());
    departedTime.setYearOffset(0);

    // We only save the new time to NVM if user wants persistence.
    // Might not be preferred; first, this messes with the user's custom
    // times. Secondly, it wears the flash memory.
    if(timetravelPersistent) {
        departedTime.save();
    }

    // Calculate time difference between RTC and destination time

    DateTime dt;
    myrtcnow(dt);
    uint64_t rtcTime = dateToMins(dt.year() - presentTime.getYearOffset(),
                                  dt.month(),
                                  dt.day(),
                                  dt.hour(),
                                  dt.minute());

    uint64_t newTime = dateToMins(
                destinationTime.getYear(),
                destinationTime.getMonth(),
                destinationTime.getDay(),
                destinationTime.getHour(),
                destinationTime.getMinute());

    if(rtcTime < newTime) {
        timeDifference = newTime - rtcTime;
        timeDiffUp = true;
    } else {
        timeDifference = rtcTime - newTime;
        timeDiffUp = false;
    }

    // Save presentTime settings (timeDifference) if to be persistent
    if(timetravelPersistent) {
        presentTime.save();
    }

    // If speedo was used: Initiate P2: Count speed down
    #ifdef TC_HAVESPEEDO
    if(useSpeedo && (timeTravelP0Speed == 88)) {
        timeTravelP2 = 1;
        timetravelP0Now = ttUnivNow;
        timetravelP0Delay = 2000;
    }
    #endif

    // For external props: Signal Re-Entry by ending tigger signal
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    if(useETTO) {
        ettoPulseEnd();
        #ifdef TC_HAVEMQTT
        if(useMQTT && pubMQTT) {
            mqttPublish("bttf/tcd/pub", "REENTRY\0", 8);
        }
        #endif
    }
    #endif
}

static void triggerLongTT()
{
    if(playTTsounds) play_file("/travelstart.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
    timetravelP1Now = millis();
    timetravelP1Delay = TT_P1_DELAY_P1;
    timeTravelP1 = 1;
}

#ifdef EXTERNAL_TIMETRAVEL_OUT
static void ettoPulseStart()
{
    if(useETTOWired) {
        digitalWrite(EXTERNAL_TIMETRAVEL_OUT_PIN, HIGH);
    }
    #ifdef TC_DBG
    digitalWrite(WHITE_LED_PIN, HIGH);
    #endif
}
static void ettoPulseEnd()
{
    if(useETTOWired) {
        digitalWrite(EXTERNAL_TIMETRAVEL_OUT_PIN, LOW);
    }
    #ifdef TC_DBG
    digitalWrite(WHITE_LED_PIN, LOW);
    #endif
}
#endif

/*
 * Reset present time to actual present time
 * (aka "return from time travel")
 */
void resetPresentTime()
{
    pwrNeedFullNow();

    timetravelNow = millis();
    timeTravelRE = true;
    
    if(timeDifference && playTTsounds) {
        mp_stop();
        play_file("/timetravel.mp3", PA_CHECKNM|PA_INTRMUS|PA_ALLOWSD|PA_DYNVOL);
    }

    // Disable RC and WC modes
    // Note that in WC mode, the times in red & yellow
    // displays work like "normal" times there: The user
    // travels to present time, his current time becomes 
    // last time departed; destination time is unchanged,
    // but both dest & last time dep. time become stale.
    enableRcMode(false);
    enableWcMode(false);

    cancelEnterAnim();
    cancelETTAnim();

    allOff();

    // Copy "present" time to last time departed
    departedTime.setYear(presentTime.getDisplayYear());
    departedTime.setMonth(presentTime.getMonth());
    departedTime.setDay(presentTime.getDay());
    departedTime.setHour(presentTime.getHour());
    departedTime.setMinute(presentTime.getMinute());
    departedTime.setYearOffset(0);

    // We only save the new time if user wants persistence.
    // Might not be preferred; first, this messes with the user's 
    // custom times. Secondly, it wears the flash memory.
    if(timetravelPersistent) {
        departedTime.save();
    }

    // Reset time, Yes, it's that simple.
    timeDifference = 0;

    // Save presentTime settings (timeDifference) if to be persistent
    if(timetravelPersistent) {
        presentTime.save();
    }

    // Beep auto mode: Restart timer
    startBeepTimer();
}

// Pause autoInverval-updating for 30 minutes
// Subsequent calls re-start the pause; therefore, it
// is not advised to use different pause durations
void pauseAuto(void)
{
    if(autoTimeIntervals[autoInterval]) {
        pauseDelay = 30 * 60 * 1000;
        autoPaused = true;
        pauseNow = millis();
        #ifdef TC_DBG
        Serial.println(F("pauseAuto: autoInterval paused for 30 minutes"));
        #endif
    }
}

bool checkIfAutoPaused()
{
    if(!autoPaused || (millis() - pauseNow >= pauseDelay)) {
        return false;
    }
    return true;
}

void endPauseAuto(void)
{
    autoPaused = false;
}

/*
 * Callbacks for fake power switch
 */
#ifdef FAKE_POWER_ON
void fpbKeyPressed()
{
    isFPBKeyPressed = true;
    isFPBKeyChange = true;
}
void fpbKeyLongPressStop()
{
    isFPBKeyPressed = false;
    isFPBKeyChange = true;
}
#endif

/*
 * Delay function for keeping essential stuff alive
 * during Intro playback
 * 
 */
static void myIntroDelay(unsigned int mydel, bool withGPS)
{
    unsigned long startNow = millis();
    while(millis() - startNow < mydel) {
        delay(5);
        audio_loop();
        ntp_short_loop();
        #ifdef TC_HAVEGPS
        if(withGPS) gps_loop();
        #endif
        if(withGPS) wifi_loop();
    }
}

static void waitAudioDoneIntro()
{
    int timeout = 100;

    while(!checkAudioDone() && timeout--) {
        audio_loop();
        ntp_short_loop();
        #ifdef TC_HAVEGPS
        gps_loop();
        #endif
        wifi_loop();
        audio_loop();
        delay(10);
    }
}

/*
 * Delay function for external modules
 * (gps, temp sensor, light sensor). 
 * Do not call gps_loop() or wifi_loop() here!
 */
static void myCustomDelay(unsigned int mydel)
{
    unsigned long startNow = millis();
    audio_loop();
    while(millis() - startNow < mydel) {
        delay(5);
        audio_loop();
        ntp_short_loop();
    }
}


// Call this to get full CPU speed
void pwrNeedFullNow(bool force)
{
    if(pwrLow || force) {
        setCpuFrequencyMhz(240);
        #ifdef TC_DBG
        Serial.print("Setting CPU speed to ");
        Serial.println(getCpuFrequencyMhz());
        #endif
    }
    pwrFullNow = millis();
    pwrLow = false;
}

/*
 * Internal replacement for RTC.now()
 * RTC sometimes loses sync and does not send data,
 * which is interpreted as 2165/165/165 etc
 * Check for this and retry in case.
 */
void myrtcnow(DateTime& dt)
{
    int retries = 0;

    rtc.now(dt);

    while ((dt.month() < 1 || dt.month() > 12 ||
            dt.day()   < 1 || dt.day()   > 31 ||
            dt.hour() > 23 ||
            dt.minute() < 0 || dt.minute() > 59) &&
            retries < 30 ) {

            mydelay((retries < 5) ? 50 : 100);
            rtc.now(dt);
            retries++;
    }

    if(retries > 0) {
        Serial.printf("myrtcnow: %d retries needed to read RTC. Check your i2c cabling.\n", retries);
    }
}

/*
 * World Clock setters/getters
 * 
 */

void enableWcMode(bool onOff)
{
    if(haveWcMode) {
        wcMode = onOff;
        triggerWC = true;
    }
}

bool toggleWcMode()
{
    enableWcMode(!wcMode);
    return wcMode;
}

bool isWcMode()
{
    return wcMode;
}

/*
 * Room Condition setters/getters
 * 
 */

void enableRcMode(bool onOff)
{
    #ifdef TC_HAVETEMP
    if(haveRcMode) {
        rcMode = onOff;
        tempUpdInt = rcMode ? TEMP_UPD_INT_S : TEMP_UPD_INT_L;
    }
    #endif
}

bool toggleRcMode()
{
    #ifdef TC_HAVETEMP
    enableRcMode(!rcMode);
    return rcMode;
    #else
    return false;
    #endif
}

bool isRcMode()
{
    #ifdef TC_HAVETEMP
    return rcMode;
    #else
    return false;
    #endif
}

#ifdef TC_HAVETEMP
static void updateTemperature(bool force)
{
    if(!useTemp)
        return;
        
    if(force || (millis() - tempReadNow >= tempUpdInt)) {
        tempSens.readTemp(tempUnit);
        tempReadNow = millis();
    }
}
#endif

#ifdef TC_HAVESPEEDO
#ifdef TC_HAVEGPS
static void dispGPSSpeed(bool force)
{
    if(!useSpeedo || !useGPSSpeed)
        return;

    if(timeTravelP0 || timeTravelP1 || timeTravelRE || timeTravelP2)
        return;

    if(force || (millis() - dispGPSnow >= 500)) {

        speedo.setSpeed(myGPS.getSpeed());
        speedo.show();
        speedo.on();
        dispGPSnow = millis();

    }
}
#endif
#ifdef TC_HAVETEMP
static bool dispTemperature(bool force)
{
    bool tempNM = speedo.getNightMode();
    bool tempChgNM = false;

    if(!dispTemp)
        return false;

    if(!FPBUnitIsOn || startup || timeTravelP0 || timeTravelP1 || timeTravelRE || timeTravelP2)
        return false;

    tempChgNM = (tempNM != tempOldNM);
    tempOldNM = tempNM;

    if(tempChgNM || force || (millis() - tempDispNow >= 2*60*1000)) {
        if(tempNM && tempOffNM) {
            speedo.off();
        } else {
            speedo.setTemperature(tempSens.readLastTemp());
            speedo.show();
            if(!tempNM) {
                speedo.setBrightnessDirect(tempBrightness); // after show b/c brightness
            }
            speedo.on();
        }
        tempDispNow = millis();
    }

    return true;
}
#endif
#ifdef SP_ALWAYS_ON
static void dispIdleZero(bool force)
{
    if(!FPBUnitIsOn || startup || timeTravelP0 || timeTravelP1 || timeTravelRE || timeTravelP2)
        return;

    if(force || (millis() - dispIdlenow >= 500)) {

        speedo.setSpeed(0);
        speedo.show();
        speedo.on();
        dispIdlenow = millis();

    }
}
#endif
#endif

// Show all, month after a short delay
void animate()
{
    #ifdef TC_HAVETEMP
    if(isRcMode() && (!isWcMode() || !WcHaveTZ1)) {
            destinationTime.showTempDirect(tempSens.readLastTemp(), tempUnit, true);
    } else
    #endif
        destinationTime.showAnimate1();

    presentTime.showAnimate1();

    #ifdef TC_HAVETEMP
    if(isRcMode()) {
        if(isWcMode() && WcHaveTZ1) {
            departedTime.showTempDirect(tempSens.readLastTemp(), tempUnit, true);
        } else if(!isWcMode() && tempSens.haveHum()) {
            departedTime.showHumDirect(tempSens.readHum(), true);
        } else {
            departedTime.showAnimate1();
        }
    } else
    #endif
        departedTime.showAnimate1();
        
    mydelay(80);

    #ifdef TC_HAVETEMP
    if(isRcMode() && (!isWcMode() || !WcHaveTZ1)) {
        destinationTime.showTempDirect(tempSens.readLastTemp(), tempUnit);
    } else
    #endif
        destinationTime.showAnimate2();
        
    presentTime.showAnimate2();

    #ifdef TC_HAVETEMP
    if(isRcMode()) {
        if(isWcMode() && WcHaveTZ1) {
            departedTime.showTempDirect(tempSens.readLastTemp(), tempUnit);
        } else if(!isWcMode() && tempSens.haveHum()) {
            departedTime.showHumDirect(tempSens.readHum());
        } else {
            departedTime.showAnimate2();
        }
    } else
    #endif
        departedTime.showAnimate2();
}

// Activate lamp test on all displays and turn on
void allLampTest()
{
    destinationTime.on();
    presentTime.on();
    departedTime.on();
    destinationTime.lampTest();
    presentTime.lampTest();
    departedTime.lampTest();
}

void allOff()
{
    destinationTime.off();
    presentTime.off();
    departedTime.off();
}

/**************************************************************
 ***                                                        ***
 ***              NTP/GPS time synchronization              ***
 ***                                                        ***
 **************************************************************/

// choose your time zone from this list
// https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv

/* ATTN:
 *  DateTime uses 1-12 for months
 *  tm (_timeinfo) uses 0-11 for months
 *  Hardware RTC uses 1-12 for months
 *  Xprintf treats "%B" substition as from timeinfo, ie 0-11
 */

/*
 * Get time from NTP or GPS
 * and update given DateTime
 * 
 */
static bool getNTPOrGPSTime(bool weHaveAuthTime, DateTime& dt)
{
    // If GPS has a time and WiFi is off, try GPS first.
    // This avoids a frozen display when WiFi reconnects.
    #ifdef TC_HAVEGPS
    if(gpsHaveTime() && wifiIsOff) {
        if(getGPStime(dt)) return true;
    }
    #endif

    // Now try NTP
    if(getNTPTime(weHaveAuthTime, dt)) return true;

    // Again go for GPS, might have older timestamp
    #ifdef TC_HAVEGPS
    return getGPStime(dt);
    #else
    return false;
    #endif
}

/*
 * Get time from NTP
 * 
 * Saves time to RTC; does not save YOffs/isDST to NVM.
 * 
 * Does no re-tries in case NTPGetLocalTime() fails; this is
 * called repeatedly within a certain time window, so we can 
 * retry with a later call.
 */
static bool getNTPTime(bool weHaveAuthTime, DateTime& dt)
{
    uint16_t newYear;
    int16_t  newYOffs = 0;

    if(settings.ntpServer[0] == 0) {
        return false;
    }

    pwrNeedFullNow();

    // Reconnect for only 3 mins, but only if the user configured
    // a WiFi network to connect to; do not start CP.
    // If we don't have authTime yet, connect for longer to avoid
    // reconnects (aka frozen displays).
    // If WiFi is reconnected here, we won't have a valid time stamp
    // immediately. This will therefore fail the first time called.
    wifiOn(weHaveAuthTime ? 3*60*1000 : 21*60*1000, false, true);    

    if(WiFi.status() == WL_CONNECTED) {

        int nyear, nmonth, nday, nhour, nmin, nsecond, nisDST;
                
        if(NTPGetLocalTime(nyear, nmonth, nday, nhour, nmin, nsecond, nisDST)) {
            
            // Get RTC-fit year plus offs for given real year
            newYear = nyear;
            correctYr4RTC(newYear, newYOffs);
    
            rtc.adjust(nsecond,
                       nmin,
                       nhour,
                       dayOfWeek(nday, nmonth, nyear),
                       nday,
                       nmonth,
                       newYear - 2000);

            presentTime.setYearOffset(newYOffs);

            dt.set(newYear - 2000, nmonth, nday, 
                   nhour,          nmin,   nsecond);
    
            // Parse TZ and set up DST data for now current year
            if(tzHasDST[0] && (tzForYear[0] != nyear)) {
                if(!(parseTZ(0, nyear))) {
                    #ifdef TC_DBG
                    Serial.println(F("getNTPTime: Failed to parse TZ"));
                    #endif
                }
            }

            updateDSTFlag(nisDST);
    
            #ifdef TC_DBG
            Serial.printf("getNTPTime: New time %d-%02d-%02d %02d:%02d:%02d DST: %d\n", 
                      nyear, nmonth, nday, nhour, nmin, nsecond, nisDST);
            #endif
    
            return true;

        } else {

            #ifdef TC_DBG
            Serial.println(F("getNTPTime: No current NTP timestamp available"));
            #endif

            return false;
        }

    } else {

        #ifdef TC_DBG
        Serial.println(F("getNTPTime: WiFi not connected, NTP time sync skipped"));
        #endif
        
        return false;

    }
}

/*
 * Get time from GPS
 * 
 * Saves time to RTC; does not save YOffs/isDST to NVM.
 * 
 */
#ifdef TC_HAVEGPS
static bool getGPStime(DateTime& dt)
{
    struct tm timeinfo;
    uint64_t utcMins;
    unsigned long stampAge = 0;
    int nyear, nmonth, nday, nhour, nminute, nsecond, isDST = 0;
    uint16_t newYear;
    int16_t  newYOffs = 0;
   
    if(!useGPS)
        return false;

    if(!myGPS.getDateTime(&timeinfo, &stampAge, GPSupdateFreq))
        return false;

    #ifdef TC_DBG
    Serial.printf("getGPStime: stamp age %d\n", stampAge);
    #endif
            
    // Convert UTC to local (non-DST) time
    utcMins = dateToMins(timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday, 
                         timeinfo.tm_hour, timeinfo.tm_min);

    // Correct seconds by stampAge
    nsecond = timeinfo.tm_sec + (stampAge / 1000);
    if((stampAge % 1000) > 500) nsecond++;
    
    utcMins += (uint64_t)(nsecond / 60);
    nsecond %= 60;

    utcMins -= tzDiffGMT[0];

    minsToDate(utcMins, nyear, nmonth, nday, nhour, nminute);

    // DST handling: Parse TZ and setup DST data for now current year
    if(tzHasDST[0] && (tzForYear[0] != nyear)) {
        if(!(parseTZ(0, nyear))) {
            #ifdef TC_DBG
            Serial.println(F("getGPStime: Failed to parse TZ"));
            #endif
        }
    }

    // Determine DST from local nonDST time
    localToDST(0, nyear, nmonth, nday, nhour, nminute, isDST);

    // Get RTC-fit year & offs for given real year
    newYOffs = 0;
    newYear = nyear;
    correctYr4RTC(newYear, newYOffs);   

    rtc.adjust(nsecond,
               nminute,
               nhour,
               dayOfWeek(nday, nmonth, nyear),
               nday,
               nmonth,
               newYear - 2000);

    presentTime.setYearOffset(newYOffs);

    dt.set(newYear - 2000, nmonth,  nday, 
           nhour,          nminute, nsecond);

    // Parse TZ and set up DST data for now current year
    if(tzHasDST[0] && (tzForYear[0] != nyear)) {
        if(!(parseTZ(0, nyear))) {
            #ifdef TC_DBG
            Serial.println(F("getGPStime: Failed to parse TZ"));
            #endif
        }
    }

    // Update presentTime's DST flag
    updateDSTFlag(isDST);

    #ifdef TC_DBG
    Serial.printf("getGPStime: New time %d-%02d-%02d %02d:%02d:%02d DST: %d\n", 
                      nyear, nmonth, nday, nhour, nminute, nsecond, isDST);
    #endif
        
    return true;
}

/*
 * Set GPS's own RTC
 * This is only called once upon boot and
 * speeds up finding a fix significantly.
 */
static bool setGPStime()
{
    struct tm timeinfo;
    int nyear, nmonth, nday, nhour, nminute;
    uint64_t utcMins;
    DateTime dt;

    if(!useGPS && !useGPSSpeed)
        return false;

    #ifdef TC_DBG
    Serial.println(F("setGPStime() called"));
    #endif

    myrtcnow(dt);

    // Convert local time to UTC
    utcMins = dateToMins(dt.year() - presentTime.getYearOffset(), dt.month(), dt.day(), 
                         dt.hour(), dt.minute());

    if(couldDST[0] && presentTime.getDST() > 0)
        utcMins += tzDiffGMTDST[0];
    else
        utcMins += tzDiffGMT[0];
    
    minsToDate(utcMins, nyear, nmonth, nday, nhour, nminute);
    
    timeinfo.tm_year = nyear - 1900;
    timeinfo.tm_mon = nmonth - 1;
    timeinfo.tm_mday = nday;
    timeinfo.tm_hour = nhour;
    timeinfo.tm_min = nminute;
    timeinfo.tm_sec = dt.second();

    return myGPS.setDateTime(&timeinfo);
}

bool gpsHaveFix()
{
    if(!useGPS && !useGPSSpeed)
        return false;
        
    return myGPS.fix;
}

static bool gpsHaveTime()
{
    if(!useGPS && !useGPSSpeed)
        return false;
        
    return myGPS.haveTime();
}

/* Only for menu loop: 
 * Call GPS loop, but do not call
 * custom delay inside, ie no audio_loop()
 */
void gps_loop()
{
    if(!useGPS && !useGPSSpeed)
        return;

    if(millis() - lastLoopGPS > GPSupdateFreqMin) {
        lastLoopGPS = millis();
        myGPS.loop(false);
        #ifdef TC_HAVESPEEDO
        dispGPSSpeed(true);
        #endif
    }
}
#endif

/* 
 * Determine if provided year is a leap year 
 */
bool isLeapYear(int year)
{
    if((year & 3) == 0) { 
        if((year % 100) == 0) {
            if((year % 400) == 0) {
                return true;
            } else {
                return false;
            }
        } else {
            return true;
        }
    } else {
        return false;
    }
}

/* 
 * Find number of days in a month 
 */
int daysInMonth(int month, int year)
{
    if(month == 2 && isLeapYear(year)) {
        return 29;
    }
    return monthDays[month - 1];
}

/*
 *  Convert a date into "minutes since 1/1/0 0:0"
 */
uint64_t dateToMins(int year, int month, int day, int hour, int minute)
{
    uint64_t total64 = 0;
    uint32_t total32 = 0;
    int c = year, d = 0;        // ny0: d=1

    total32 = hours1kYears[year / 500];
    if(total32) d = (year / 500) * 500;

    while(c-- > d) {
        total32 += (isLeapYear(c) ? (8760+24) : 8760);
    }
    total32 += (mon_yday[isLeapYear(year) ? 1 : 0][month - 1] * 24);
    total32 += (day - 1) * 24;
    total32 += hour;
    total64 = (uint64_t)total32 * 60;
    total64 += minute;
    return total64;
}

/*
 *  Convert "minutes since 1/1/0 0:0" into date
 */
void minsToDate(uint64_t total64, int& year, int& month, int& day, int& hour, int& minute)
{
    int c = 0, d = 19;    // ny0: c=1
    int temp;
    uint32_t total32;

    year = 0;             // ny0: 1
    month = day = 1;
    hour = minute = 0;

    while(d >= 0) {
        if(total64 > mins1kYears[d]) break;
        d--;
    }
    if(d > 0) {
        total64 -= mins1kYears[d];
        c = year = d * 500;
    }

    total32 = total64;

    while(1) {
        temp = isLeapYear(c++) ? ((8760+24)*60) : (8760*60);
        if(total32 < temp) break;
        year++;
        total32 -= temp;
    }

    c = 1;
    temp = isLeapYear(year) ? 1 : 0;
    while(c < 12) {
        if(total32 < (mon_ydayt24t60[temp][c])) break;
        c++;
    }
    month = c;
    total32 -= (mon_ydayt24t60[temp][c-1]);

    temp = total32 / (24*60);
    day = temp + 1;
    total32 -= (temp * (24*60));

    temp = total32 / 60;
    hour = temp;

    minute = total32 - (temp * 60);
}

uint32_t getHrs1KYrs(int index)
{
    return hours1kYears[index*2];
}

/*
 * Return doW from given date (year=yyyy)
 */
uint8_t dayOfWeek(int d, int m, int y)
{
    // Sakamoto's method
    const int t[] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    if(y > 0) {
        if(m < 3) y -= 1;
        return (y + y/4 - y/100 + y/400 + t[m-1] + d) % 7;
    }
    return (mon_yday[1][m-1] + d + 5) % 7;
}

/*
 * Return RTC-fit year & offs for given real year
 */
void correctYr4RTC(uint16_t& year, int16_t& offs)
{
    offs = 0;
    if(year >= 2000 && year <= 2098) return;
    
    if(isLeapYear(year)) {
        offs = 2000 - year;
        year = 2000;
    } else {
        offs = 2001 - year;
        year = 2001;
    }
}


/**************************************************************
 ***                                                        ***
 ***               Timezone and DST handling                ***
 ***                                                        ***
 **************************************************************/

/*
 * Parse integer
 */
static char *parseInt(char *t, int& it)
{
    bool isNeg = false;
    it = 0;
    
    if(*t == '-') {
        t++;
        isNeg = true;
    } else if(*t == '+') {
        t++;
    }
    
    if(*t < '0' || *t > '9') return NULL;
    
    while(*t >= '0' && *t <= '9') {
        it *= 10;
        it += (*t++ - '0');
    }
    if(isNeg) it *= -1;

    return t;
}

/*
 * Parse DST parts of TZ string
 */
static char *parseDST(char *t, int& DSTyear, int& DSTmonth, int& DSTday, int& DSThour, int& DSTmin, int currYear)
{
    char *u;
    int it, tw, dow;

    DSTyear = currYear;
    DSTmin = 0;
    
    if(*t == 'M') {
        t++;
        u = parseInt(t, it);
        if(!u) return NULL;
        if(it >= 1 && it <= 12) DSTmonth = it;
        else                    return NULL;
            
        t = u;
        if(*t++ != '.') return NULL;
        
        u = parseInt(t, it);
        if(!u) return NULL;
        if(it < 1 || it > 5) return NULL;
        tw = it;
        
        t = u;        
        if(*t++ != '.') return NULL;
        
        u = parseInt(t, it);
        if(!u) return NULL;
        if(it < 0 || it > 6) return NULL;

        t = u;
        
        // it = weekday (0=Su), tw = week (1,2,3,4=nth week; 5=last)
        dow = dayOfWeek(1, DSTmonth, currYear);
        if(dow == 0) dow = 7;
        DSTday = (it+1) - dow;
        if(DSTday < 1) DSTday += 7;
        while(--tw) {
             DSTday += 7;
        }
        if(DSTday > daysInMonth(DSTmonth, currYear)) DSTday -= 7;

    } else if(*t == 'J') {

        t++;
        u = parseInt(t, it);
        if(!u) return NULL;

        if(it < 1 || it > 365) return NULL;
        
        t = u;

        DSTmonth = 0;
        while(it > monthDays[DSTmonth]) {
            it -= monthDays[DSTmonth++];
        }
        DSTmonth++;
        DSTday = it;
      
    } else if(*t >= '0' && *t <= '9') {

        u = parseInt(t, it);
        if(!u) return NULL;

        if(it < 0 || it > 365) return NULL;
        if((it > 364) && (!isLeapYear(currYear))) return NULL;
        
        t = u;

        it++;
        DSTmonth = 1;
        while(it > daysInMonth(DSTmonth, currYear)) {
            it -= daysInMonth(DSTmonth, currYear);
            DSTmonth++;
        }
        DSTday = it;
      
    } else return NULL;

    if(*t == '/') {
        t++;
        u = parseInt(t, it);
        if(!u) return NULL;
        
        t = u;
        if(it >= -167 && it <= 167) DSThour = it;
        else return NULL;
        
        if(*t == ':') {
            t++;
            u = parseInt(t, it);
            if(!u) return NULL;
            
            t = u;
            if(it >= 0 && it <= 59) DSTmin = it;
            else return NULL;
            
            if(*t == ':') {
                t++;
                u = parseInt(t, it);
                if(!u) return NULL;
                t = u;
            }
        }
    } else {
        DSThour = 2;    
    }

    if(DSThour > 23) {
        while(DSThour > 23) {
            DSTday++;
            DSThour -= 24;
        }
        while(DSTday > daysInMonth(DSTmonth, DSTyear)) {
            DSTday -= daysInMonth(DSTmonth, DSTyear);
            DSTmonth++;
            if(DSTmonth > 12) {
                DSTyear++;
                DSTmonth = 1;
            }
        }
    } else if(DSThour < 0) {
        while(DSThour < 0) {
            DSTday--;
            DSThour += 24;
        }
        while(DSTday < 1) {
            DSTmonth--;
            if(DSTmonth < 1) {
                DSTyear--;
                DSTmonth = 12;
            }
            DSTday += daysInMonth(DSTmonth, DSTyear);
        }
    }
    
    return t;
}

/*
 * Convert date into minutes since 1/1 00:00 of given year
 */
int mins2Date(int year, int month, int day, int hour, int mins)
{
    return ((((mon_yday[isLeapYear(year) ? 1 : 0][month - 1] + (day - 1)) * 24) + hour) * 60) + mins;
}

/*
 * Parse TZ string and setup DST data
 * 
 * If TZ-part is bad, always returns FALSE
 * If DST-part is bad, only returns FALSE once
 * (DST-part ignored if bad in later calls)
 */
bool parseTZ(int index, int currYear, bool doparseDST)
{
    char *tz, *t, *u, *v;
    int diffNorm = 0;
    int diffDST = 0;
    int it;
    int DSTonYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute;
    int DSToffYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute;

    switch(index) {
    case 0: tz = settings.timeZone;     break;
    case 1: tz = settings.timeZoneDest; break;
    case 2: tz = settings.timeZoneDep;  break;
    default:
      return false;
    }

    couldDST[index] = false;
    tzForYear[index] = 0;
    if(!tzDSTpart[index]) {
        tzDiffGMT[index] = tzDiffGMTDST[index] = 0;
    }

    // 0) Basic validity check

    if(*tz == 0) {                                    // Empty string. OK, don't use TZ. So be it.
        tzHasDST[index] = 0;
        return true;
    }

    if(tzIsValid[index] < 1) {

        // If previously determined to be invalid, bail.
        if(!tzIsValid[index]) return false;

        // Set TZ to "invalid" until verified
        tzIsValid[index] = 0;

        t = tz;
        while((t = strchr(t, '>'))) { t++; diffNorm++; }
        t = tz;
        while((t = strchr(t, '<'))) { t++; diffDST++; }
        if(diffNorm != diffDST) return false;         // Uneven < and >, string is bad.
    }

    // 1) Find difference between nonDST and DST time

    if(!tzDSTpart[index]) {

        diffNorm = diffDST = 0;

        // a. Skip TZ name and parse GMT-diff
        t = tz;
        if(*t == '<') {
           t = strchr(t, '>');
           if(!t) return false;                       // if <, but no >, string is bad. Bad TZ.
           t++;
        } else {
           while(*t && *t != '-' && (*t < '0' || *t > '9')) {
              if(*t == ',') return false;
              t++;
           }
        }
        
        // t = start of diff to GMT
        if(*t != '-' && *t != '+' && (*t < '0' || *t > '9'))
            return false;                             // No numerical difference after name -> bad string. Bad TZ.
    
        t = parseInt(t, it);
        if(it >= -24 && it <= 24) diffNorm = it * 60;
        else                      return false;       // Bad hr difference. No DST.
        
        if(*t == ':') {
            t++;
            u = parseInt(t, it);
            if(!u) return false;                      // No number following ":". Bad string. Bad TZ.
            t = u;
            if(it >= 0 && it <= 59) {
                if(diffNorm < 0)  diffNorm -= it;
                else              diffNorm += it;
            } else return false;                      // Bad min difference. Bad TZ.
            if(*t == ':') {
                t++;
                u = parseInt(t, it);
                if(u) t = u;
                // Ignore seconds
            }
        }
        
        // b. Skip DST TZ name and parse GMT-diff
        
        if(*t == '<') {
           t = strchr(t, '>');
           if(!t) return false;                       // if <, but no >, string is bad. Bad TZ.
           t++;
        } else {
           while(*t && *t != ',' && *t != '-' && (*t < '0' || *t > '9'))
              t++;
        }
        
        // t = assumed start of DST-diff to GMT
        if(*t == 0) {
            tzDiff[index] = 0;
        } else if(*t != '-' && *t != '+' && (*t < '0' || *t > '9')) {
            tzDiff[index] = 60;                       // No numerical difference after name -> Assume 1 hr
        } else {
            t = parseInt(t, it);
            if(it >= -24 && it <= 24) diffDST = it * 60;
            else                      return false;   // Bad hr difference. Bad TZ.
            if(*t == ':') {
                t++;
                u = parseInt(t, it);
                if(!u) return false;                  // No number following ":". Bad TZ.
                t = u;
                if(it >= 0 && it <= 59) {
                    if(diffNorm < 0)  diffDST -= it;
                    else              diffDST += it;
                } else return false;                  // Bad min difference. Bad TZ.
                if(*t == ':') {
                    t++;
                    u = parseInt(t, it);
                    if(u) t = u;
                    // Ignore seconds
                }
            }
            tzDiff[index] = abs(diffDST - diffNorm);
        }
    
        tzDiffGMT[index] = diffNorm;
        tzDiffGMTDST[index] = tzDiffGMT[index] - tzDiff[index];
    
        tzDSTpart[index] = t;

        tzIsValid[index] = 1;   // TZ is valid

    } else {

        t = tzDSTpart[index];
      
    }

    if(!tzHasDST[index] || !doparseDST) {
        return true;
    }
    
    if(*t == 0 || *t != ',') {                        // No DST definition. No DST.
        tzHasDST[index] = 0;
        return true;
    }

    t++;

    tzForYear[index] = currYear;

    // Set to "no DST" until verified valid
    tzHasDST[index] = 0;

    // 2) parse DST start

    v = t;
    u = parseDST(t, DSTonYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute, currYear);
    if(!u) return false;

    // If start crosses end year (due to hour numbers >= 24), need to calculate 
    // for previous year (which then might be in current year). 
    // The same goes for the other direction vice versa.
    if(DSTonYear > currYear) {
        u = parseDST(v, DSTonYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute, currYear-1);
        if(!u) return false;
    } else if(DSTonYear < currYear) {
        u = parseDST(v, DSTonYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute, currYear+1);
        if(!u) return false;
    }
    
    t = u;

    if(*t == 0 || *t != ',') return false;          // Have start, but no end. Bad string. No DST.
    t++;

    // 3) parse DST end

    v = t;
    u = parseDST(t, DSToffYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute, currYear);
    if(!u) return false;

    // See above
    if(DSToffYear > currYear) {
        u = parseDST(v, DSToffYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute, currYear-1);
        if(!u) return false;
    } else if(DSToffYear < currYear) {
        u = parseDST(v, DSToffYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute, currYear+1);
        if(!u) return false;
    }

    // 4) Evaluate results

    tzHasDST[index] = 1;  // TZ has valid DST definition

    if((DSToffMonth == DSTonMonth) && (DSToffDay == DSTonDay)) {
        couldDST[index] = false;
        #ifdef TC_DBG
        Serial.printf("parseTZ: (%d) DST not used\n", index);
        #endif
    } else {
        couldDST[index] = true;

        // If start or end still beyond our current year, set to impossible values
        // to allow a valid comparison.
        // Despire our cross-end-check for currYear above, this still can happen!
        // Eg: "CRAZY-3:30<C3ACY>4:56,M1.1.0/-48,M12.5.0/48"
        // For 2023, first calculated start is on 12/30/2022, so we do 2024 above, 
        // but for 2024 start is on 1/5/2024. Nothing in 2023!
        // Likewise 2023's first calculated end is on 1/2/2024, so we do 2022 above,
        // but for 2022, it is on 12/27/2022. Again, outside of our current year!
        // So with this somewhat challenging time zone definition, the entire year 
        // 2023 is DST. Need to set -1/600000 to make comparison right.
        if(DSTonYear < currYear)
            DSTonMins[index] = -1;
        else if(DSTonYear > currYear)
            DSTonMins[index] = 600000;
        else 
            DSTonMins[index] = mins2Date(currYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute);
    
        if(DSToffYear < currYear)
            DSToffMins[index] = -1;
        else if(DSToffYear > currYear)
            DSToffMins[index] = 600000;
        else 
            DSToffMins[index] = mins2Date(currYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute);

        #ifdef TC_DBG
        Serial.printf("parseTZ: (%d) DST dates/times: %d/%d Start: %d-%02d-%02d/%02d:%02d End: %d-%02d-%02d/%02d:%02d\n",
                    index,
                    tzDiffGMT[index], tzDiffGMTDST[index], 
                    DSTonYear, DSTonMonth, DSTonDay, DSTonHour, DSTonMinute,
                    DSToffYear, DSToffMonth, DSToffDay, DSToffHour, DSToffMinute);
        #endif

    }
        
    return true;
}

int getTzDiff()
{
  if(couldDST[0]) return tzDiff[0];
  return 0;
}

/*
 * Check if given local date/time is within DST period.
 * "Local" can be non-DST or DST; therefore a change possibly
 * needs to be blocked, see below.
 */
int timeIsDST(int index, int year, int month, int day, int hour, int mins, int& currTimeMins)
{
    currTimeMins = mins2Date(year, month, day, hour, mins);

    if(DSTonMins[index] < DSToffMins[index]) {
        if((currTimeMins >= DSTonMins[index]) && (currTimeMins < DSToffMins[index]))
            return 1;
        else 
            return 0;
    } else {
        if((currTimeMins >= DSToffMins[index]) && (currTimeMins < DSTonMins[index]))
            return 0;
        else
            return 1;
    }
}

/*
 * Check if given currminutes fall in period where DST determination needs 
 * to be blocked because it would yield a wrong result. The elephant in the 
 * room here is the 'time-loop-hour' after switching to nonDST time, ie the 
 * period (where we already are on nonDST time) within tzDiff minutes ahead 
 * of a (would-be-repeated) change to nonDST time.
 * Block if getDST <= 0 because
 * - if 0, block because it has already been set, so block a reset to DST;
 * - if -1, block because determination not possible at this point.
 */
static bool blockDSTChange(int currTimeMins)
{
    return (
             (presentTime.getDST() <= 0) &&
             (currTimeMins < DSToffMins[0]) &&
             (DSToffMins[0] - currTimeMins <= tzDiff[0])
           );
}

/*
 * Determine DST from non-DST local time. This is used with NTP, GPS and
 * the WC times, where we get UTC time and have converted it to local
 * nonDST time. There is no need for blocking as there is no ambiguity.
 */
static void localToDST(int index, int& year, int& month, int& day, int& hour, int& minute, int& isDST)
{
    if(couldDST[index]) {
      
        // Get mins since 1/1 of current year in local (nonDST) time
        int currTimeMins = mins2Date(year, month, day, hour, minute);

        // Determine DST
        // DSTonMins is in non-DST time
        // DSToffMins is in DST time, so correct it for the comparison
        if(DSTonMins[index] < DSToffMins[index]) {
            if((currTimeMins >= DSTonMins[index]) && (currTimeMins < (DSToffMins[index] - tzDiff[index])))
                isDST = 1;
            else 
                isDST = 0;
        } else {
            if((currTimeMins >= (DSToffMins[index] - tzDiff[index])) && (currTimeMins < DSTonMins[index]))
                isDST = 0;
            else
                isDST = 1;
        }

        // Translate to DST local time
        if(isDST) {
            uint64_t myTime = dateToMins(year, month, day, hour, minute);

            myTime += tzDiff[index];

            minsToDate(myTime, year, month, day, hour, minute);
        }
    }
}

/*
 * Update presentTime's isDST flag
 */
static void updateDSTFlag(int nisDST)
{
    #ifdef TC_DBG
    if(presentTime.getDST() != nisDST) {
        Serial.printf("updateDSTFlag: Updating isDST from %d to %d\n", 
              presentTime.getDST(), nisDST);
    }
    #endif
    presentTime.setDST(nisDST);
}

/*
 * World Clock
 * Convert local time to other time zone, and copy
 * them to display. If a TW for a display is not
 * configured, it does not touch that display.
 */
void setDatesTimesWC(DateTime& dt)
{
    uint64_t myTimeL; 
    int year = dt.year() - presentTime.getYearOffset();
    int month = dt.month();
    int day = dt.day();
    int hour = dt.hour();
    int minute = dt.minute();
    int isDST;

    uint64_t myTime = dateToMins(year, month, day, hour, minute);

    // Convert local time to UTC
    if(presentTime.getDST() > 0)
        myTime -= tzDiff[0];

    myTime += tzDiffGMT[0];

    // Convert UTC to new TZ
    if(WcHaveTZ1) {
        myTimeL = myTime - tzDiffGMT[1];
        minsToDate(myTimeL, year, month, day, hour, minute);

        if(tzHasDST[1] && tzForYear[1] != year) {
            parseTZ(1, year);
        }       

        localToDST(1, year, month, day, hour, minute, isDST);

        destinationTime.setFromParms(year, month, day, hour, minute);
    }

    if(WcHaveTZ2) {
        myTimeL = myTime - tzDiffGMT[2];
        minsToDate(myTimeL, year, month, day, hour, minute);

        if(tzHasDST[2] && tzForYear[2] != year) {
            parseTZ(2, year);
        }       

        localToDST(2, year, month, day, hour, minute, isDST);

        departedTime.setFromParms(year, month, day, hour, minute);
    }
}

/**************************************************************
 ***                                                        ***
 ***                       Native NTP                       ***
 ***                                                        ***
 **************************************************************/

static void ntp_setup()
{
    myUDP = &ntpUDP;
    myUDP->begin(NTP_DEFAULT_LOCAL_PORT);
    NTPfailCount = 0;
}

void ntp_loop()
{
    if(NTPPacketDue) {
        NTPCheckPacket();
    }
    if(!NTPPacketDue) {
        // If WiFi status changed, trigger immediately
        if(!NTPWiFiUp && (WiFi.status() == WL_CONNECTED)) {
            NTPUpdateNow = 0;
        }
        if((!NTPUpdateNow) || (millis() - NTPUpdateNow > 60000)) {
            NTPTriggerUpdate();
        }
    }
}

// Short version for inside delay loops
// Fetches pending packet, if available
void ntp_short_loop()
{
    if(NTPPacketDue) {
        NTPCheckPacket();
    }
}

// Send a new NTP request
static bool NTPTriggerUpdate()
{
    NTPPacketDue = false;

    NTPUpdateNow = millis();

    if(WiFi.status() != WL_CONNECTED) {
        NTPWiFiUp = false;
        return false;
    }

    NTPWiFiUp = true;

    if(settings.ntpServer[0] == 0) {
        return false;
    }

    // Flush old packets received after timeout
    while(myUDP->parsePacket() != 0) {
        myUDP->flush();
    }

    // Send new packet
    NTPSendPacket();
    NTPTSRQAge = millis();
    
    NTPPacketDue = true;
    
    return true;
}

static void NTPSendPacket()
{
    unsigned long mymillis = millis();
    
    memset(NTPUDPBuf, 0, NTP_PACKET_SIZE);

    NTPUDPBuf[0] = B11100011; // LI, Version, Mode
    NTPUDPBuf[1] = 0;         // Stratum
    NTPUDPBuf[2] = 6;         // Polling Interval
    NTPUDPBuf[3] = 0xEC;      // Peer Clock Precision

    NTPUDPBuf[12] = 'T';      // Ref ID, anything
    NTPUDPBuf[13] = 'C';
    NTPUDPBuf[14] = 'D';
    NTPUDPBuf[15] = '1';

    // Transmit, use as id
    NTPUDPBuf[40] = NTPUDPID[0] = ((mymillis >> 24) & 0xff);      
    NTPUDPBuf[41] = NTPUDPID[1] = ((mymillis >> 16) & 0xff);
    NTPUDPBuf[42] = NTPUDPID[2] = ((mymillis >>  8) & 0xff);
    NTPUDPBuf[43] = NTPUDPID[3] = (mymillis         & 0xff);

    myUDP->beginPacket(settings.ntpServer, 123);
    myUDP->write(NTPUDPBuf, NTP_PACKET_SIZE);
    myUDP->endPacket();
}

// Check for pending packet and parse it
static void NTPCheckPacket()
{
    unsigned long mymillis = millis();
    
    int psize = myUDP->parsePacket();
    if(!psize) {
        if((mymillis - NTPTSRQAge) > 10000) {
            // Packet timed out
            NTPPacketDue = false;
            // Immediately trigger new request for
            // the first 10 timeouts, after that
            // the new request is only triggered
            // a minute later as per ntp_loop().
            if(NTPfailCount < 10) {
                NTPfailCount++;
                NTPUpdateNow = 0;
            }
        }
        return;
    }

    NTPfailCount = 0;
    
    myUDP->read(NTPUDPBuf, NTP_PACKET_SIZE);

    // Basic validity check
    if((NTPUDPBuf[0] & 0x3f) != 0x24) return;
    if(NTPUDPBuf[24] != NTPUDPID[0] || 
       NTPUDPBuf[25] != NTPUDPID[1] || 
       NTPUDPBuf[26] != NTPUDPID[2] || 
       NTPUDPBuf[27] != NTPUDPID[3]) {
        #ifdef TC_DBG
        Serial.println("NTPCheckPacket: Bad packet ID (outdated packet?)");
        #endif
        return;
    }

    // If it's our expected packet, no other is due for now
    NTPPacketDue = false;

    // Baseline for round-trip correction
    NTPTSAge = mymillis - ((mymillis - NTPTSRQAge) / 2);

    // Evaluate data
    uint64_t secsSince1900 = ((uint32_t)NTPUDPBuf[40] << 24) |
                             ((uint32_t)NTPUDPBuf[41] << 16) |
                             ((uint32_t)NTPUDPBuf[42] <<  8) |
                             ((uint32_t)NTPUDPBuf[43]);

    uint32_t fractSec = ((uint32_t)NTPUDPBuf[44] << 24) |
                        ((uint32_t)NTPUDPBuf[45] << 16) |
                        ((uint32_t)NTPUDPBuf[46] <<  8) |
                        ((uint32_t)NTPUDPBuf[47]);
                        
    // Correct era
    if(secsSince1900 < (SECS1900_1970 + TCEPOCH_SECS)) {
        secsSince1900 |= 0x100000000ULL;
    }           

    // Calculate seconds since 1/1/TCEPOCH (without round-trip correction)
    NTPsecsSinceTCepoch = secsSince1900 - (SECS1900_1970 + TCEPOCH_SECS);

    // Convert fraction into ms
    NTPmsSinceSecond = (uint32_t)(((uint64_t)fractSec * 1000ULL) >> 32);
}

static bool NTPHaveTime()
{
    return NTPsecsSinceTCepoch ? true : false;
}

// Get seconds since 1/1/TCEPOCH including round-trip correction
static uint32_t NTPGetCurrSecsSinceTCepoch()
{
    return NTPsecsSinceTCepoch + ((NTPmsSinceSecond + (millis() - NTPTSAge)) / 1000UL);
}

// Get local time
static bool NTPGetLocalTime(int& year, int& month, int& day, int& hour, int& minute, int& second, int& isDST)
{
    uint32_t temp, c;

    // Fail if no time received, or stamp is older than 10 mins
    if(!NTPHaveLocalTime()) return false;

    isDST = 0;
    
    uint32_t secsSinceTCepoch = NTPGetCurrSecsSinceTCepoch();
    
    second = secsSinceTCepoch % 60;

    // Calculate minutes since 1/1/TCEpoch for local (nonDST) time zone
    uint32_t total32 = (secsSinceTCepoch / 60) - tzDiffGMT[0];

    // Calculate current date
    year = c = TCEPOCH;
    while(1) {
        temp = isLeapYear(c++) ? ((8760+24)*60) : (8760*60);
        if(total32 < temp) break;
        year++;
        total32 -= temp;
    }

    c = 1;
    temp = isLeapYear(year) ? 1 : 0;
    while(c < 12) {
        if(total32 < (mon_ydayt24t60[temp][c])) break;
        c++;
    }
    month = c;
    total32 -= (mon_ydayt24t60[temp][c-1]);

    temp = total32 / (24*60);
    day = temp + 1;
    total32 -= (temp * (24*60));

    temp = total32 / 60;
    hour = temp;

    minute = total32 - (temp * 60);

    // DST handling: Parse TZ and setup DST data for now current year
    if(tzHasDST[0] && tzForYear[0] != year) {
        if(!(parseTZ(0, year))) {
            #ifdef TC_DBG
            Serial.println(F("NTPGetLocalTime: Failed to parse TZ"));
            #endif
        }
    }

    // Determine DST from local nonDST time
    localToDST(0, year, month, day, hour, minute, isDST);

    return true;
}

static bool NTPHaveLocalTime()
{
    // Have no time if no time stamp received, or stamp is older than 10 mins
    if((!NTPsecsSinceTCepoch) || ((millis() - NTPTSAge) > 10*60*1000)) 
        return false;

    return true;
}
