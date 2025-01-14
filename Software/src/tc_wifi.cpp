/*
 * -------------------------------------------------------------------
 * CircuitSetup.us Time Circuits Display
 * (C) 2021-2022 John deGlavina https://circuitsetup.us
 * (C) 2022-2023 Thomas Winischhofer (A10001986)
 * https://github.com/realA10001986/Time-Circuits-Display-A10001986
 *
 * WiFi and Config Portal handling
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
#include <ArduinoJson.h>

#ifdef TC_MDNS
#include <ESPmDNS.h>
#endif

#include <WiFiManager.h>

#include "clockdisplay.h"
#include "tc_menus.h"
#include "tc_time.h"
#include "tc_audio.h"
#include "tc_settings.h"
#include "tc_wifi.h"
#ifdef TC_HAVEMQTT
#include "mqtt.h"
#include "tc_keypad.h"
#endif

// If undefined, use the checkbox/dropdown-hacks.
// If defined, go back to standard text boxes
//#define TC_NOCHECKBOXES

Settings settings;

IPSettings ipsettings;

WiFiManager wm;

#ifdef TC_HAVEMQTT
WiFiClient mqttWClient;
PubSubClient mqttClient(mqttWClient);
#endif

static char beepaintCustHTML[820] = "";
static const char beepCustHTML1[] = "<div class='cmp0'><label for='beepmode'>Default beep mode</label><select class='sel0' value='";
static const char beepCustHTML2[] = "' name='beepmode' id='beepmode' autocomplete='off' title='Select power-up beep mode'><option value='0'";
static const char beepCustHTML3[] = ">Off</option><option value='1'";
static const char beepCustHTML4[] = ">On</option><option value='2'";
static const char beepCustHTML5[] = ">Auto (30 secs)</option><option value='3'";
static const char beepCustHTML6[] = ">Auto (60 secs)</option></select></div>";
static const char aintCustHTML1[] = "<div class='cmp0'><label for='rotate_times'>Time-cycling interval</label><select class='sel0' value='";
static const char aintCustHTML2[] = "' name='rotate_times' id='rotate_times' autocomplete='off'><option value='0'";
static const char aintCustHTML3[] = ">Off</option><option value='1'";
static const char aintCustHTML4[] = ">Every 5th minute</option><option value='2'";
static const char aintCustHTML5[] = ">Every 10th minute</option><option value='3'";
static const char aintCustHTML6[] = ">Every 15th minute</option><option value='4'";
static const char aintCustHTML7[] = ">Every 30th minute</option><option value='5'";
static const char aintCustHTML8[] = ">Every 60th minute</option></select></div>";

static char anmCustHTML[768] = "";
static const char anmCustHTML1[] = "<div class='cmp0'><label for='autonmtimes'>Schedule</label><select class='sel0' value='";
static const char anmCustHTML2[] = "' name='autonmtimes' id='autonmtimes' autocomplete='off'><option value='10'";
static const char anmCustHTML3[] = ">&#10060; Off</option><option value='0'";
static const char anmCustHTML4[] = ">&#128337; Daily, set hours below</option><option value='1'";
static const char anmCustHTML5[] = ">&#127968; M-T:17-23/F:13-1/S:9-1/Su:9-23</option><option value='2'";
static const char anmCustHTML6[] = ">&#127970; M-F:9-17</option><option value='3'";
static const char anmCustHTML7[] = ">&#127970; M-T:7-17/F:7-14</option><option value='4'";
static const char anmCustHTML8[] = ">&#128722; M-W:8-20/T-F:8-21/S:8-17</option></select></div>";

#ifdef TC_HAVESPEEDO
static char spTyCustHTML[1024] = "";
static const char spTyCustHTML1[] = "<div class='cmp0'><label for='speedo_type'>Speedo display type</label><select class='sel0' value='";
static const char spTyCustHTML2[] = "' name='speedo_type' id='speedo_type' autocomplete='off'>";
static const char spTyCustHTMLE[] = "</select></div>";
static const char spTyOptP1[] = "<option value='";
static const char spTyOptP2[] = "'>";
static const char spTyOptP3[] = "</option>";
static const char *dispTypeNames[SP_NUM_TYPES] = {
  "CircuitSetup.us\0",
  "Adafruit 878 (4x7)\0",
  "Adafruit 878 (4x7;left)\0",
  "Adafruit 1270 (4x7)\0",
  "Adafruit 1270 (4x7;left)\0",
  "Adafruit 1911 (4x14)\0",
  "Adafruit 1911 (4x14;left)\0",
  "Grove 0.54\" 2x14\0",
  "Grove 0.54\" 4x14\0",
  "Grove 0.54\" 4x14 (left)\0"
#ifndef TWPRIVATE
  ,"Ada 1911 (left tube)\0"
  ,"Ada 878 (left tube)\0"
#else
  ,"A10001986 wallclock\0"
  ,"A10001986 speedo replica\0"
#endif
};
#endif

static const char *aco = "autocomplete='off'";
static const char *tznp1 = "City/location name [a-z/0-9/-/ ]";

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_ttrp("ttrp", "Make time travels persistent (0=no, 1=yes)", settings.timesPers, 1, "autocomplete='off' title='If disabled, the displays are reset after reboot'");
WiFiManagerParameter custom_alarmRTC("artc", "Alarm base is RTC (1) or displayed \"present\" time (0)", settings.alarmRTC, 1, aco);
WiFiManagerParameter custom_playIntro("plIn", "Play intro (0=off, 1=on)", settings.playIntro, 1, aco);
WiFiManagerParameter custom_mode24("md24", "24-hour clock mode: (0=12hr, 1=24hr)", settings.mode24, 1, aco);
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_ttrp("ttrp", "Make time travels persistent", settings.timesPers, 1, "title='If unchecked, the displays are reset after reboot' type='checkbox' style='margin-top:3px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_alarmRTC("artc", "Alarm base is real present time", settings.alarmRTC, 1, "title='If unchecked, the alarm base is the displayed \"present\" time' type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_playIntro("plIn", "Play intro", settings.playIntro, 1, "type='checkbox'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_mode24("md24", "24-hour clock mode", settings.mode24, 1, "type='checkbox'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_beep_aint(beepaintCustHTML);  // beep + aint

#if defined(TC_MDNS) || defined(TC_WM_HAS_MDNS)
#define HNTEXT "Hostname<br><span style='font-size:80%'>The Config Portal is accessible at http://<i>hostname</i>.local<br>(Valid characters: a-z/0-9/-)</span>"
#else
#define HNTEXT "Hostname<br><span style='font-size:80%'>(Valid characters: a-z/0-9/-)</span>"
#endif
WiFiManagerParameter custom_hostName("hostname", HNTEXT, settings.hostName, 31, "pattern='[A-Za-z0-9-]+' placeholder='Example: timecircuits'");
WiFiManagerParameter custom_wifiConRetries("wifiret", "WiFi connection attempts (1-15)", settings.wifiConRetries, 2, "type='number' min='1' max='15' autocomplete='off'", WFM_LABEL_BEFORE);
WiFiManagerParameter custom_wifiConTimeout("wificon", "WiFi connection timeout (7-25[seconds])", settings.wifiConTimeout, 2, "type='number' min='7' max='25'");
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_wifiPRe("wifiPRet", "Periodic reconnection attempts (0=no, 1=yes)", settings.wifiPRetry, 1, "autocomplete='off' title='Enable to periodically retry WiFi connection after failure'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_wifiPRe("wifiPRet", "Periodic reconnection attempts ", settings.wifiPRetry, 1, "autocomplete='off' title='Check to periodically retry WiFi connection after failure' type='checkbox' style='margin:5px 0 10px 0'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_wifiOffDelay("wifioff", "<br>WiFi power save timer<br>(10-99[minutes];0=off)", settings.wifiOffDelay, 2, "type='number' min='0' max='99' title='If in station mode, WiFi will be shut down after chosen number of minutes after power-on. 0 means never.'");
WiFiManagerParameter custom_wifiAPOffDelay("wifiAPoff", "WiFi power save timer (AP-mode)<br>(10-99[minutes];0=off)", settings.wifiAPOffDelay, 2, "type='number' min='0' max='99' title='If in AP mode, WiFi will be shut down after chosen number of minutes after power-on. 0 means never.'");

WiFiManagerParameter custom_timeZone("time_zone", "Time zone (in <a href='https://github.com/nayarsystems/posix_tz_db/blob/master/zones.csv' target=_blank>Posix</a> format)", settings.timeZone, 63, "placeholder='Example: CST6CDT,M3.2.0,M11.1.0'");
WiFiManagerParameter custom_ntpServer("ntp_server", "NTP Server (empty to disable NTP)", settings.ntpServer, 63, "pattern='[a-zA-Z0-9.-]+' placeholder='Example: pool.ntp.org'");

WiFiManagerParameter custom_timeZone1("time_zone1", "Time zone for Destination Time display", settings.timeZoneDest, 63, "placeholder='Example: CST6CDT,M3.2.0,M11.1.0'");
WiFiManagerParameter custom_timeZone2("time_zone2", "Time zone for Last Time Dep. display", settings.timeZoneDep, 63, "placeholder='Example: CST6CDT,M3.2.0,M11.1.0'");
WiFiManagerParameter custom_timeZoneN1("time_zonen1", tznp1, settings.timeZoneNDest, DISP_LEN, "pattern='[a-zA-Z0-9- ]+' placeholder='Optional. Example: CHICAGO' style='margin-bottom:15px'");
WiFiManagerParameter custom_timeZoneN2("time_zonen2", tznp1, settings.timeZoneNDep, DISP_LEN, "pattern='[a-zA-Z0-9- ]+' placeholder='Optional. Example: CHICAGO'");

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_dtNmOff("dTnMOff", "Destination time (0=dimmed, 1=off)", settings.dtNmOff, 1, aco);
WiFiManagerParameter custom_ptNmOff("pTnMOff", "Present time (0=dimmed, 1=off)", settings.ptNmOff, 1, aco);
WiFiManagerParameter custom_ltNmOff("lTnMOff", "Last time dep. (0=dimmed, 1=off)", settings.ltNmOff, 1, aco);
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_dtNmOff("dTnMOff", "Destination time off", settings.dtNmOff, 1, "title='If unchecked, the display will be dimmed' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_ptNmOff("pTnMOff", "Present time off", settings.ptNmOff, 1, "title='If unchecked, the display will be dimmed' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
WiFiManagerParameter custom_ltNmOff("lTnMOff", "Last time dep. off", settings.ltNmOff, 1, "title='If unchecked, the display will be dimmed' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_autoNMTimes(anmCustHTML);
WiFiManagerParameter custom_autoNMOn("anmon", "Daily night-mode start hour (0-23)", settings.autoNMOn, 2, "type='number' min='0' max='23' title='Enter hour to switch on night-mode'");
WiFiManagerParameter custom_autoNMOff("anmoff", "Daily night-mode end hour (0-23)", settings.autoNMOff, 2, "type='number' min='0' max='23' autocomplete='off' title='Enter hour to switch off night-mode'");
#ifdef TC_HAVELIGHT
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_uLS("uLS", "Use light sensor (0=no, 1=yes)", settings.useLight, 1,  "title='If enabled, device will go into night mode if lux level is below or equal the threshold.' autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_uLS("uLS", "Use light sensor", settings.useLight, 1, "title='If checked, device will go into night mode if lux level is below or equal the threshold.' type='checkbox' style='margin-top:14px'", WFM_LABEL_AFTER);
#endif
WiFiManagerParameter custom_lxLim("lxLim", "<br>Lux threshold (0-50000)", settings.luxLimit, 6, "title='If the lux level is below or equal the threshold, the device will go into night-mode' type='number' min='0' max='50000' autocomplete='off'", WFM_LABEL_BEFORE);
#endif

#ifdef TC_HAVETEMP
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_tempUnit("uTem", "Temperature unit (0=°F, 1=°C)", settings.tempUnit, 1, "autocomplete='off' title='Select unit for temperature'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_tempUnit("temUnt", "Show temperature in °Celsius", settings.tempUnit, 1, "title='If unchecked, temperature is displayed in Fahrenheit' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_tempOffs("tOffs", "<br>Temperature offset (-3.0-3.0)", settings.tempOffs, 4, "type='number' min='-3.0' max='3.0' step='0.1' title='Correction value to add to temperature' autocomplete='off'");
#endif // TC_HAVETEMP

#ifdef TC_HAVESPEEDO
WiFiManagerParameter custom_speedoType(spTyCustHTML);
WiFiManagerParameter custom_speedoBright("speBri", "<br>Speedo brightness (0-15)", settings.speedoBright, 2, "type='number' min='0' max='15' autocomplete='off'");
WiFiManagerParameter custom_speedoFact("speFac", "Speedo sequence speed factor (0.5-5.0)", settings.speedoFact, 3, "type='number' min='0.5' max='5.0' step='0.5' title='1.0 means the sequence is played in real-world DMC-12 acceleration time.' autocomplete='off'");
#ifdef TC_HAVEGPS
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_useGPSS("uGPSS", "Display GPS speed (0=no, 1=yes)", settings.useGPSSpeed, 1, "autocomplete='off' title='Enable to use a GPS receiver to display actual speed on speedo display'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_useGPSS("uGPSS", "Display GPS speed", settings.useGPSSpeed, 1, "autocomplete='off' title='Check to use a GPS receiver to display actual speed on speedo display' type='checkbox' style='margin-top:12px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif // TC_HAVEGPS
#ifdef TC_HAVETEMP
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_useDpTemp("dpTemp", "Display temperature (0=no, 1=yes)", settings.dispTemp, 1, "autocomplete='off' title='Enable to display temperature on speedo display when idle (needs temperature sensor)'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_useDpTemp("dpTemp", "Display temperature", settings.dispTemp, 1, "autocomplete='off' title='Check to display temperature on speedo display when idle (needs temperature sensor)' type='checkbox' style='margin-top:12px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_tempBright("temBri", "<br>Temperature brightness (0-15)", settings.tempBright, 2, "type='number' min='0' max='15' autocomplete='off'");
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_tempOffNM("toffNM", "Temperature in night mode (0=dimmed, 1=off)", settings.tempOffNM, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_tempOffNM("toffNM", "Temperature off in night mode", settings.tempOffNM, 1, "autocomplete='off' title='If unchecked, the display will be dimmed' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif
#endif // TC_HAVESPEEDO

#ifdef FAKE_POWER_ON
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_fakePwrOn("fpo", "Use fake power switch (0=no, 1=yes)", settings.fakePwrOn, 1, "autocomplete='off' title='Enable to use a switch to fake-power-up and fake-power-down the device'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_fakePwrOn("fpo", "Use fake power switch", settings.fakePwrOn, 1, "title='Check to use a switch to fake-power-up and fake-power-down the device' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif

#ifdef EXTERNAL_TIMETRAVEL_IN
WiFiManagerParameter custom_ettDelay("ettDe", "Delay (ms)", settings.ettDelay, 5, "type='number' min='0' max='60000' title='Externally triggered time travel will be delayed by specified number of millisecs'");
//#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
//WiFiManagerParameter custom_ettLong("ettLg", "Time travel sequence (0=short, 1=complete)", settings.ettLong, 1, "autocomplete='off'");
//#else // -------------------- Checkbox hack: --------------
//WiFiManagerParameter custom_ettLong("ettLg", "Play complete time travel sequence", settings.ettLong, 1, "title='If unchecked, the short \"re-entry\" sequence is played' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
//#endif // -------------------------------------------------
#endif

#ifdef EXTERNAL_TIMETRAVEL_OUT
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_useETTO("uEtto", "Use compatible external props (0=no, 1=yes)", settings.useETTO, 1, "autocomplete='off' title='Enable to use compatible wired external props to be part of the time travel sequence, eg. FluxCapacitor, SID, etc.'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_useETTO("uEtto", "Use compatible external props", settings.useETTO, 1, "autocomplete='off' title='Check to use compatible wired external props to be part of the time travel sequence, eg. Flux Capacitor, SID, etc.' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif // EXTERNAL_TIMETRAVEL_OUT
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_playTTSnd("plyTTS", "Play time travel sounds (0=no, 1=yes)", settings.playTTsnds, 1, "autocomplete='off' title='Disable if other props provide time travel sound.'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_playTTSnd("plyTTS", "Play time travel sounds", settings.playTTsnds, 1, "autocomplete='off' title='Uncheck if other props provide time travel sound.' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

#ifdef TC_HAVEMQTT
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_useMQTT("uMQTT", "Use Home Assistant (0=no, 1=yes)", settings.useMQTT, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_useMQTT("uMQTT", "Use Home Assistant (MQTT 3.1.1)", settings.useMQTT, 1, "type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
WiFiManagerParameter custom_mqttServer("ha_server", "<br>Broker IP[:port] or domain[:port]", settings.mqttServer, 79, "pattern='[a-zA-Z0-9.-:]+' placeholder='Example: 192.168.1.5'");
WiFiManagerParameter custom_mqttUser("ha_usr", "User[:Password]", settings.mqttUser, 63, "placeholder='Example: ronald:mySecret'");
WiFiManagerParameter custom_mqttTopic("ha_topic", "Topic to subscribe to", settings.mqttTopic, 127, "placeholder='Example: home/outside/temperature'");
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_pubMQTT("pMQTT", "Send commands for external props", settings.pubMQTT, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_pubMQTT("pMQTT", "Send commands for external props", settings.pubMQTT, 1, "type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#endif // HAVEMQTT

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_shuffle("musShu", "Shuffle at startup (0=no, 1=yes)", settings.shuffle, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_shuffle("musShu", "Shuffle at startup", settings.shuffle, 1, "type='checkbox' style='margin-top:8px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
WiFiManagerParameter custom_CfgOnSD("CfgOnSD", "Save alarm/volume on SD (0=no, 1=yes)<br><span style='font-size:80%'>Enable this if you often change alarm or volume settings to avoid flash wear</span>", settings.CfgOnSD, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
WiFiManagerParameter custom_CfgOnSD("CfgOnSD", "Save alarm/volume settings on SD<br><span style='font-size:80%'>Check this if you often change alarm or volume settings to avoid flash wear</span>", settings.CfgOnSD, 1, "autocomplete='off' type='checkbox' style='margin-top:5px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------
#ifdef TC_NOCHECKBOXES  // --- Standard text boxes: -------
//WiFiManagerParameter custom_sdFrq("sdFrq", "SD clock speed (0=16Mhz, 1=4Mhz)<br><span style='font-size:80%'>Slower access might help in case of problems with SD cards</span>", settings.sdFreq, 1, "autocomplete='off'");
#else // -------------------- Checkbox hack: --------------
//WiFiManagerParameter custom_sdFrq("sdFrq", "4MHz SD clock speed<br><span style='font-size:80%'>Checking this might help in case of SD card problems</span>", settings.sdFreq, 1, "autocomplete='off' type='checkbox' style='margin-top:12px'", WFM_LABEL_AFTER);
#endif // -------------------------------------------------

WiFiManagerParameter custom_sectstart_head("<div class='sects'>");
WiFiManagerParameter custom_sectstart("</div><div class='sects'>");
WiFiManagerParameter custom_sectend("</div>");

WiFiManagerParameter custom_sectstart_wi("<div style='margin:0;padding:0'>Hold '7' to re-enable Wifi when in power save mode.</div></div><div class='sects'>");
WiFiManagerParameter custom_sectstart_wc("</div><div class='sects'><div class='headl'>World Clock mode</div>");
WiFiManagerParameter custom_sectstart_nm("</div><div class='sects'><div class='headl'>Night mode</div>");
#ifdef TC_HAVETEMP
WiFiManagerParameter custom_sectstart_te("</div><div class='sects'><div class='headl'>Temperature/humidity sensor</div>");
#endif
WiFiManagerParameter custom_sectstart_et("</div><div class='sects'><div class='headl'>External time travel button</div>");
WiFiManagerParameter custom_sectstart_mp("</div><div class='sects'><div class='headl'>MusicPlayer</div>");

WiFiManagerParameter custom_sectend_foot("</div><p></p>");

#define TC_MENUSIZE 7
static const char* wifiMenu[TC_MENUSIZE] = {"wifi", "param", "sep", "restart", "update", "sep", "custom" };

static const char* myHead = "<link rel='shortcut icon' type='image/png' href='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAA9QTFRFjpCRzMvH9tgx8iU9Q7YkHP8yywAAAC1JREFUeNpiYEQDDIwMKAAkwIwEiBTAMIMFCRApgGEGExIgUgDDDHQBNAAQYADhYgGBZLgAtAAAAABJRU5ErkJggg=='><script>function wlp(){return window.location.pathname;}function getn(x){return document.getElementsByTagName(x)}function ge(x){return document.getElementById(x)}function c(l){ge('s').value=l.getAttribute('data-ssid')||l.innerText||l.textContent;p=l.nextElementSibling.classList.contains('l');ge('p').disabled=!p;if(p){ge('p').placeholder='';ge('p').focus();}}window.onload=function(){xx=false;document.title='Time Circuits';if(ge('s')&&ge('dns')){xx=true;xxx=document.title;yyy='Configure WiFi';aa=ge('s').parentElement;bb=aa.innerHTML;dd=bb.search('<hr>');ee=bb.search('<button');cc='<div class=\"sects\">'+bb.substring(0,dd)+'</div><div class=\"sects\">'+bb.substring(dd+4,ee)+'</div>'+bb.substring(ee);aa.innerHTML=cc;document.querySelectorAll('a[href=\"#p\"]').forEach((userItem)=>{userItem.onclick=function(){c(this);return false;}});if(aa=ge('s')){aa.oninput=function(){if(this.placeholder.length>0&&this.value.length==0){ge('p').placeholder='********';}}}}if(ge('uploadbin')||wlp()=='/u'||wlp()=='/wifisave'){xx=true;xxx=document.title;yyy=(wlp()=='/wifisave')?'Configure WiFi':'Firmware update';aa=document.getElementsByClassName('wrap');if(aa.length>0){if((bb=ge('uploadbin'))){aa[0].style.textAlign='center';bb.parentElement.onsubmit=function(){aa=ge('uploadbin');if(aa){aa.disabled=true;aa.innerHTML='Please wait'}}}aa=getn('H3');if(aa.length>0){aa[0].remove()}aa=getn('H1');if(aa.length>0){aa[0].remove()}}}if(ge('ttrp')||wlp()=='/param'){xx=true;xxx=document.title;yyy='Setup';}if(ge('ebnew')){xx=true;bb=getn('H3');aa=getn('H1');xxx=aa[0].innerHTML;yyy=bb[0].innerHTML;ff=aa[0].parentNode;ff.style.position='relative';}if(xx){zz=(Math.random()>0.8);dd=document.createElement('div');dd.classList.add('tpm0');dd.innerHTML='<div class=\"tpm\"><div class=\"tpm2\"><img src=\"data:image/png;base64,'+(zz?'iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAAZQTFRFSp1tAAAA635cugAAAAJ0Uk5T/wDltzBKAAAAbUlEQVR42tzXwRGAQAwDMdF/09QQQ24MLkDj77oeTiPA1wFGQiHATOgDGAp1AFOhDWAslAHMhS6AQKgCSIQmgEgoAsiEHoBQqAFIhRaAWCgByIVXAMuAdcA6YBlwALAKePzgd71QAByP71uAAQC+xwvdcFg7UwAAAABJRU5ErkJggg==':'iVBORw0KGgoAAAANSUhEUgAAAEAAAABACAMAAACdt4HsAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAAAZQTFRFSp1tAAAA635cugAAAAJ0Uk5T/wDltzBKAAAAgElEQVR42tzXQQqDABAEwcr/P50P2BBUdMhee6j7+lw8i4BCD8MiQAjHYRAghAh7ADWMMAcQww5jADHMsAYQwwxrADHMsAYQwwxrADHMsAYQwwxrgLgOPwKeAjgrrACcFkYAzgu3AN4C3AV4D3AP4E3AHcDF+8d/YQB4/Pn+CjAAMaIIJuYVQ04AAAAASUVORK5CYII=')+'\" class=\"tpm3\"></div><H1 class=\"tpmh1\"'+(zz?' style=\"margin-left:1.2em\"':'')+'>'+xxx+'</H1>'+'<H3 class=\"tpmh3\"'+(zz?' style=\"padding-left:4.5em\"':'')+'>'+yyy+'</div></div>';}if(ge('ebnew')){bb[0].remove();aa[0].replaceWith(dd);}if((ge('s')&&ge('dns'))||ge('uploadbin')||wlp()=='/u'||wlp()=='/wifisave'||ge('ttrp')||wlp()=='/param'){aa=document.getElementsByClassName('wrap');if(aa.length>0){aa[0].insertBefore(dd,aa[0].firstChild);aa[0].style.position='relative';}}}</script><style type='text/css'>body{font-family:-apple-system,BlinkMacSystemFont,system-ui,'Segoe UI',Roboto,'Helvetica Neue',Verdana,Helvetica}H1,H2{margin-top:0px;margin-bottom:0px;text-align:center;}H3{margin-top:0px;margin-bottom:5px;text-align:center;}div.msg{border:1px solid #ccc;border-left-width:15px;border-radius:20px;background:linear-gradient(320deg,rgb(255,255,255) 0%,rgb(235,234,233) 100%);}button{transition-delay:250ms;margin-top:10px;margin-bottom:10px;color:#fff;background-color:#225a98;font-variant-caps:all-small-caps;}button.DD{color:#000;border:4px ridge #999;border-radius:2px;background:#e0c942;background-image:url('data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAACAAAAAgCAMAAABEpIrGAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAAADBQTFRF////AAAAMyks8+AAuJYi3NHJo5aQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAbP19EwAAAAh0Uk5T/////////wDeg71ZAAAA4ElEQVR42qSTyxLDIAhF7yChS/7/bwtoFLRNF2UmRr0H8IF4/TBsY6JnQFvTJ8D0ncChb0QGlDvA+hkw/yC4xED2Z2L35xwDRSdqLZpFIOU3gM2ox6mA3tnDPa8UZf02v3q6gKRH/Eyg6JZBqRUCRW++yFYIvCjNFIt9OSC4hol/ItH1FkKRQgAbi0ty9f/F7LM6FimQacPbAdG5zZVlWdfvg+oEpl0Y+jzqIJZ++6fLqlmmnq7biZ4o67lgjBhA0kvJyTww/VK0hJr/LHvBru8PR7Dpx9MT0f8e72lvAQYALlAX+Kfw0REAAAAASUVORK5CYII=');background-repeat:no-repeat;background-origin:content-box;background-size:contain;}br{display:block;font-size:1px;content:''}input[type='checkbox']{display:inline-block;margin-top:10px}input{border:thin inset}small{display:none}em > small{display:inline}form{margin-block-end:0;}.tpm{border:1px solid black;border-radius:5px;padding:0 0 0 0px;min-width:18em;}.tpm2{position:absolute;top:-0.7em;z-index:130;left:0.7em;}.tpm3{width:4em;height:4em;}.tpmh1{font-variant-caps:all-small-caps;margin-left:2em;}.tpmh3{background:#000;font-size:0.6em;color:#ffa;padding-left:7em;margin-left:0.5em;margin-right:0.5em;border-radius:5px}.sects{background-color:#eee;border-radius:7px;margin-bottom:20px;padding-bottom:7px;padding-top:7px}.tpm0{position:relative;width:300px;margin:0 auto 0 auto;}.headl{margin:0 0 3px 0;padding:0}.cmp0{margin:0;padding:0;}.sel0{font-size:90%;width:auto;margin-left:10px;vertical-align:baseline;}</style>";

static const char* myCustMenu = "<form action='/erase' method='get' onsubmit='return confirm(\"This erases the WiFi config and reboots. The clock will restart in access point mode. Are you sure?\");'><button id='ebnew' class='DD'>Erase WiFi Config</button></form><br/><img style='display:block;margin:10px auto 10px auto;' src='data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAR8AAAAyCAYAAABlEt8RAAAAGXRFWHRTb2Z0d2FyZQBBZG9iZSBJbWFnZVJlYWR5ccllPAAADQ9JREFUeNrsXTFzG7sRhjTuReYPiGF+gJhhetEzTG2moFsrjVw+vYrufOqoKnyl1Zhq7SJ0Lc342EsT6gdIof+AefwFCuksnlerBbAA7ygeH3bmRvTxgF3sLnY/LMDzjlKqsbgGiqcJXEPD97a22eJKoW2mVqMB8HJRK7D/1DKG5fhH8NdHrim0Gzl4VxbXyeLqLK4DuDcGvXF6P4KLG3OF8JtA36a2J/AMvc/xTh3f22Q00QnSa0r03hGOO/Wws5Y7RD6brbWPpJ66SNHl41sTaDMSzMkTxndriysBHe/BvVs0XyeCuaEsfqblODHwGMD8+GHEB8c1AcfmJrurbSYMHK7g8CC4QknS9zBQrtSgO22gzJNnQp5pWOyROtqa7k8cOkoc+kyEOm1ZbNAQyv7gcSUryJcG+kiyZt9qWcagIBhkjn5PPPWbMgHX1eZoVzg5DzwzDKY9aFtT5aY3gknH0aEF/QxRVpDyTBnkxH3WvGmw0zR32Pu57XVUUh8ZrNm3hh7PVwQ+p1F7KNWEOpjuenR6wEArnwCUqPJT6IQ4ZDLQEVpm2eg9CQQZY2wuuJicD0NlG3WeWdedkvrILxak61rihbR75bGyOBIEHt+lLDcOEY8XzM0xYt4i2fPEEdV+RUu0I1BMEc70skDnuUVBtgWTX9M+GHrikEuvqffJ+FOiS6r3AYLqB6TtwBA0ahbko8eQMs9OBY46KNhetgDo0rWp76/o8wVBBlOH30rloz5CJ1zHgkg0rw4EKpygTe0wP11Lob41EdiBzsEvyMZ6HFNlrtFeGOTLLAnwC/hzBfGYmNaICWMAaY2h5WgbCuXTnGo7kppPyhT+pHUAGhRM/dYcNRbX95mhXpB61FUSQV2illPNJ7TulgT0KZEzcfitywdTZlJL5W5Z2g2E/BoW32p5+GuN8bvOCrU+zo4VhscPmSTLrgGTSaU0smTpslAoBLUhixZT+6Ftb8mS15SRJciH031IpoxLLxmCqwXOj0YgvxCaMz46Ve7dWd9VRMbwSKXBZxKooEhmkgSC1BKwpoaAc+DB0wStv+VQ48qLNqHwHZJoKiWQea+guTyX2i8k+Pg4Q8UDDWwqdQrIOjWBXjKhsx8wur5gkkVFiOj2Eep6rsn/pWTop1aAjxRBGYO48w5AEymPF2ucuPMcg08ivBfqSAnK/LiwN1byA5Mt4VLJFHxsQX/CBPmGAxn5OFmKglpL+W3nSu01tPjDlKCvQcF+emRYCk8DbS1tV8lhXvmUBpbPvSKJ6z+L6xR0nAnGmTBjHRIeeJPqEPFIQoLPNzIJXUasgIL2LevbVeh9gcFn39D/rSALJyhQvHGs732zVM3yXYM48hTZjAs6YwfvpTP9ghx9WIC9UsskzUDfB2tCX2885cMJqqWenqdKcw4itZx8a6D4Ix7v4f6Jo69DZqxj4h8DJmljHr/vzEmDzxR1VvE0okY9iSovzUFxWcAk08uINEd5uL4o8tE222Oys2scExS8Xj1TDWPp0P/a0KXXvsXWpw7k00D2OBEu12z8LjyXeXry7zE8hiDXKstG/dOY1MAjBR2IDxlWPByXQ02tktZ7NOlT2kcBbS9UMYXbOYHD9ADhxBCYpDWJ0TPXXUYEUZeBTgVJdhlQv0Iw2SPzxBcd/xagmyn4wxeDnw9z0MMEeIwNPEY+yOdgBUFSlX8BrshDhmOydEwQgvjogOOmDJ7lIFfGGPjQEGAy8nyFPDsVyo2XXmMGcq9ir4lgkuClV5FFXO6QYQi/VSZuyK8HQksZU7BpC2TeJ3O9Y+ibO2SYWXi00LJ9j/Bo7BZgxJck4r0pALanzJU3ZernL6CVMAsvx/4Pj+eVZSnbckyGzIB8bpnnG4xjSLKX3nZfdenF2SvznMxFHvGYeMp3C7b+1VHDkSLYfzoCye0KvuWyS0M9PlNm0/WU0ZMrSC/HVWN4tHYDJkYmMOIwB6NsCqVCw+hnR0TRXPD16dOmaw6dZobgFJLVRzmh3zx0f7BBPqFfFzMgy19JMLiA5dkpBJOaADFlBt/q5DSWZA36ojuWFUnwCXHc0RYFHwlKccHvjiOA15g+XHWaqUGmlJm4Pgkkr2VEXojk24b7Aw3QDYFOE7hGAUvyEamf5DG3pmvQ0xMekuATcqYgI0svCtv1j8z0Vct5oDXSf2XFvlZdi7t02GECHA763xR/TN2FCnRWxrWacckm/0htNo1yXgoVmdgrhrmQp8xiHruOThL1ePt87lFfsRllmR2+oitvgx2R/kPrBR0GLkrGPyXwmAbfCYHrr9TPX/5qGL7n4DkRLFUmWzD5hyUIPvM1onyaEDqe82IKfyvoXidHJITfjqksPFIu+Cy3AJe/Rp2pp2cLRis4bZ4BRvLmuVA6RP39Wz0+EepjGNfSa8jofanz/zI8BwZ0GQKnU099pAXaKwmYbEXQ1xXkozraV8X//jF06dVSP3dtZzDGj+rpgUDTPH+v3G8RbUF/H9F3H0kynZuCj7JAeJ/tQJr9y/IjQZcORoGTljpIouxvE9T0xYJgxg6+08CgZcvscen1/EuvYSA/SXL+Ta12NERyHGMgrfnoSdcKEMqV/ctGRx46oBmbLr0ygdPcOp7JDDUeW/CZlHDyl2HptU4/d/kWRw3lfsPgrVpt50sS3PTLxZzBZynMhZK9UW4TjFIEjUEHfw6YhK7xL7//q3p62nQOPF0B33Uwbipcim168Nn0Xa+M2HDdSy/J3Frq8CX41Zzxt9NAgEFRt4nHN+CxTTvfW0WNLViaRioH1VQxO81iHjsPDw/RDJEiRVo77UYVRIoUKQafSJEixeATKVKkSDH4RIoUKQafSJEiRYrBJ1KkSDH4RIoUKVIMPpEiRYrBJ1KkSJFi8IkUKVIMPpEiRYrBJ1KkSJFi8IkUKdIfg15s02B2dnaWf+qLq7u4qur/r4r8vLjuDU168PfM0fUx9Ef7ou17TNurxXUTMJwq4jtDY5kxz2hafncOn9uLqwm8r9C/OaLynxM+PdS3lomjG9BPFz2v7SF9ntO7MsjlIuoL96BDZRmHloPTF7YB1v2ZxV/qxA5UNqyLK6FsmE8d6eSHf5bmTRVLQbflAkNw75ftGgIPff+siS7huTZVH2lver/tB0+zLMfxnennGj3TNDxzR8bXY8Zrev/uA2mD718SXXBXD3SEn297Pq+D6jXz/HdLAKXUNfDsO8Zx6dAXluEO7tUJb32/ythBBw2bn7hkUwb9/OBZlvm6VcgHMpvOIFdg5C78/Uycu4cyWN70jvA5hux4L2yPM+c5fG6TrP8J7t+gsXUFKOuKZGCO+hbE+Bm178Mz5yh722xzziAfE/8mjPcMBdumB4rsIVvcIKRB25+Tcc4s+uqCDEv7vAVd9OA+lrMObWaGxPIB6fIGySuVrYt0cQb320hnEfk8A/JRTDDR2UqRiXuNslLeyEfSNoRfFTm4Rjl0vE0H8unZ3AGhqU8G5KMc903I59LAk/tey9A0jE3k2gbbVoV24fRFZe0yunLpvce00XLVV5Dt97FF5PN8NCNZhmbYNjjN3zwDgq/zr0I3INsnyGy6bjRDYzDVQFzIoE7GfU+yq67DHMNzVzmNqUr4zgyytuFZrlZ246nDJiSZc+jvntFXk2knRQ+fiT1wf1eWYKsYFDjzkO0eIcQqQmezUs3ULUQ+FOE8oMJgFdBCn2QQKRLxqZn0AF7TWo10ot4x6/2qB4qR1nx6DPLRNafrHJGPqX7hi5Sk1GZqYn2BTdtEX5fInndMDfETQWnfUd2Ns4MECbtkw3xxra8Zkc9mkF6Ln6MsI93dMhFdg/ctNQucHd8GoLe/QNBswjjaEMxer6gXWvO5YQLfPeiorx7vpq2KSG8CUUzoOKkOe6SOxNn0nglibTSG16R+eIPsU0W1ujzIJttrJFsXEsYyaP0pIp/nRT7HaF1dJZn6Dox0iTKZK8v61nzaJHOuSnXC61i5d9FCaz4PBH3drbnmU1ePd+3yomPF79q56iof4Jk7w/N1gpAoMqJ6/0DQuI+/2ZCy3v1ql2W+buMhw2Mw8Dlkh5mh5tFGNaF2zjJcQXbVtZtj4ow99XR7FlPXINOM1BOOSd/tnJHKmUPOIkjXoOokuNYdgZMLHnVHTVAqz1Lf71Dw4OTFCOnKUYvS6LhJ5JXWFKku8K5t3O16RuTjqstw2U1a8/Hd7WozWfxBkNWuCUr7ztQs+urx2ZPvSnbOByM/fTUN8uOxr3O3q8vUM/RnSTCsqsdno3ANpUvGdc3ow4QULw2opa/4szimfq4NY/sglK2P7I4R/HWs+USi9RW9DJPWms5RraKO6lS4/TvIcj2U9e4FPOrMBLaddTorABm66DOg1j6SVyMxaWZ/h3SIkRytx/jsYGpd6HNQM6Z+Jdkd/Duqp9VRO6lsV+rnuSWMtt6WaXJs1X8aCD+v2DaqK/nhxEh/PB0+GVtZ5vT/BBgARwZUDnOS4TkAAAAASUVORK5CYII='><div style='font-size:9px;margin-left:auto;margin-right:auto;text-align:center;'>Version " TC_VERSION " (" TC_VERSION_EXTRA ")<br>Powered by <a href='https://github.com/realA10001986/Time-Circuits-Display' target=_blank>A10001986</a></div>";

static int  shouldSaveConfig = 0;
static bool shouldSaveIPConfig = false;
static bool shouldDeleteIPConfig = false;

// Did user configure a WiFi network to connect to?
bool wifiHaveSTAConf = false;

static unsigned long lastConnect = 0;
static unsigned long consecutiveAPmodeFB = 0;

// WiFi power management in AP mode
bool          wifiInAPMode = false;
bool          wifiAPIsOff = false;
unsigned long wifiAPModeNow;
unsigned long wifiAPOffDelay = 0;     // default: never

// WiFi power management in STA mode
bool          wifiIsOff = false;
unsigned long wifiOnNow = 0;
unsigned long wifiOffDelay     = 0;   // default: never
unsigned long origWiFiOffDelay = 0;

#ifdef TC_HAVEMQTT
#define       MQTT_SHORT_INT  (30*1000)
#define       MQTT_LONG_INT   (5*60*1000)
bool          useMQTT = false;
char          mqttUser[64] = { 0 };
char          mqttPass[64] = { 0 };
char          mqttServer[80] = { 0 };
uint16_t      mqttPort = 1883;
bool          haveMQTTaudio = false;
const char    *mqttAudioFile = "/ha-alert.mp3";
bool          pubMQTT = false;
static unsigned long mqttReconnectNow = 0;
static unsigned long mqttReconnectInt = MQTT_SHORT_INT;
static uint16_t      mqttReconnFails = 0;
static bool          mqttSubAttempted = false;
static bool          mqttOldState = true;
static bool          mqttDoPing = true;
static bool          mqttRestartPing = false;
static bool          mqttPingDone = false;
static unsigned long mqttPingNow = 0;
static unsigned long mqttPingInt = MQTT_SHORT_INT;
static uint16_t      mqttPingsExpired = 0;
#endif

static void wifiOff(bool force);
static void wifiConnect(bool deferConfigPortal = false);
static void saveParamsCallback();
static void saveConfigCallback();
static void preUpdateCallback();
static void preSaveConfigCallback();
static void waitConnectCallback();

static void setupStaticIP();
static bool isIp(char *str);
static void ipToString(char *str, IPAddress ip);
static IPAddress stringToIp(char *str);

static void getParam(String name, char *destBuf, size_t length);
static bool myisspace(char mychar);
static char* strcpytrim(char* destination, const char* source, bool doFilter = false);
static char* strcpyfilter(char* destination, const char* source);
static void mystrcpy(char *sv, WiFiManagerParameter *el);
#ifndef TC_NOCHECKBOXES
static void strcpyCB(char *sv, WiFiManagerParameter *el);
static void setCBVal(WiFiManagerParameter *el, char *sv);
#endif

#ifdef TC_HAVEMQTT
static void strcpyutf8(char *dst, const char *src, unsigned int len);
static void mqttPing();
static bool mqttReconnect(bool force = false);
static void mqttLooper();
static void mqttCallback(char *topic, byte *payload, unsigned int length);
static void mqttSubscribe();
#endif


/*
 * wifi_setup()
 *
 */
void wifi_setup()
{
    int temp;

    // Explicitly set mode, esp allegedly defaults to STA_AP
    WiFi.mode(WIFI_MODE_STA);

    #ifndef TC_DBG
    wm.setDebugOutput(false);
    #endif

    wm.setParamsPage(true);
    wm.setBreakAfterConfig(true);
    wm.setConfigPortalBlocking(false);
    wm.setPreSaveConfigCallback(preSaveConfigCallback);
    wm.setSaveConfigCallback(saveConfigCallback);
    wm.setSaveParamsCallback(saveParamsCallback);
    wm.setPreOtaUpdateCallback(preUpdateCallback);
    wm.setHostname(settings.hostName);
    wm.setCaptivePortalEnable(false);
    
    // Our style-overrides, the page title
    wm.setCustomHeadElement(myHead);
    wm.setTitle(F("Time Circuits"));
    wm.setDarkMode(false);

    // Hack version number into WiFiManager main page
    wm.setCustomMenuHTML(myCustMenu);

    // Static IP info is not saved by WiFiManager,
    // have to do this "manually". Hence ipsettings.
    wm.setShowStaticFields(true);
    wm.setShowDnsFields(true);

    temp = (int)atoi(settings.wifiConTimeout);
    if(temp < 7) temp = 7;
    if(temp > 25) temp = 25;
    wm.setConnectTimeout(temp);

    temp = (int)atoi(settings.wifiConRetries);
    if(temp < 1) temp = 1;
    if(temp > 15) temp = 15;
    wm.setConnectRetries(temp);

    wm.setCleanConnect(true);
    //wm.setRemoveDuplicateAPs(false);

    wm.setMenu(wifiMenu, TC_MENUSIZE);
    
    wm.addParameter(&custom_sectstart_head);// 6
    wm.addParameter(&custom_ttrp);
    wm.addParameter(&custom_alarmRTC);
    wm.addParameter(&custom_playIntro);
    wm.addParameter(&custom_mode24);
    wm.addParameter(&custom_beep_aint);
    
    wm.addParameter(&custom_sectstart);     // 7
    wm.addParameter(&custom_hostName);
    wm.addParameter(&custom_wifiConRetries);
    wm.addParameter(&custom_wifiConTimeout);
    wm.addParameter(&custom_wifiPRe);
    wm.addParameter(&custom_wifiOffDelay);
    wm.addParameter(&custom_wifiAPOffDelay);
    
    wm.addParameter(&custom_sectstart_wi);  // 3    (ss_wi contains wifiHint from section above)
    wm.addParameter(&custom_timeZone);
    wm.addParameter(&custom_ntpServer);   

    wm.addParameter(&custom_sectstart_wc);  // 5
    wm.addParameter(&custom_timeZone1);
    wm.addParameter(&custom_timeZoneN1);
    wm.addParameter(&custom_timeZone2);
    wm.addParameter(&custom_timeZoneN2);
    
    wm.addParameter(&custom_sectstart_nm);  // 9
    wm.addParameter(&custom_dtNmOff);
    wm.addParameter(&custom_ptNmOff);
    wm.addParameter(&custom_ltNmOff);
    wm.addParameter(&custom_autoNMTimes);
    wm.addParameter(&custom_autoNMOn);
    wm.addParameter(&custom_autoNMOff);
    #ifdef TC_HAVELIGHT
    wm.addParameter(&custom_uLS);
    wm.addParameter(&custom_lxLim);
    #endif

    #ifdef TC_HAVETEMP
    wm.addParameter(&custom_sectstart_te);  // 3
    wm.addParameter(&custom_tempUnit);
    wm.addParameter(&custom_tempOffs);
    #endif

    #ifdef TC_HAVESPEEDO
    wm.addParameter(&custom_sectstart);     // 8
    wm.addParameter(&custom_speedoType);
    wm.addParameter(&custom_speedoBright);
    wm.addParameter(&custom_speedoFact);
    #ifdef TC_HAVEGPS
    wm.addParameter(&custom_useGPSS);
    #endif
    #ifdef TC_HAVETEMP
    wm.addParameter(&custom_useDpTemp);
    wm.addParameter(&custom_tempBright);
    wm.addParameter(&custom_tempOffNM);
    #endif
    #endif

    #ifdef FAKE_POWER_ON
    wm.addParameter(&custom_sectstart);     // 2
    wm.addParameter(&custom_fakePwrOn);
    #endif
    
    #ifdef EXTERNAL_TIMETRAVEL_IN
    wm.addParameter(&custom_sectstart_et);  // 2
    wm.addParameter(&custom_ettDelay);
    //wm.addParameter(&custom_ettLong);
    #endif
    
    wm.addParameter(&custom_sectstart);     // 3
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    wm.addParameter(&custom_useETTO);
    #endif
    wm.addParameter(&custom_playTTSnd);

    #ifdef TC_HAVEMQTT
    wm.addParameter(&custom_sectstart);     // 6
    wm.addParameter(&custom_useMQTT);
    wm.addParameter(&custom_mqttServer);
    wm.addParameter(&custom_mqttUser);
    wm.addParameter(&custom_mqttTopic);
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    wm.addParameter(&custom_pubMQTT);
    #endif
    #endif
    
    wm.addParameter(&custom_sectstart_mp);  // 2
    wm.addParameter(&custom_shuffle);

    wm.addParameter(&custom_sectstart);     // 3
    wm.addParameter(&custom_CfgOnSD);
    //wm.addParameter(&custom_sdFrq);
    wm.addParameter(&custom_sectend_foot);

    updateConfigPortalValues();

    #ifdef TC_MDNS
    if(MDNS.begin(settings.hostName)) {
        MDNS.addService("http", "tcp", 80);
    }
    #endif

    // Read settings for WiFi powersave countdown
    wifiOffDelay = (unsigned long)atoi(settings.wifiOffDelay);
    if(wifiOffDelay > 0 && wifiOffDelay < 10) wifiOffDelay = 10;
    origWiFiOffDelay = wifiOffDelay *= (60 * 1000);
    #ifdef TC_DBG
    Serial.printf("wifiOffDelay is %d\n", wifiOffDelay);
    #endif
    wifiAPOffDelay = (unsigned long)atoi(settings.wifiAPOffDelay);
    if(wifiAPOffDelay > 0 && wifiAPOffDelay < 10) wifiAPOffDelay = 10;
    wifiAPOffDelay *= (60 * 1000);

    // Read setting for "periodic retries"
    // This determines if, after a fall-back to AP mode,
    // the device should periodically retry to connect
    // to a configured WiFi network; see time_loop().
    doAPretry = ((int)atoi(settings.wifiPRetry) > 0);

    // Configure static IP
    if(loadIpSettings()) {
        setupStaticIP();
    }
           
    // Find out if we have a configured WiFi network to connect to,
    // or if we are condemned to AP mode for good
    {
        wifi_config_t conf;
        esp_wifi_get_config(WIFI_IF_STA, &conf);
        wifiHaveSTAConf = (conf.sta.ssid[0] != 0);
        #ifdef TC_DBG
        Serial.printf("WiFi network configured: %s (%s)\n", wifiHaveSTAConf ? "YES" : "NO", 
                    wifiHaveSTAConf ? (const char *)conf.sta.ssid : "n/a");
        #endif
    }

    // Connect, but defer starting the CP
    wifiConnect(true);
    
#ifdef TC_HAVEMQTT
    useMQTT = ((int)atoi(settings.useMQTT) > 0);
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    pubMQTT = ((int)atoi(settings.pubMQTT) > 0);
    #endif
    
    if((!settings.mqttServer[0]) || // No server -> no MQTT
       (wifiInAPMode))              // WiFi in AP mode -> no MQTT
        useMQTT = false;  
    
    if(useMQTT) {

        bool mqttRes = false;
        char *t;
        int tt;

        // No WiFi power save if we're using MQTT
        origWiFiOffDelay = wifiOffDelay = 0;

        if((t = strchr(settings.mqttServer, ':'))) {
            strncpy(mqttServer, settings.mqttServer, t - settings.mqttServer);
            mqttServer[t - settings.mqttServer + 1] = 0;
            tt = atoi(t+1);
            if(tt > 0 && tt <= 65535) {
                mqttPort = tt;
            }
        } else {
            strcpy(mqttServer, settings.mqttServer);
        }

        if(isIp(mqttServer)) {
            mqttClient.setServer(stringToIp(mqttServer), mqttPort);
        } else {
            IPAddress remote_addr;
            if(WiFi.hostByName(mqttServer, remote_addr)) {
                mqttClient.setServer(remote_addr, mqttPort);
            } else {
                mqttClient.setServer(mqttServer, mqttPort);
                // Disable PING if we can't resolve domain
                mqttDoPing = false;
                Serial.printf("MQTT: Failed to resolve '%s'\n", mqttServer);
            }
        }
        
        mqttClient.setCallback(mqttCallback);
        mqttClient.setLooper(mqttLooper);

        if(settings.mqttUser[0] != 0) {
            if((t = strchr(settings.mqttUser, ':'))) {
                strncpy(mqttUser, settings.mqttUser, t - settings.mqttUser);
                mqttUser[t - settings.mqttUser + 1] = 0;
                strcpy(mqttPass, t + 1);
            } else {
                strcpy(mqttUser, settings.mqttUser);
            }
        }

        #ifdef TC_DBG
        Serial.printf("MQTT: server '%s' port %d user '%s' pass '%s'\n", mqttServer, mqttPort, mqttUser, mqttPass);
        #endif

        haveMQTTaudio = check_file_SD(mqttAudioFile);
            
        mqttReconnect(true);
        // Rest done in loop
            
    } else {

        #ifdef EXTERNAL_TIMETRAVEL_OUT
        pubMQTT = false;
        #endif

        #ifdef TC_DBG
        Serial.println("MQTT: Disabled");
        #endif

    }
#endif
}

/*
 * wifi_loop()
 *
 */
void wifi_loop()
{
    char oldCfgOnSD = 0;

#ifdef TC_HAVEMQTT
    if(useMQTT) {
        if(mqttClient.state() != MQTT_CONNECTING) {
            if(!mqttClient.connected()) {
                if(mqttOldState || mqttRestartPing) {
                    // Disconnection first detected:
                    mqttPingDone = mqttDoPing ? false : true;
                    mqttPingNow = mqttRestartPing ? millis() : 0;
                    mqttOldState = false;
                    mqttRestartPing = false;
                    mqttSubAttempted = false;
                }
                if(mqttDoPing && !mqttPingDone) {
                    audio_loop();
                    mqttPing();
                    audio_loop();
                }
                if(mqttPingDone) {
                    audio_loop();
                    mqttReconnect();
                    audio_loop();
                }
            } else {
                // Only call Subscribe() if connected
                mqttSubscribe();
                mqttOldState = true;
            }
        }
        mqttClient.loop();
    }
#endif
    
    wm.process();
    
    if(shouldSaveIPConfig) {

        #ifdef TC_DBG
        Serial.println(F("WiFi: Saving IP config"));
        #endif

        writeIpSettings();

        shouldSaveIPConfig = false;

    } else if(shouldDeleteIPConfig) {

        #ifdef TC_DBG
        Serial.println(F("WiFi: Deleting IP config"));
        #endif

        deleteIpSettings();

        shouldDeleteIPConfig = false;

    }

    if(shouldSaveConfig) {

        // Save settings and restart esp32

        #ifdef TC_DBG
        Serial.println(F("Config Portal: Saving config"));
        #endif

        // Only read parms if the user actually clicked SAVE on the params page
        if(shouldSaveConfig > 1) {

            getParam("beepmode", settings.beep, 1);
            if(strlen(settings.beep) == 0) {
                sprintf(settings.beep, "%d", DEF_BEEP);
            }
            getParam("rotate_times", settings.autoRotateTimes, 1);
            if(strlen(settings.autoRotateTimes) == 0) {
                sprintf(settings.autoRotateTimes, "%d", DEF_AUTOROTTIMES);
            }
            strcpytrim(settings.hostName, custom_hostName.getValue(), true);
            if(strlen(settings.hostName) == 0) {
                strcpy(settings.hostName, DEF_HOSTNAME);
            } else {
                char *s = settings.hostName;
                for ( ; *s; ++s) *s = tolower(*s);
            }
            mystrcpy(settings.wifiConRetries, &custom_wifiConRetries);
            mystrcpy(settings.wifiConTimeout, &custom_wifiConTimeout);
            mystrcpy(settings.wifiOffDelay, &custom_wifiOffDelay);
            mystrcpy(settings.wifiAPOffDelay, &custom_wifiAPOffDelay);
            strcpytrim(settings.ntpServer, custom_ntpServer.getValue());
            strcpytrim(settings.timeZone, custom_timeZone.getValue());

            strcpytrim(settings.timeZoneDest, custom_timeZone1.getValue());
            strcpytrim(settings.timeZoneDep, custom_timeZone2.getValue());
            strcpyfilter(settings.timeZoneNDest, custom_timeZoneN1.getValue());
            if(strlen(settings.timeZoneNDest) != 0) {
                char *s = settings.timeZoneNDest;
                for ( ; *s; ++s) *s = toupper(*s);
            }
            strcpyfilter(settings.timeZoneNDep, custom_timeZoneN2.getValue());
            if(strlen(settings.timeZoneNDep) != 0) {
                char *s = settings.timeZoneNDep;
                for ( ; *s; ++s) *s = toupper(*s);
            }
            
            getParam("autonmtimes", settings.autoNMPreset, 2);
            if(strlen(settings.autoNMPreset) == 0) {
                sprintf(settings.autoNMPreset, "%d", DEF_AUTONM_PRESET);
            }
            mystrcpy(settings.autoNMOn, &custom_autoNMOn);
            mystrcpy(settings.autoNMOff, &custom_autoNMOff);
            #ifdef TC_HAVELIGHT
            mystrcpy(settings.luxLimit, &custom_lxLim);
            #endif
            
            #ifdef EXTERNAL_TIMETRAVEL_IN
            mystrcpy(settings.ettDelay, &custom_ettDelay);
            #endif

            #ifdef TC_HAVETEMP
            mystrcpy(settings.tempOffs, &custom_tempOffs);
            #endif
            
            #ifdef TC_HAVESPEEDO
            getParam("speedo_type", settings.speedoType, 2);
            if(strlen(settings.speedoType) == 0) {
                sprintf(settings.speedoType, "%d", DEF_SPEEDO_TYPE);
            }
            mystrcpy(settings.speedoBright, &custom_speedoBright);
            mystrcpy(settings.speedoFact, &custom_speedoFact);
            #ifdef TC_HAVETEMP
            mystrcpy(settings.tempBright, &custom_tempBright);
            #endif
            #endif

            #ifdef TC_HAVEMQTT
            strcpytrim(settings.mqttServer, custom_mqttServer.getValue());
            strcpyutf8(settings.mqttUser, custom_mqttUser.getValue(), sizeof(settings.mqttUser));
            strcpyutf8(settings.mqttTopic, custom_mqttTopic.getValue(), sizeof(settings.mqttTopic));
            #endif
            
            #ifdef TC_NOCHECKBOXES // --------- Plain text boxes:

            mystrcpy(settings.timesPers, &custom_ttrp);
            mystrcpy(settings.alarmRTC, &custom_alarmRTC);
            mystrcpy(settings.playIntro, &custom_playIntro);
            mystrcpy(settings.mode24, &custom_mode24);

            mystrcpy(settings.wifiPRetry, &custom_wifiPRe);
            
            mystrcpy(settings.dtNmOff, &custom_dtNmOff);
            mystrcpy(settings.ptNmOff, &custom_ptNmOff);
            mystrcpy(settings.ltNmOff, &custom_ltNmOff);
            #ifdef TC_HAVELIGHT
            mystrcpy(settings.useLight, &custom_uLS);
            #endif
            
            #ifdef TC_HAVETEMP
            mystrcpy(settings.tempUnit, &custom_tempUnit);
            #endif
            
            #ifdef TC_HAVESPEEDO
            #ifdef TC_HAVEGPS
            mystrcpy(settings.useGPSSpeed, &custom_useGPSS);
            #endif
            #ifdef TC_HAVETEMP
            mystrcpy(settings.dispTemp, &custom_useDpTemp);
            mystrcpy(settings.tempOffNM, &custom_tempOffNM);
            #endif
            #endif
            
            //#ifdef EXTERNAL_TIMETRAVEL_IN
            //mystrcpy(settings.ettLong, &custom_ettLong);
            //#endif

            #ifdef FAKE_POWER_ON
            mystrcpy(settings.fakePwrOn, &custom_fakePwrOn);
            #endif
            
            #ifdef EXTERNAL_TIMETRAVEL_OUT
            mystrcpy(settings.useETTO, &custom_useETTO);
            #endif
            mystrcpy(settings.playTTsnds, &custom_playTTSnd);

            #ifdef TC_HAVEMQTT
            mystrcpy(settings.useMQTT, &custom_useMQTT);
            #ifdef EXTERNAL_TIMETRAVEL_OUT
            mystrcpy(settings.pubMQTT, &custom_pubMQTT);
            #endif
            #endif

            mystrcpy(settings.shuffle, &custom_shuffle);

            oldCfgOnSD = settings.CfgOnSD[0];
            mystrcpy(settings.CfgOnSD, &custom_CfgOnSD);
            //mystrcpy(settings.sdFreq, &custom_sdFrq);

            #else // -------------------------- Checkboxes:

            strcpyCB(settings.timesPers, &custom_ttrp);
            strcpyCB(settings.alarmRTC, &custom_alarmRTC);
            strcpyCB(settings.playIntro, &custom_playIntro);
            strcpyCB(settings.mode24, &custom_mode24);

            strcpyCB(settings.wifiPRetry, &custom_wifiPRe);

            strcpyCB(settings.dtNmOff, &custom_dtNmOff);
            strcpyCB(settings.ptNmOff, &custom_ptNmOff);
            strcpyCB(settings.ltNmOff, &custom_ltNmOff);
            #ifdef TC_HAVELIGHT
            strcpyCB(settings.useLight, &custom_uLS);
            #endif
            
            #ifdef TC_HAVETEMP
            strcpyCB(settings.tempUnit, &custom_tempUnit);
            #endif
            
            #ifdef TC_HAVESPEEDO
            #ifdef TC_HAVEGPS
            strcpyCB(settings.useGPSSpeed, &custom_useGPSS);
            #endif
            #ifdef TC_HAVETEMP
            strcpyCB(settings.dispTemp, &custom_useDpTemp);
            strcpyCB(settings.tempOffNM, &custom_tempOffNM);
            #endif
            #endif
            
            //#ifdef EXTERNAL_TIMETRAVEL_IN
            //strcpyCB(settings.ettLong, &custom_ettLong);
            //#endif
            
            #ifdef FAKE_POWER_ON
            strcpyCB(settings.fakePwrOn, &custom_fakePwrOn);
            #endif
            
            #ifdef EXTERNAL_TIMETRAVEL_OUT
            strcpyCB(settings.useETTO, &custom_useETTO);
            #endif
            strcpyCB(settings.playTTsnds, &custom_playTTSnd);

            #ifdef TC_HAVEMQTT
            strcpyCB(settings.useMQTT, &custom_useMQTT);
            #ifdef EXTERNAL_TIMETRAVEL_OUT
            strcpyCB(settings.pubMQTT, &custom_pubMQTT);
            #endif
            #endif

            strcpyCB(settings.shuffle, &custom_shuffle);

            oldCfgOnSD = settings.CfgOnSD[0];
            strcpyCB(settings.CfgOnSD, &custom_CfgOnSD);
            //strcpyCB(settings.sdFreq, &custom_sdFrq);

            #endif  // -------------------------

            // Copy alarm/volume settings to other medium if
            // user changed respective option
            if(oldCfgOnSD != settings.CfgOnSD[0]) {
                copySettings();
            }

        }

        // Write settings if requested, or no settings file exists
        if(shouldSaveConfig > 1 || !checkConfigExists()) {
            write_settings();
        }
        
        shouldSaveConfig = 0;

        // Reset esp32 to load new settings

        stopAudio();

        allOff();
        #ifdef TC_HAVESPEEDO
        if(useSpeedo) speedo.off();
        #endif
        destinationTime.resetBrightness();
        destinationTime.showTextDirect("REBOOTING");
        destinationTime.on();

        #ifdef TC_DBG
        Serial.println(F("Config Portal: Restarting ESP...."));
        #endif

        Serial.flush();

        esp_restart();
    }

    // WiFi power management
    // If a delay > 0 is configured, WiFi is powered-down after timer has
    // run out. The timer starts when the device is powered-up/boots.
    // There are separate delays for AP mode and STA mode.
    // WiFi can be re-enabled for the configured time by holding '7'
    // on the keypad.
    // NTP requests will - under some conditions - re-enable WiFi for a 
    // short while automatically if the user configured a WiFi network 
    // to connect to.
    
    if(wifiInAPMode) {
        // Disable WiFi in AP mode after a configurable delay (if > 0)
        if(wifiAPOffDelay > 0) {
            if(!wifiAPIsOff && (millis() - wifiAPModeNow >= wifiAPOffDelay)) {
                wifiOff(false);
                wifiAPIsOff = true;
                wifiIsOff = false;
                syncTrigger = false;
                #ifdef TC_DBG
                Serial.println(F("WiFi (AP-mode) is off. Hold '7' to re-enable."));
                #endif
            }
        }
    } else {
        // Disable WiFi in STA mode after a configurable delay (if > 0)
        if(origWiFiOffDelay > 0) {
            if(!wifiIsOff && (millis() - wifiOnNow >= wifiOffDelay)) {
                wifiOff(false);
                wifiIsOff = true;
                wifiAPIsOff = false;
                syncTrigger = false;
                #ifdef TC_DBG
                Serial.println(F("WiFi (STA-mode) is off. Hold '7' to re-enable."));
                #endif
            }
        }
    }

}

static void wifiConnect(bool deferConfigPortal)
{     
    // Automatically connect using saved credentials if they exist
    // If connection fails it starts an access point with the specified name
    if(wm.autoConnect("TCD-AP")) {
        #ifdef TC_DBG
        Serial.println(F("WiFi connected"));
        #endif

        // Since WM 2.0.13beta, starting the CP invokes an async
        // WiFi scan. This interferes with network access for a 
        // few seconds after connecting. So, during boot, we start
        // the CP later, to allow a quick NTP update.
        if(!deferConfigPortal) {
            wm.startWebPortal();
        }

        // Allow modem sleep:
        // WIFI_PS_MIN_MODEM is the default, and activated when calling this
        // with "true". When this is enabled, received WiFi data can be
        // delayed for as long as the DTIM period.
        // Since it is the default setting, so no need to call it here.
        //WiFi.setSleep(true);

        // Disable modem sleep, don't want delays accessing the CP or
        // with MQTT.
        WiFi.setSleep(false);

        // Set transmit power to max; we might be connecting as STA after
        // a previous period in AP mode.
        #ifdef TC_DBG
        {
            wifi_power_t power = WiFi.getTxPower();
            Serial.printf("WiFi: Max TX power in STA mode %d\n", power);
        }
        #endif
        WiFi.setTxPower(WIFI_POWER_19_5dBm);

        wifiInAPMode = false;
        wifiIsOff = false;
        wifiOnNow = millis();
        wifiAPIsOff = false;  // Sic! Allows checks like if(wifiAPIsOff || wifiIsOff)

        consecutiveAPmodeFB = 0;  // Reset counter of consecutive AP-mode fall-backs

    } else {
        #ifdef TC_DBG
        Serial.println(F("Config portal running in AP-mode"));
        #endif

        {
            #ifdef TC_DBG
            int8_t power;
            esp_wifi_get_max_tx_power(&power);
            Serial.printf("WiFi: Max TX power in AP mode %d\n", power);
            #endif

            // Try to avoid "burning" the ESP when the WiFi mode
            // is "AP" and the vol knob is fully up by reducing
            // the max. transmit power.
            // The choices are:
            // WIFI_POWER_19_5dBm    = 19.5dBm
            // WIFI_POWER_19dBm      = 19dBm
            // WIFI_POWER_18_5dBm    = 18.5dBm
            // WIFI_POWER_17dBm      = 17dBm
            // WIFI_POWER_15dBm      = 15dBm
            // WIFI_POWER_13dBm      = 13dBm
            // WIFI_POWER_11dBm      = 11dBm
            // WIFI_POWER_8_5dBm     = 8.5dBm
            // WIFI_POWER_7dBm       = 7dBm     <-- proven to avoid the issues
            // WIFI_POWER_5dBm       = 5dBm
            // WIFI_POWER_2dBm       = 2dBm
            // WIFI_POWER_MINUS_1dBm = -1dBm
            WiFi.setTxPower(WIFI_POWER_7dBm);

            #ifdef TC_DBG
            esp_wifi_get_max_tx_power(&power);
            Serial.printf("WiFi: Max TX power set to %d\n", power);
            #endif
        }

        wifiInAPMode = true;
        wifiAPIsOff = false;
        wifiAPModeNow = millis();
        wifiIsOff = false;    // Sic!

        if(wifiHaveSTAConf)       // increase counter of consecutive AP-mode fall-backs
            consecutiveAPmodeFB++;    
    }

    lastConnect = millis();
}

// This must not be called if no power-saving
// timers are configured.
static void wifiOff(bool force)
{
    if(!force) {
        if( (!wifiInAPMode && wifiIsOff) ||
            (wifiInAPMode && wifiAPIsOff) ) {
            return;
        }
    }

    wm.stopWebPortal();
    wm.disconnect();
    WiFi.mode(WIFI_OFF);
}

void wifiOn(unsigned long newDelay, bool alsoInAPMode, bool deferCP)
{
    unsigned long desiredDelay;
    unsigned long Now = millis();
    
    // wifiON() is called when the user pressed (and held) "7" (with alsoInAPMode
    // TRUE) and when a time sync via NTP is issued (with alsoInAPMode FALSE).
    //
    // Holding "7" serves two purposes: To re-enable WiFi if in power save mode,
    // and to re-connect to a configured WiFi network if we failed to connect to 
    // that network at the last connection attempt. In both cases, the Config Portal
    // is started.
    //
    // The NTP-triggered call should only re-connect if we are in power-save mode
    // after being connected to a user-configured network, or if we are in AP mode
    // but the user had config'd a network. Should only be called when frozen 
    // displays are feasible (eg night hours).
    //    
    // "wifiInAPMode" only tells us our latest mode; if the configured WiFi
    // network was - for whatever reason - was not available when we
    // tried to (re)connect, "wifiInAPMode" is true.

    // At this point, wifiInAPMode reflects the state after
    // the last connection attempt.

    if(alsoInAPMode) {    // User held "7"
        
        if(wifiInAPMode) {  // We are in AP mode

            if(!wifiAPIsOff) {

                // If ON but no user-config'd WiFi network -> bail
                if(!wifiHaveSTAConf) {
                    // Best we can do is to restart the timer
                    wifiAPModeNow = Now;
                    return;
                }

                // If ON and User has config's a NW, disable WiFi at this point
                // (in hope of successful connection below)
                wifiOff(true);

            }

        } else {            // We are in STA mode

            // If WiFi is not off, check if caller wanted
            // to start the CP, and do so, if not running
            if(!wifiIsOff) {
                if(!deferCP) {
                    if(!wm.getWebPortalActive()) {
                        wm.startWebPortal();
                    }
                }
                // Restart timer
                wifiOnNow = Now;
                return;
            }

        }

    } else {      // NTP-triggered

        // If no user-config'd network - no point, bail
        if(!wifiHaveSTAConf) return;

        if(wifiInAPMode) {  // We are in AP mode (because connection failed)

            #ifdef TC_DBG
            Serial.printf("wifiOn: consecutiveAPmodeFB %d\n", consecutiveAPmodeFB);
            #endif

            // Reset counter of consecutive AP-mode fallbacks
            // after a couple of days
            if(Now - lastConnect > 4*24*60*60*1000)
                consecutiveAPmodeFB = 0;

            // Give up after so many attempts
            if(consecutiveAPmodeFB > 5)
                return;

            // Do not try to switch from AP- to STA-mode
            // if last fall-back to AP-mode was less than
            // 15 (for the first 2 attempts, then 90) minutes ago
            if(Now - lastConnect < ((consecutiveAPmodeFB <= 2) ? 15*60*1000 : 90*60*1000))
                return;

            if(!wifiAPIsOff) {

                // If ON, disable WiFi at this point
                // (in hope of successful connection below)
                wifiOff(true);

            }

        } else {            // We are in STA mode

            // If WiFi is not off, check if caller wanted
            // to start the CP, and do so, if not running
            if(!wifiIsOff) {
                if(!deferCP) {
                    if(!wm.getWebPortalActive()) {
                        wm.startWebPortal();
                    }
                }
                // Add 60 seconds to timer in case the NTP
                // request might fall off the edge
                if(origWiFiOffDelay > 0) {
                    if((Now - wifiOnNow >= wifiOffDelay) ||
                       ((wifiOffDelay - (Now - wifiOnNow)) < (60*1000))) {
                        wifiOnNow += (60*1000);
                    }
                }
                return;
            }

        }

    }

    // (Re)connect
    WiFi.mode(WIFI_MODE_STA);
    wifiConnect(deferCP);

    // Restart timers
    // Note that wifiInAPMode now reflects the
    // result of our above wifiConnect() call

    if(wifiInAPMode) {

        #ifdef TC_DBG
        Serial.println("wifiOn: in AP mode after connect");
        #endif
      
        wifiAPModeNow = Now;
        
        #ifdef TC_DBG
        if(wifiAPOffDelay > 0) {
            Serial.printf("Restarting WiFi-off timer (AP mode); delay %d\n", wifiAPOffDelay);
        }
        #endif
        
    } else {

        #ifdef TC_DBG
        Serial.println("wifiOn: in STA mode after connect");
        #endif

        if(origWiFiOffDelay != 0) {
            desiredDelay = (newDelay > 0) ? newDelay : origWiFiOffDelay;
            if((Now - wifiOnNow >= wifiOffDelay) ||                    // If delay has run out, or
               (wifiOffDelay - (Now - wifiOnNow))  < desiredDelay) {   // new delay exceeds remaining delay:
                wifiOffDelay = desiredDelay;                           // Set new timer delay, and
                wifiOnNow = Now;                                       // restart timer
                #ifdef TC_DBG
                Serial.printf("Restarting WiFi-off timer; delay %d\n", wifiOffDelay);
                #endif
            }
        }

    }
}

// Check if a longer interruption due to a re-connect is to
// be expected when calling wifiOn(true, xxx).
bool wifiOnWillBlock()
{
    if(wifiInAPMode) {  // We are in AP mode
        if(!wifiAPIsOff) {
            if(!wifiHaveSTAConf) {
                return false;
            }
        }
    } else {            // We are in STA mode
        if(!wifiIsOff) return false;
    }

    return true;
}

void wifiStartCP()
{
    if(wifiInAPMode || wifiIsOff)
        return;

    wm.startWebPortal();
}

// This is called when the WiFi config changes, so it has
// nothing to do with our settings here. Despite that,
// we write out our config file so that when the user initially
// configures WiFi, a default settings file exists upon reboot.
// Also, this triggers a reboot, so if the user entered static
// IP data, it becomes active after this reboot.
static void saveConfigCallback()
{
    shouldSaveConfig = 1;
}

// This is the callback from the actual Params page. In this
// case, we really read out the server parms and save them.
static void saveParamsCallback()
{
    shouldSaveConfig = 2;
}

// This is called before a firmware updated is initiated.
// Disable WiFi-off-timers.
static void preUpdateCallback()
{
    wifiAPOffDelay = 0;
    origWiFiOffDelay = 0;
}

// Grab static IP parameters from WiFiManager's server.
// Since there is no public method for this, we steal
// the html form parameters in this callback.
static void preSaveConfigCallback()
{
    char ipBuf[20] = "";
    char gwBuf[20] = "";
    char snBuf[20] = "";
    char dnsBuf[20] = "";
    bool invalConf = false;

    #ifdef TC_DBG
    Serial.println("preSaveConfigCallback");
    #endif

    // clear as strncpy might leave us unterminated
    memset(ipBuf, 0, 20);
    memset(gwBuf, 0, 20);
    memset(snBuf, 0, 20);
    memset(dnsBuf, 0, 20);

    if(wm.server->arg(FPSTR(S_ip)) != "") {
        strncpy(ipBuf, wm.server->arg(FPSTR(S_ip)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_gw)) != "") {
        strncpy(gwBuf, wm.server->arg(FPSTR(S_gw)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_sn)) != "") {
        strncpy(snBuf, wm.server->arg(FPSTR(S_sn)).c_str(), 19);
    } else invalConf |= true;
    if(wm.server->arg(FPSTR(S_dns)) != "") {
        strncpy(dnsBuf, wm.server->arg(FPSTR(S_dns)).c_str(), 19);
    } else invalConf |= true;

    #ifdef TC_DBG
    if(strlen(ipBuf) > 0) {
        Serial.printf("IP:%s / SN:%s / GW:%s / DNS:%s\n", ipBuf, snBuf, gwBuf, dnsBuf);
    } else {
        Serial.println("Static IP unset, using DHCP");
    }
    #endif

    if(!invalConf && isIp(ipBuf) && isIp(gwBuf) && isIp(snBuf) && isIp(dnsBuf)) {

        #ifdef TC_DBG
        Serial.println("All IPs valid");
        #endif

        strcpy(ipsettings.ip, ipBuf);
        strcpy(ipsettings.gateway, gwBuf);
        strcpy(ipsettings.netmask, snBuf);
        strcpy(ipsettings.dns, dnsBuf);

        shouldSaveIPConfig = true;

    } else {

        #ifdef TC_DBG
        if(strlen(ipBuf) > 0) {
            Serial.println("Invalid IP");
        }
        #endif

        shouldDeleteIPConfig = true;

    }
}

static void setupStaticIP()
{
    IPAddress ip;
    IPAddress gw;
    IPAddress sn;
    IPAddress dns;

    if(strlen(ipsettings.ip) > 0 &&
        isIp(ipsettings.ip) &&
        isIp(ipsettings.gateway) &&
        isIp(ipsettings.netmask) &&
        isIp(ipsettings.dns)) {

        ip = stringToIp(ipsettings.ip);
        gw = stringToIp(ipsettings.gateway);
        sn = stringToIp(ipsettings.netmask);
        dns = stringToIp(ipsettings.dns);

        wm.setSTAStaticIPConfig(ip, gw, sn, dns);
    }
}

void updateConfigPortalValues()
{
    const char custHTMLSel[] = " selected";
    int t = atoi(settings.autoRotateTimes);
    int tb = atoi(settings.beep);
    int tnm = atoi(settings.autoNMPreset);
    #ifdef TC_HAVESPEEDO
    int tt = atoi(settings.speedoType);
    char spTyBuf[8];
    #endif

    // Make sure the settings form has the correct values

    strcpy(beepaintCustHTML, beepCustHTML1);          // beep mode
    strcat(beepaintCustHTML, settings.beep);
    strcat(beepaintCustHTML, beepCustHTML2);
    if(tb == 0) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, beepCustHTML3);
    if(tb == 1) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, beepCustHTML4);
    if(tb == 2) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, beepCustHTML5);
    if(tb == 3) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, beepCustHTML6);
    strcat(beepaintCustHTML, aintCustHTML1);          // aint
    strcat(beepaintCustHTML, settings.autoRotateTimes);
    strcat(beepaintCustHTML, aintCustHTML2);
    if(t == 0) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML3);
    if(t == 1) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML4);
    if(t == 2) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML5);
    if(t == 3) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML6);
    if(t == 4) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML7);
    if(t == 5) strcat(beepaintCustHTML, custHTMLSel);
    strcat(beepaintCustHTML, aintCustHTML8);

    custom_hostName.setValue(settings.hostName, 31);
    custom_wifiConTimeout.setValue(settings.wifiConTimeout, 2);
    custom_wifiConRetries.setValue(settings.wifiConRetries, 2);
    custom_wifiOffDelay.setValue(settings.wifiOffDelay, 2);
    custom_wifiAPOffDelay.setValue(settings.wifiAPOffDelay, 2);
    custom_ntpServer.setValue(settings.ntpServer, 63);
    custom_timeZone.setValue(settings.timeZone, 63);

    custom_timeZone1.setValue(settings.timeZoneDest, 63);
    custom_timeZone2.setValue(settings.timeZoneDep, 63);
    custom_timeZoneN1.setValue(settings.timeZoneNDest, DISP_LEN);
    custom_timeZoneN2.setValue(settings.timeZoneNDep, DISP_LEN);

    strcpy(anmCustHTML, anmCustHTML1);
    strcat(anmCustHTML, settings.autoNMPreset);
    strcat(anmCustHTML, anmCustHTML2);
    if(tnm == 10) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML3);
    if(tnm == 0) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML4);
    if(tnm == 1) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML5);
    if(tnm == 2) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML6);
    if(tnm == 3) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML7);
    if(tnm == 4) strcat(anmCustHTML, custHTMLSel);
    strcat(anmCustHTML, anmCustHTML8);

    custom_autoNMOn.setValue(settings.autoNMOn, 2);
    custom_autoNMOff.setValue(settings.autoNMOff, 2);
    #ifdef TC_HAVELIGHT
    custom_lxLim.setValue(settings.luxLimit, 6);
    #endif
    
    #ifdef EXTERNAL_TIMETRAVEL_IN
    custom_ettDelay.setValue(settings.ettDelay, 5);
    #endif

    #ifdef TC_HAVETEMP
    custom_tempOffs.setValue(settings.tempOffs, 4);
    #endif

    #ifdef TC_HAVESPEEDO
    strcpy(spTyCustHTML, spTyCustHTML1);
    strcat(spTyCustHTML, settings.speedoType);
    strcat(spTyCustHTML, spTyCustHTML2);
    strcat(spTyCustHTML, spTyOptP1);
    strcat(spTyCustHTML, "99'");
    if(tt == 99) strcat(spTyCustHTML, custHTMLSel);
    strcat(spTyCustHTML, ">None");
    strcat(spTyCustHTML, spTyOptP3);
    for (int i = SP_MIN_TYPE; i < SP_NUM_TYPES; i++) {
        strcat(spTyCustHTML, spTyOptP1);
        sprintf(spTyBuf, "%d'", i);
        strcat(spTyCustHTML, spTyBuf);
        if(tt == i) strcat(spTyCustHTML, custHTMLSel);
        strcat(spTyCustHTML, ">");
        strcat(spTyCustHTML, dispTypeNames[i]);
        strcat(spTyCustHTML, spTyOptP3);
    }
    strcat(spTyCustHTML, spTyCustHTMLE);
    custom_speedoBright.setValue(settings.speedoBright, 2);
    custom_speedoFact.setValue(settings.speedoFact, 3);
    #ifdef TC_HAVETEMP
    custom_tempBright.setValue(settings.tempBright, 2);
    #endif
    #endif

    #ifdef TC_HAVEMQTT
    custom_mqttServer.setValue(settings.mqttServer, 79);
    custom_mqttUser.setValue(settings.mqttUser, 63);
    custom_mqttTopic.setValue(settings.mqttTopic, 63);
    #endif

    #ifdef TC_NOCHECKBOXES  // Standard text boxes: -------

    custom_ttrp.setValue(settings.timesPers, 1);
    custom_alarmRTC.setValue(settings.alarmRTC, 1);
    custom_playIntro.setValue(settings.playIntro, 1);
    custom_mode24.setValue(settings.mode24, 1);
    custom_wifiPRe.setValue(settings.wifiPRetry, 1);
    custom_dtNmOff.setValue(settings.dtNmOff, 1);
    custom_ptNmOff.setValue(settings.ptNmOff, 1);
    custom_ltNmOff.setValue(settings.ltNmOff, 1);
    #ifdef TC_HAVELIGHT
    custom_uLS.setValue(settings.useLight, 1);
    #endif
    #ifdef TC_HAVETEMP
    custom_tempUnit.setValue(settings.tempUnit, 1);
    #endif
    #ifdef TC_HAVESPEEDO
    #ifdef TC_HAVEGPS
    custom_useGPSS.setValue(settings.useGPSSpeed, 1);
    #endif
    #ifdef TC_HAVETEMP
    custom_useDpTemp.setValue(settings.dispTemp, 1);
    custom_tempOffNM.setValue(settings.tempOffNM, 1);
    #endif
    #endif
    #ifdef FAKE_POWER_ON
    custom_fakePwrOn.setValue(settings.fakePwrOn, 1);
    #endif
    //#ifdef EXTERNAL_TIMETRAVEL_IN
    //custom_ettLong.setValue(settings.ettLong, 1);
    //#endif
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    custom_useETTO.setValue(settings.useETTO, 1);
    #endif
    custom_playTTSnd.setValue(settings.playTTsnds, 1);
    #ifdef TC_HAVEMQTT
    custom_useMQTT.setValue(settings.useMQTT, 1);
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    custom_pubMQTT.setValue(settings.pubMQTT, 1);
    #endif
    #endif
    custom_shuffle.setValue(settings.shuffle, 1);
    custom_CfgOnSD.setValue(settings.CfgOnSD, 1);
    //custom_sdFrq.setValue(settings.sdFreq, 1);

    #else   // For checkbox hack --------------------------

    setCBVal(&custom_ttrp, settings.timesPers);
    setCBVal(&custom_alarmRTC, settings.alarmRTC);
    setCBVal(&custom_playIntro, settings.playIntro);
    setCBVal(&custom_mode24, settings.mode24);
    setCBVal(&custom_wifiPRe, settings.wifiPRetry);
    setCBVal(&custom_dtNmOff, settings.dtNmOff);
    setCBVal(&custom_ptNmOff, settings.ptNmOff);
    setCBVal(&custom_ltNmOff, settings.ltNmOff);
    #ifdef TC_HAVELIGHT
    setCBVal(&custom_uLS, settings.useLight);
    #endif
    #ifdef TC_HAVETEMP
    setCBVal(&custom_tempUnit, settings.tempUnit);
    #endif
    #ifdef TC_HAVESPEEDO
    #ifdef TC_HAVEGPS
    setCBVal(&custom_useGPSS, settings.useGPSSpeed);
    #endif
    #ifdef TC_HAVETEMP
    setCBVal(&custom_useDpTemp, settings.dispTemp);
    setCBVal(&custom_tempOffNM, settings.tempOffNM);
    #endif
    #endif
    #ifdef FAKE_POWER_ON
    setCBVal(&custom_fakePwrOn, settings.fakePwrOn);
    #endif
    //#ifdef EXTERNAL_TIMETRAVEL_IN
    //setCBVal(&custom_ettLong, settings.ettLong);
    //#endif
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    setCBVal(&custom_useETTO, settings.useETTO);
    #endif
    setCBVal(&custom_playTTSnd, settings.playTTsnds);
    #ifdef TC_HAVEMQTT
    setCBVal(&custom_useMQTT, settings.useMQTT);
    #ifdef EXTERNAL_TIMETRAVEL_OUT
    setCBVal(&custom_pubMQTT, settings.pubMQTT);
    #endif
    #endif
    setCBVal(&custom_shuffle, settings.shuffle);
    setCBVal(&custom_CfgOnSD, settings.CfgOnSD);
    //setCBVal(&custom_sdFrq, settings.sdFreq);

    #endif // ---------------------------------------------    
}

int wifi_getStatus()
{
    switch(WiFi.getMode()) {
      case WIFI_MODE_STA:
          return (int)WiFi.status();
      case WIFI_MODE_AP:
          return 0x10000;     // AP MODE
      case WIFI_MODE_APSTA:
          return 0x10003;     // AP/STA MIXED
      case WIFI_MODE_NULL:
          return 0x10001;     // OFF
    }

    return 0x10002;           // UNKNOWN
}

bool wifi_getIP(uint8_t& a, uint8_t& b, uint8_t& c, uint8_t& d)
{
    IPAddress myip;

    switch(WiFi.getMode()) {
      case WIFI_MODE_STA:
          myip = WiFi.localIP();
          break;
      case WIFI_MODE_AP:
      case WIFI_MODE_APSTA:
          myip = WiFi.softAPIP();
          break;
      default:
          a = b = c = d = 0;
          return true;
    }

    a = myip[0];
    b = myip[1];
    c = myip[2];
    d = myip[3];

    return true;
}

void wifi_getMAC(char *buf)
{
    byte myMac[6];
    
    WiFi.macAddress(myMac);
    sprintf(buf, "%02x%02x%02x%02x%02x%02x", myMac[0], myMac[1], myMac[2], myMac[3], myMac[4], myMac[5]); 
}

// Check if String is a valid IP address
static bool isIp(char *str)
{
    int segs = 0;
    int digcnt = 0;
    int num = 0;

    while(*str != '\0') {

        if(*str == '.') {

            if(!digcnt || (++segs == 4))
                return false;

            num = digcnt = 0;
            str++;
            continue;

        } else if((*str < '0') || (*str > '9')) {

            return false;

        }

        if((num = (num * 10) + (*str - '0')) > 255)
            return false;

        digcnt++;
        str++;
    }

    return true;
}

// IPAddress to string
static void ipToString(char *str, IPAddress ip)
{
    sprintf(str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

// String to IPAddress
static IPAddress stringToIp(char *str)
{
    int ip1, ip2, ip3, ip4;

    sscanf(str, "%d.%d.%d.%d", &ip1, &ip2, &ip3, &ip4);

    return IPAddress(ip1, ip2, ip3, ip4);
}

/*
 * Read parameter from server, for customhmtl input
 */
static void getParam(String name, char *destBuf, size_t length)
{
    memset(destBuf, 0, length+1);
    if(wm.server->hasArg(name)) {
        strncpy(destBuf, wm.server->arg(name).c_str(), length);
    }
}

static bool myisspace(char mychar)
{
    return (mychar == ' ' || mychar == '\n' || mychar == '\t' || mychar == '\v' || mychar == '\f' || mychar == '\r');
}

static bool myisgoodchar(char mychar)
{
    return ((mychar >= '0' && mychar <= '9') || (mychar >= 'a' && mychar <= 'z') || (mychar >= 'A' && mychar <= 'Z') || mychar == '-');
}

static bool myisgoodchar2(char mychar)
{
    return ((mychar == ' '));
}

static char* strcpytrim(char* destination, const char* source, bool doFilter)
{
    char *ret = destination;
    
    do {
        if(!myisspace(*source) && (!doFilter || myisgoodchar(*source))) *destination++ = *source;
        source++;
    } while(*source);
    
    *destination = 0;
    
    return ret;
}

static char* strcpyfilter(char *destination, const char *source)
{
    char *ret = destination;
    
    do {
        if(myisgoodchar(*source) || myisgoodchar2(*source)) *destination++ = *source;
        source++;
    } while(*source);
    
    *destination = 0;
    
    return ret;
}

static void mystrcpy(char *sv, WiFiManagerParameter *el)
{
    strcpy(sv, el->getValue());
}

#ifndef TC_NOCHECKBOXES
static void strcpyCB(char *sv, WiFiManagerParameter *el)
{
    strcpy(sv, ((int)atoi(el->getValue()) > 0) ? "1" : "0");
}

static void setCBVal(WiFiManagerParameter *el, char *sv)
{
    const char makeCheck[] = "1' checked a='";
    
    el->setValue(((int)atoi(sv) > 0) ? makeCheck : "1", 14);
}
#endif

#ifdef TC_HAVEMQTT
static void strcpyutf8(char *dst, const char *src, unsigned int len)
{
    strncpy(dst, src, len - 1);
    dst[len - 1] = 0;
}

static int16_t filterOutUTF8(char *src, char *dst)
{
    int i, j, slen = strlen(src);
    unsigned char c, d, e, f;

    for(i = 0, j = 0; i < slen; i++) {
        c = (unsigned char)src[i];
        if(c >= 32 && c < 127 - 1) {    // 127 = °, non-std, skip
            if(c >= 'a' && c <= 'z') c &= ~0x20;
            dst[j++] = c;
        } else if(c >= 194 && c < 224 && (i+1) < slen) {
            d = (unsigned char)src[i+1];
            if(d > 127 && d < 192) i++;
        } else if(c < 240 && (i+2) < slen) {
            d = (unsigned char)src[i+1];
            e = (unsigned char)src[i+2];
            if(d > 127 && d < 192 && 
               e > 127 && e < 192) 
                i+=2;
        } else if(c < 245 && (i+3) < slen) {
            d = (unsigned char)src[i+1];
            e = (unsigned char)src[i+2];
            f = (unsigned char)src[i+3];
            if(d > 127 && d < 192 && 
               e > 127 && e < 192 && 
               f > 127 && f < 192)
                i+=3;
        }
    }
    dst[j] = 0;

    return j;
}

static void mqttLooper()
{
    ntp_loop();
    audio_loop();
    #ifdef TC_HAVEGPS
    gps_loop();   // does not call any other loops
    #endif
}

static void mqttCallback(char *topic, byte *payload, unsigned int length)
{
    int i = 0, j, ml = (length <= 255) ? length : 255;
    char tempBuf[256];
    static const char *cmdList[] = {
      "TIMETRAVEL",       // 0
      "RETURN",           // 1
      "ALARM_ON",         // 2
      "ALARM_OFF",        // 3
      "NIGHTMODE_ON",     // 4
      "NIGHTMODE_OFF",    // 5
      "MP_SHUFFLE_ON",    // 6
      "MP_SHUFFLE_OFF",   // 7
      "MP_PLAY",          // 8
      "MP_STOP",          // 9
      "MP_NEXT",          // 10
      "MP_PREV",          // 11
      "BEEP_OFF",         // 12
      "BEEP_ON",          // 13
      "BEEP_30",          // 14
      "BEEP_60",          // 15
      NULL
    };

    if(!length) return;

    if(!strcmp(topic, "bttf/tcd/cmd")) {

        // Not taking commands under these circumstances:
        if(!FPBUnitIsOn || menuActive   || startup || 
           timeTravelP0 || timeTravelP1 || timeTravelRE)
            return;

        memcpy(tempBuf, (const char *)payload, ml);
        tempBuf[ml] = 0;

        for(j = 0; j < ml; j++) {
            if(tempBuf[j] >= 'a' && tempBuf[j] <= 'z') tempBuf[j] &= ~0x20;
        }

        while(cmdList[i]) {
            j = strlen(cmdList[i]);
            if((ml >= j) && !strncmp((const char *)tempBuf, cmdList[i], j)) {
                break;
            }
            i++;          
        }

        if(!cmdList[i]) return;

        switch(i) {
        case 0:
            #ifdef EXTERNAL_TIMETRAVEL_IN
            isEttKeyPressed = true;
            #endif
            break;
        case 1:
            #ifdef EXTERNAL_TIMETRAVEL_IN
            isEttKeyHeld = true;
            #endif
            break;
        case 2:
            alarmOn();
            break;
        case 3:
            alarmOff();
            break;
        case 4:
            nightModeOn();
            manualNightMode = 1;
            manualNMNow = millis();
            break;
        case 5:
            nightModeOff();
            manualNightMode = 0;
            manualNMNow = millis();
            break;
        case 6:
        case 7:
            if(haveMusic) mp_makeShuffle((i == 6));
            break;
        case 8:    
            if(haveMusic) mp_play();
            break;
        case 9:
            if(haveMusic) mp_stop();
            break;
        case 10:
            if(haveMusic) mp_next(mpActive);
            break;
        case 11:
            if(haveMusic) mp_prev(mpActive);
            break;
        case 12:
        case 13:
        case 14:
        case 15:
            setBeepMode(i-12);
            break;
        }
            
    } else if(!strcmp(topic, settings.mqttTopic)) {

        memcpy(tempBuf, (const char *)payload, ml);
        tempBuf[ml] = 0;

        j = filterOutUTF8(tempBuf, mqttMsg);
        
        mqttIdx = 0;
        mqttMaxIdx = (j > DISP_LEN) ? j : -1;
        mqttDisp = 1;
        mqttOldDisp = 0;
        mqttST = haveMQTTaudio;
    
        #ifdef TC_DBG
        Serial.printf("MQTT: Message about [%s]: %s\n", topic, mqttMsg);
        #endif
    }
}

#ifdef TC_DBG
#define MQTT_FAILCOUNT 6 //120
#else
#define MQTT_FAILCOUNT 120
#endif

static void mqttPing()
{
    switch(mqttClient.pstate()) {
    case PING_IDLE:
        if(WiFi.status() == WL_CONNECTED) {
            if(!mqttPingNow || (millis() - mqttPingNow > mqttPingInt)) {
                mqttPingNow = millis();
                if(!mqttClient.sendPing()) {
                    // Mostly fails for internal reasons;
                    // skip ping test in that case
                    mqttDoPing = false;
                    mqttPingDone = true;  // allow mqtt-connect attempt
                }
            }
        }
        break;
    case PING_PINGING:
        if(mqttClient.pollPing()) {
            mqttPingDone = true;          // allow mqtt-connect attempt
            mqttPingNow = 0;
            mqttPingsExpired = 0;
            mqttPingInt = MQTT_SHORT_INT; // Overwritten on fail in reconnect
            // Delay re-connection for 5 seconds after first ping echo
            mqttReconnectNow = millis() - (mqttReconnectInt - 5000);
        } else if(millis() - mqttPingNow > 5000) {
            mqttClient.cancelPing();
            mqttPingNow = millis();
            mqttPingsExpired++;
            mqttPingInt = MQTT_SHORT_INT * (1 << (mqttPingsExpired / MQTT_FAILCOUNT));
            mqttReconnFails = 0;
        }
        break;
    } 
}

static bool mqttReconnect(bool force)
{
    bool success = false;

    if(useMQTT && (WiFi.status() == WL_CONNECTED)) {

        if(!mqttClient.connected()) {
    
            if(force || !mqttReconnectNow || (millis() - mqttReconnectNow > mqttReconnectInt)) {

                #ifdef TC_DBG
                Serial.println("MQTT: Attempting to (re)connect");
                #endif
    
                if(strlen(mqttUser)) {
                    success = mqttClient.connect(settings.hostName, mqttUser, strlen(mqttPass) ? mqttPass : NULL);
                } else {
                    success = mqttClient.connect(settings.hostName);
                }
    
                mqttReconnectNow = millis();
                
                if(!success) {
                    mqttRestartPing = true;  // Force PING check before reconnection attempt
                    mqttReconnFails++;
                    if(mqttDoPing) {
                        mqttPingInt = MQTT_SHORT_INT * (1 << (mqttReconnFails / MQTT_FAILCOUNT));
                    } else {
                        mqttReconnectInt = MQTT_SHORT_INT * (1 << (mqttReconnFails / MQTT_FAILCOUNT));
                    }
                    #ifdef TC_DBG
                    Serial.printf("MQTT: Failed to reconnect (%d)\n", mqttReconnFails);
                    #endif
                } else {
                    mqttReconnFails = 0;
                    mqttReconnectInt = MQTT_SHORT_INT;
                    #ifdef TC_DBG
                    Serial.println("MQTT: Connected to broker, waiting for CONNACK");
                    #endif
                }
    
                return success;
            } 
        }
    }
      
    return true;
}

static void mqttSubscribe()
{
    // Meant only to be called when connected!
    if(!mqttSubAttempted) {
        if(!mqttClient.subscribe("bttf/tcd/cmd", settings.mqttTopic)) {
            if(!mqttClient.subscribe("bttf/tcd/cmd")) {
                Serial.println("MQTT: Failed to subscribe to all topics");
            } else {
                Serial.println("MQTT: Failed to subscribe to user topic");
            }
        } else {
            #ifdef TC_DBG
            Serial.println("MQTT: Subscribed to all topics");
            #endif
        }
        mqttSubAttempted = true;
    }
}

bool mqttState()
{
    return (useMQTT && mqttClient.connected());
}

void mqttPublish(const char *topic, const char *pl, unsigned int len)
{
    if(useMQTT) {
        mqttClient.publish(topic, (uint8_t *)pl, len, false);
    }
}

#endif
