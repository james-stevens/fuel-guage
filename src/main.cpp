#define LGFX_USE_V1
#include <esp_wifi.h>
#include <esp_bt.h>
#include <driver/gpio.h>
#include <LovyanGFX.hpp>

//-------------------------------------------------------------
// This switches on/off `monitor` messages in the `Serial` port
// set of `0` for prodution, `1` for debugging
const int DEBUG_WIRING = 1;

//-------------------------------------------------------------
// KEY button toggles the debug screen. 0 is the BOOT button.
const int DEBUG_BUTTON_PIN = 14;

//-------------------------------------------------------------
// These numbers you will prob need to change to match your car
//-------------------------------------------------------------
const float EMPTY_OHMS = 167.0f;
const float FULL_OHMS  = 29.0f;
// if outside the range above, by this amount, indicate an error
// i.e. light the warning light and put the needle below "E"
const float ERROR_GAP_OHMS  = 10.0f;
// Open circuit reads about 7561 Ω (calibrated ADC tops out near 3.162 V).
// These readings are not averaged. Three in a row is a cable break.
const float OPEN_CIRCUIT_OHMS = 7500.0f;

// Set this to a value more than 0.01 to fake a reading from the car's sender
const float FAKE_CAR_READING = 0.0;

//-------------------------------------------------------------
// Lamp turns on below TANK_LOW_ON_ANGLE and stays on until the
// needle reaches TANK_LOW_OFF_ANGLE, so it does not flicker.
// ERROR_ANGLE is below the on-angle, so a fault still lights it.
const int TANK_LOW_ON_ANGLE  = 335;
const int TANK_LOW_OFF_ANGLE = 338;

//-------------------------------------------------------------
// Set LIGHTS_GPIO_PIN to your GPIO pin number when the lights divider is wired.
// Leave it at 0 until then: an unconnected pin floats, so the
// feature stays off. For wiring see README
//-------------------------------------------------------------
const int LIGHTS_GPIO_PIN = 16;

//-------------------------------------------------------------
// Tweak these numbers to adjust the screen brightness
//-------------------------------------------------------------
// in theory the max brightness is 255,
// but mine glitched if I used 255 & 250 looks no different
const int BRIGHTNESS_LIGHTS_OFF = 250;
const int BRIGHTNESS_LIGHTS_ON = 60;
// Fade the backlight between those two over this long.
// Timed with millis() inside loop(), so a slow loop still finishes on time.
const int FADE_MS = 1000;
// analogReadMilliVolts() hysteresis on the divider output.
// Lights ON above LIGHTS_ON_MV, OFF below LIGHTS_OFF_MV, else hold.
const int LIGHTS_ON_MV  = 1600;
const int LIGHTS_OFF_MV = 1200;
// check_lights() runs every loop (~150 ms). The new state must be
// seen this many times in a row (~0.6 s) before the brightness changes.
const int STABLE_LOOPS = 4;


// set this to zero to disable the start-up animation
const int WITH_START_UP_ANIMATION = 1;

// readings kept from the sender. The filtered value drops the highest
// and the lowest, then averages the rest (8 once the buffer is full).
const int   SENDER_AVG_SAMPLES = 10;


class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ST7789 _panel_instance;
  lgfx::Bus_Parallel8 _bus_instance;
  lgfx::Light_PWM _light_instance;

public:
  LGFX(void) {
    {
      auto cfg = _bus_instance.config();
      cfg.freq_write = 20000000;
      cfg.pin_wr = 8;
      cfg.pin_rd = 9;
      cfg.pin_rs = 7;
      cfg.pin_d0 = 39;
      cfg.pin_d1 = 40;
      cfg.pin_d2 = 41;
      cfg.pin_d3 = 42;
      cfg.pin_d4 = 45;
      cfg.pin_d5 = 46;
      cfg.pin_d6 = 47;
      cfg.pin_d7 = 48;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }
    {
      auto cfg = _panel_instance.config();
      cfg.pin_cs = 6;
      cfg.pin_rst = 5;
      cfg.pin_busy = -1;
      cfg.memory_width = 240;
      cfg.memory_height = 320;
      cfg.panel_width = 170;
      cfg.panel_height = 320;
      cfg.offset_x = 35;
      cfg.offset_y = 0;
      cfg.offset_rotation = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits = 1;
      cfg.readable = false;
      cfg.invert = true;
      cfg.rgb_order = false;
      cfg.dlen_16bit = false;
      cfg.bus_shared = false;
      _panel_instance.config(cfg);
    }
    {
      auto cfg = _light_instance.config();
      cfg.pin_bl = 38;
      cfg.invert = false;
      cfg.freq = 12000;
      cfg.pwm_channel = 7;
      _light_instance.config(cfg);
      _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
  }
};


#include "light_off_small.h"
#include "light_on_small.h"

LGFX tft;
LGFX_Sprite needle(&tft);

// These numbers are tied to the background image
const int EMPTY_ANGLE = 315;
const int FULL_ANGLE  = 402;
const int ERROR_ANGLE = 305;

const int MIN_ANGLE = min(EMPTY_ANGLE, FULL_ANGLE);
const int MAX_ANGLE = max(EMPTY_ANGLE, FULL_ANGLE);
const float MIN_OHMS = min(EMPTY_OHMS, FULL_OHMS) - ERROR_GAP_OHMS;
const float MAX_OHMS = max(EMPTY_OHMS, FULL_OHMS) + ERROR_GAP_OHMS;

const float ERROR_OHMS = -1.0;


// Voltage divider: 3.3 V -- 330 Ω -- ADC pi (GPIO-1) -- sender -- GND
// (choose series resistor roughly equal to mid-range sender resistance)
const int   ADC_PIN = 1;
const float SERIES_R = 330.0f;
const float VCC = 3.3f;

// The 330 Ω pull-up is in the harness. A bare GPIO1 floats around
// 0.2-0.4 V and looks near-full. About once a second the pad pull-up
// is switched on. The calibrated ADC tops out near 3.16 V, so the rail
// guard (within 50 mV of 3.3 V) never sees that float. A connected
// sender is at most about 1.2 V at empty (167 Ω with the 330 Ω divider),
// and the 45 kΩ pull-up in parallel lifts it by under 1%. 2500 mV sits
// in the gap. Above PROBE_FAIL_MV is a failed probe. Three in a row is
// ERR. One pass clears it. 300 µs is many time constants of that pull-up.
const unsigned long PROBE_MS = 1000;
const int PROBE_SETTLE_US = 300;
const int PROBE_FAIL_MV = 2500;
const int PROBE_FAILS = 3;

// One sample per loop. readFuelOhms() fills the sender fields.
struct Readings {
	float senderVolts;
	float senderOhms;
	float avgOhms;
	int lightsMv;
	int angle;
	int probeMv;
	int probeFails;
};

Readings readings;
bool probeErr = false;
int lastAngle = -999;
bool lowFuel = false;
bool lightsOn = false;
int brightness = BRIGHTNESS_LIGHTS_OFF;
int targetBrightness = BRIGHTNESS_LIGHTS_OFF;
int fadeFrom = BRIGHTNESS_LIGHTS_OFF;
int fadeTo = BRIGHTNESS_LIGHTS_OFF;
unsigned long fadeStartMs = 0;
bool debugScreen = false;
bool debugButtonDown = false;
char debugShown[4][24];



bool voltsOnRail(float volts)
{
	// Within 50 mV of 3.3 V. The ohms guard only; the probe uses PROBE_FAIL_MV.
	return volts >= (VCC - 0.05f);
}



float readFuelOhms()
{
	// Factory calibration. raw/4095*VCC reads about 80 mV low.
	readings.senderVolts = analogReadMilliVolts(ADC_PIN) / 1000.0f;
	if (readings.senderVolts < 0.01f) {
		readings.senderOhms = 0;
		return 0;
		}
	// On the top rail the divider is ~0 and ohms goes negative or infinite.
	if (voltsOnRail(readings.senderVolts)) {
		readings.senderOhms = 1.0e6f;
		return readings.senderOhms;
		}
	readings.senderOhms = SERIES_R * (readings.senderVolts / (VCC - readings.senderVolts));
	return readings.senderOhms;
}



void probeSenderPin()
{
static unsigned long lastMs = 0;
unsigned long now;
int mv;

	now = millis();
	if ((now - lastMs) < PROBE_MS) return;
	lastMs = now;

	// platformio.ini does not pin a core. This project resolves
	// espressif32 8.0.0: Arduino-ESP32 3.3.12 on IDF 5.5.5.
	// In that core analogReadMilliVolts() calls __analogInit() only
	// while the pin is not already on the ADC oneshot bus. That init,
	// like pinMode(pin, ANALOG), applies gpio_config() with both pulls
	// off. pinMode(INPUT_PULLUP) would also drop the ADC bus through
	// periman, so the next analogReadMilliVolts() would attach the pin
	// again and clear the pull before the sample.
	// The throwaway read attaches the channel first. gpio_set_pull_mode()
	// (gpio_pullup_en underneath) then sets the pad pull without going
	// through pinMode. On ESP32-S3 that writes the IO_MUX pull, because
	// SOC_GPIO_SUPPORT_RTC_INDEPENDENT is set, and that is the pad the
	// ADC samples. The real sample does not re-enter __analogInit(), so
	// the pull stays on for it. The pull is turned off before the
	// filtered reading, and this sample is not stored in the average.
	(void)analogReadMilliVolts(ADC_PIN);
	gpio_set_pull_mode((gpio_num_t)ADC_PIN, GPIO_PULLUP_ONLY);
	delayMicroseconds(PROBE_SETTLE_US);
	mv = (int)analogReadMilliVolts(ADC_PIN);
	gpio_set_pull_mode((gpio_num_t)ADC_PIN, GPIO_FLOATING);

	readings.probeMv = mv;
	if (mv > PROBE_FAIL_MV) {
		if (readings.probeFails < PROBE_FAILS) readings.probeFails++;
		if (readings.probeFails >= PROBE_FAILS) probeErr = true;
		}
	else {
		readings.probeFails = 0;
		probeErr = false;
		}
}



float filteredOhms() 
{
static float buf[SENDER_AVG_SAMPLES];
static int idx = 0;
static bool filled = false;
static int openStreak = 0;
float raw;

	// Once a second. The probe sample stays out of buf[].
	probeSenderPin();
	raw = readFuelOhms();

	// Open-circuit samples stay out of the average. The first good
	// reading is filtered with whatever was stored before the break.
	if (raw >= OPEN_CIRCUIT_OHMS) {
		if (openStreak < 3) openStreak++;
		}
	else {
		openStreak = 0;
		buf[idx] = raw;
		idx = (idx + 1) % SENDER_AVG_SAMPLES;
		if (idx == 0) filled = true;
		}

	int n = filled ? SENDER_AVG_SAMPLES : idx;
	float ohms = 0;
	if (n > 0) {
		float sum = 0;
		float lo = buf[0];
		float hi = buf[0];
		for (int i = 0; i < n; i++) {
			sum += buf[i];
			if (buf[i] < lo) lo = buf[i];
			if (buf[i] > hi) hi = buf[i];
			}
		// Plain mean until a high and a low can be dropped. A full
		// buffer divides by 8.
		if (n < 3) ohms = sum / n;
		else ohms = (sum - hi - lo) / (n - 2);
		}

	if (FAKE_CAR_READING > 0.01) {
		readings.avgOhms = FAKE_CAR_READING;
		return FAKE_CAR_READING;
		}

	// Empty buffer (cable open from power-on), three raw readings on
	// the ADC rail, three failed pull-up probes, or a filtered value
	// outside the sender range.
	if (probeErr||(n == 0)||(openStreak >= 3)||(ohms < MIN_OHMS)||(ohms > MAX_OHMS)) {
		readings.avgOhms = ERROR_OHMS;
		return ERROR_OHMS;
		}

	readings.avgOhms = ohms;
	return ohms;
}



int ohmsToAngle(float ohms) 
{
	if (ohms < 0) return ERROR_ANGLE;

	int angle = map(ohms * 10, EMPTY_OHMS * 10, FULL_OHMS * 10,
				  EMPTY_ANGLE, FULL_ANGLE);

	return constrain(angle, MIN_ANGLE, MAX_ANGLE);
}



void updateLowFuel(int angle)
{
	if (angle < TANK_LOW_ON_ANGLE) lowFuel = true;
	else if (angle >= TANK_LOW_OFF_ANGLE) lowFuel = false;
}


void drawNeedle(int angle)
{
	tft.pushImage(0, 0, 320, 170, (lowFuel?light_on_small:light_off_small));
	needle.pushRotateZoom(160, 60 + 180, angle, 1.0f, 1.0f, TFT_TRANSPARENT);
}


void startupSweep() 
{
	// Warning lamp stays off for the whole sweep, including the bounce.
	for (int a = EMPTY_ANGLE; a <= FULL_ANGLE; a += 2) {
		tft.pushImage(0, 0, 320, 170, light_off_small);
		needle.pushRotateZoom(160, 60 + 180, a, 1.0f, 1.0f, TFT_TRANSPARENT);
		delay(8);
		if (a&0x1) filteredOhms();
		}
	delay(10);   // brief pause at Full

	int finalAngle = ohmsToAngle(filteredOhms());

	// Sweep from F to actual level, unless the level is "F"
	if (finalAngle != FULL_ANGLE) {
		int step = -2;
		for (int a = FULL_ANGLE; a != finalAngle; a += step) {
			tft.pushImage(0, 0, 320, 170, light_off_small);
			needle.pushRotateZoom(160, 60 + 180, a, 1.0f, 1.0f, TFT_TRANSPARENT);
			delay(8);
			if ((step > 0 && a >= finalAngle) || (step < 0 && a <= finalAngle)) break;
			}

		// End a little below the final position to get a little bounce at the end
		tft.pushImage(0, 0, 320, 170, light_off_small);
		needle.pushRotateZoom(160, 60 + 180, finalAngle - 2, 1.0f, 1.0f, TFT_TRANSPARENT);
		}

	// Sweep is finished. Normal hysteresis, then redraw the real level.
	// ERROR_ANGLE is below the on-angle, so a fault still lights the lamp.
	updateLowFuel(finalAngle);
	drawNeedle(finalAngle);
}


void createNeedle() 
{
	const int NW = 22;
	const int NH = 150;
	needle.createSprite(NW, NH);
	needle.fillSprite(TFT_TRANSPARENT);

	// Drop shadow
	needle.fillTriangle(NW/2 + 1, 15,
					  2,         47,
					  NW-2,      47,
					  0x6000);
	needle.fillTriangle(2, 41,  0, 51,  7, 47,  0x6000);
	needle.fillTriangle(NW-2, 41,  NW, 51,  NW-7, 47,  0x6000);
	needle.fillRect(NW/2 - 3, 45, 7, NH-55, 0x6000);   // wider shaft shadow

	// Main needle
	uint16_t needleColour = 0xC000;

	needle.fillTriangle(NW/2, 13,
					  3,    45,
					  NW-3, 45,
					  needleColour);
	needle.fillTriangle(3, 39,  1, 49,  8, 45,  needleColour);
	needle.fillTriangle(NW-3, 39,  NW-1, 49,  NW-8, 45,  needleColour);
	needle.fillRect(NW/2 - 3, 43, 7, NH-52, needleColour);  // wider shaft

	needle.setPivot(NW/2, NH + 40);
}




void setup() 
{
	esp_wifi_stop();          // extra safety
	esp_bt_controller_disable();
	Serial.begin(115200);
	Serial.setTxTimeoutMs(0);
	setCpuFrequencyMhz(240);

	analogReadResolution(12);
	pinMode(ADC_PIN, INPUT);
	if (LIGHTS_GPIO_PIN) pinMode(LIGHTS_GPIO_PIN, INPUT);
	pinMode(DEBUG_BUTTON_PIN, INPUT_PULLUP);
	debugButtonDown = (digitalRead(DEBUG_BUTTON_PIN) == LOW);

	tft.init();
	tft.setRotation(3);
	tft.setSwapBytes(true);          // try false if colours are wrong

	// Match the backlight to the lights input before the sweep, using
	// the same thresholds as check_lights(). Pin 0 leaves the feature off.
	// lightsOn starts in agreement so the first loops do not fade.
	if (LIGHTS_GPIO_PIN) {
		int mv = analogReadMilliVolts(LIGHTS_GPIO_PIN);
		if (mv > LIGHTS_ON_MV) lightsOn = true;
		else if (mv < LIGHTS_OFF_MV) lightsOn = false;
		}
	brightness = lightsOn?BRIGHTNESS_LIGHTS_ON:BRIGHTNESS_LIGHTS_OFF;
	targetBrightness = brightness;
	tft.setBrightness(brightness);

	pinMode(15, OUTPUT); // switch screen on
	digitalWrite(15, HIGH);

	// Image was stored as 320×170. Lamp stays off until the sweep has finished.
	tft.pushImage(0, 0, 320, 170, light_off_small);

	createNeedle();

	// Read the real level once
	if (WITH_START_UP_ANIMATION) startupSweep();   // play the animation

	setCpuFrequencyMhz(80);   // drop from 240 MHz to 80 MHz — plenty for a gauge
}



void fadeBrightness()
{
unsigned long elapsed;
long span;
int next;

	if (brightness == targetBrightness) {
		fadeTo = targetBrightness;
		return;
		}

	// A new target, including one that arrives mid-fade, restarts
	// from the level already reached.
	if (targetBrightness != fadeTo) {
		fadeFrom = brightness;
		fadeTo = targetBrightness;
		fadeStartMs = millis();
		}

	// Unsigned subtraction stays correct when millis() wraps.
	elapsed = millis() - fadeStartMs;
	if (elapsed >= (unsigned long)FADE_MS) next = fadeTo;
	else {
		span = (long)fadeTo - fadeFrom;
		next = fadeFrom + (int)(span * (long)elapsed / FADE_MS);
		}

	if (next == brightness) return;
	brightness = next;
	tft.setBrightness(brightness);
}



void check_lights()
{
static int stableCount = 0;
int mv = readings.lightsMv;
bool seen = lightsOn;

	if (mv > LIGHTS_ON_MV) seen = true;
	else if (mv < LIGHTS_OFF_MV) seen = false;

	if (seen == lightsOn) {
		stableCount = 0;
		return;
		}

	stableCount++;
	if (stableCount < STABLE_LOOPS) return;

	targetBrightness = seen?BRIGHTNESS_LIGHTS_ON:BRIGHTNESS_LIGHTS_OFF;
	lightsOn = seen;
	stableCount = 0;
}



void drawDebugFrame()
{
	tft.fillScreen(TFT_BLACK);
	tft.drawFastVLine(160, 0, 170, TFT_DARKGREY);
	tft.drawFastHLine(0, 85, 320, TFT_DARKGREY);

	tft.setFont(&fonts::FreeSansBold9pt7b);
	tft.setTextDatum(textdatum_t::top_center);
	tft.setTextColor(TFT_YELLOW, TFT_BLACK);
	tft.setTextPadding(0);
	tft.drawString("Sender volts", 80, 16);
	tft.drawString("Sender ohms", 240, 16);
	tft.drawString("Filtered ohms", 80, 101);
	tft.drawString("Lights mV", 240, 101);

	debugShown[0][0] = 0;
	debugShown[1][0] = 0;
	debugShown[2][0] = 0;
	debugShown[3][0] = 0;
}



void drawDebugValue(int index, const char *text)
{
int col = index & 1;
int row = index >> 1;
int x = col*160 + 80;
int y = row*85 + 54;

	if (!strcmp(debugShown[index], text)) return;

	strncpy(debugShown[index], text, sizeof(debugShown[index]) - 1);
	debugShown[index][sizeof(debugShown[index]) - 1] = 0;

	// Keep the refresh inside the cell, clear of the label and the dividers
	tft.setClipRect(col*160 + 2, row*85 + 24, 156, 59);
	tft.drawString(text, x, y);
	tft.clearClipRect();
}



void drawDebugValues()
{
char buf[24];

	tft.setFont(&fonts::FreeSansBold18pt7b);
	tft.setTextDatum(textdatum_t::middle_center);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);
	tft.setTextPadding(156);

	snprintf(buf, sizeof(buf), "%.3f", readings.senderVolts);
	drawDebugValue(0, buf);
	snprintf(buf, sizeof(buf), "%.1f", readings.senderOhms);
	drawDebugValue(1, buf);
	if (readings.avgOhms == ERROR_OHMS) snprintf(buf, sizeof(buf), "ERR");
	else snprintf(buf, sizeof(buf), "%.1f", readings.avgOhms);
	drawDebugValue(2, buf);
	if (LIGHTS_GPIO_PIN) snprintf(buf, sizeof(buf), "%d", readings.lightsMv);
	else snprintf(buf, sizeof(buf), "n/a");
	drawDebugValue(3, buf);
}



void debug_wiring()
{
static int id=0;
char line[192];
int len;

	if (LIGHTS_GPIO_PIN) {
		len = snprintf(line, sizeof(line), "%03d: Sender calVolts=%.3f  calOhms=%.1f  filteredOhms=%.1f,  Lights millivolts=%d,  Probe mV=%d fails=%d\n", id++, readings.senderVolts, readings.senderOhms, readings.avgOhms, readings.lightsMv, readings.probeMv, readings.probeFails);
	} else {
		len = snprintf(line, sizeof(line), "%03d: Sender calVolts=%.3f  calOhms=%.1f  filteredOhms=%.1f,  Probe mV=%d fails=%d\n", id++, readings.senderVolts, readings.senderOhms, readings.avgOhms, readings.probeMv, readings.probeFails);
	}
	if (len > 0 && len < (int)sizeof(line) && Serial.availableForWrite() >= len) {
		Serial.write((const uint8_t *)line, (size_t)len);
	}
}



void loop()
{
bool down;

	readings.angle = ohmsToAngle(filteredOhms());
	updateLowFuel(readings.angle);
	if (LIGHTS_GPIO_PIN) readings.lightsMv = analogReadMilliVolts(LIGHTS_GPIO_PIN);

	if (DEBUG_WIRING) debug_wiring();

	// Press edge. The 150 ms loop delay is enough debounce.
	down = (digitalRead(DEBUG_BUTTON_PIN) == LOW);
	if (down && !debugButtonDown) {
		debugScreen = !debugScreen;
		if (debugScreen) {
			setCpuFrequencyMhz(240);
			drawDebugFrame();
			}
		else {
			lastAngle = -999;
			}
		}
	debugButtonDown = down;

	if (debugScreen) {
		setCpuFrequencyMhz(240);
		drawDebugValues();
		}
	else if (abs(readings.angle - lastAngle) >= 1) {
		// Only update the display if the needle has moved at least 1°
		setCpuFrequencyMhz(240);
		drawNeedle(readings.angle);
		lastAngle = readings.angle;
		}

	if (LIGHTS_GPIO_PIN) {
		check_lights();
		// ERR (open cable, rail-high, or a failed float probe) forces
		// day brightness. A bare lights pin floats near 2.9 V and would
		// dim a unit with no harness. lightsOn still tracks the pin, so
		// leaving ERR fades back to it. The millis() fade does the move.
		if (readings.avgOhms == ERROR_OHMS) targetBrightness = BRIGHTNESS_LIGHTS_OFF;
		else targetBrightness = lightsOn?BRIGHTNESS_LIGHTS_ON:BRIGHTNESS_LIGHTS_OFF;
		fadeBrightness();
		}

	setCpuFrequencyMhz(80);
	delay(150);
}
