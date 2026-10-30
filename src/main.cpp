#define LGFX_USE_V1
#include <esp_wifi.h>
#include <esp_bt.h>
#include <LovyanGFX.hpp>

//-------------------------------------------------------------
// These numbers you will prob need to change to match your car
//-------------------------------------------------------------
const float EMPTY_OHMS = 150.0f;
const float FULL_OHMS  = 29.0f;
// if outside the range above, by this amount, indicate an error
// i.e. light the warning light and put the needle below "E"
const float ERROR_GAP_OHMS  = 10.0f;

// Set this to a value more than 0.01 to fake a reading from the car's sender
const float FAKE_CAR_READING = 0.0;

//-------------------------------------------------------------
// below this angle, light the warning light to indicated low fuel
const int TANK_LOW_WARNING_ANGLE = 335;  

//-------------------------------------------------------------
// Set LIGHTS_GPIO_PIN to the GPIO line you have wired to detect
// when the car lights are on - `0`=disable feature
//-------------------------------------------------------------
const int LIGHTS_GPIO_PIN = 0;
// const int LIGHTS_GPIO_PIN = 2;   // or any free GPIO
//-------------------------------------------------------------
// Tweak these numbers to adjust the screen brightness
//-------------------------------------------------------------
const int BRIGHTNESS_LIGHTS_OFF = 255;
const int BRIGHTNESS_LIGHTS_ON = 80;


// set this to zero to disable the start-up animation
const int WITH_START_UP_ANIMATION = 1;


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
const int EMPTY_ANGLE = 318;
const int FULL_ANGLE  = 402;
const int ERROR_ANGLE = EMPTY_ANGLE - 8;

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

// Simple moving average
const int   AVG_SAMPLES = 8;
float       avgBuf[AVG_SAMPLES];
int         avgIdx = 0;
bool        avgFilled = false;



float readFuelOhms()
{
	int raw = analogRead(ADC_PIN);
	float v = (raw / 4095.0f) * VCC;
	if (v < 0.01f) return 0;
	float r = SERIES_R * (v / (VCC - v));
	return r;
}



float filteredOhms() 
{
static float buf[AVG_SAMPLES];
static int idx = 0;
static bool filled = false;
float raw = readFuelOhms();

	buf[idx] = raw;
	idx = (idx + 1) % AVG_SAMPLES;
	if (idx == 0) filled = true;

	int n = filled ? AVG_SAMPLES : idx;
	float sum = 0;
	for (int i = 0; i < n; i++) sum += buf[i];
	float ohms = sum / n;

	if (FAKE_CAR_READING > 0.01) return FAKE_CAR_READING;

	if ((ohms < MIN_OHMS)||(ohms > MAX_OHMS)) return ERROR_OHMS;
	return ohms;
}



int ohmsToAngle(float ohms) 
{
	if (ohms < 0) return ERROR_ANGLE;

	int angle = map(ohms * 10, EMPTY_OHMS * 10, FULL_OHMS * 10,
				  EMPTY_ANGLE, FULL_ANGLE);

	return constrain(angle, MIN_ANGLE, MAX_ANGLE);
}



void startupSweep() 
{
	// sweep from E to F on start up
	for (int a = EMPTY_ANGLE; a <= FULL_ANGLE; a += 2) {
		tft.pushImage(0, 0, 320, 170, light_off_small);
		needle.pushRotateZoom(160, 60 + 180, a, 1.0f, 1.0f, TFT_TRANSPARENT);
		delay(8);
		if (a&0x1) filteredOhms();
		}
	delay(10);   // brief pause at Full

	int finalAngle = ohmsToAngle(filteredOhms());

	// Sweep from F to actual level, unless the level is "F"
	if (finalAngle == FULL_ANGLE) return;

	int step = -2;
	for (int a = FULL_ANGLE; a != finalAngle; a += step) {
		tft.pushImage(0, 0, 320, 170, light_off_small);
		needle.pushRotateZoom(160, 60 + 180, a, 1.0f, 1.0f, TFT_TRANSPARENT);
		delay(8);
		if ((step > 0 && a >= finalAngle) || (step < 0 && a <= finalAngle)) break;
		}

	// End a little below the final positiion to get a little bounce at the end
	tft.pushImage(0, 0, 320, 170, light_off_small);
	needle.pushRotateZoom(160, 60 + 180, finalAngle - 2, 1.0f, 1.0f, TFT_TRANSPARENT);
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
	setCpuFrequencyMhz(240);

	analogReadResolution(12);
	pinMode(ADC_PIN, INPUT);
	if (LIGHTS_GPIO_PIN) pinMode(LIGHTS_GPIO_PIN, INPUT);

	tft.init();
	tft.setRotation(3);
	tft.setBrightness(BRIGHTNESS_LIGHTS_OFF);   // normal day brightness
	tft.setSwapBytes(true);          // try false if colours are wrong

	pinMode(15, OUTPUT); // switch screen on
	digitalWrite(15, HIGH);

	// Image was stored as 320×170
	tft.pushImage(0, 0, 320, 170, light_off_small);

	createNeedle();

	// Read the real level once
	if (WITH_START_UP_ANIMATION) startupSweep();   // play the animation

	setCpuFrequencyMhz(80);   // drop from 240 MHz to 80 MHz — plenty for a gauge
}



void debug_sender()
{
	static int id=0;
	int raw = analogRead(ADC_PIN);
	float volts = raw * 3.3f / 4095.0f;
	float ohms = readFuelOhms();

	Serial.printf("%03d: raw=%d  volts=%.2f  ohms=%.1f\n",id++, raw, volts, ohms);
}



void check_lights()
{
static int lastLights = -999;
int lights_now = digitalRead(LIGHTS_GPIO_PIN);

	if (lights_now == lastLights) return;

	tft.setBrightness((lights_now == HIGH)?BRIGHTNESS_LIGHTS_ON:BRIGHTNESS_LIGHTS_OFF);
	lastLights = lights_now;
}



void loop()
{
	static int lastAngle = -999;
	int angle = ohmsToAngle(filteredOhms());

	// uncomment the "debug_sender();" line if you are trying to debug the wiring in the car
	// then use `pio device monitor ...` to watch the output
	debug_sender();

	// Only update the display if the needle has moved at least 1°
	if (abs(angle - lastAngle) >= 1) {
		setCpuFrequencyMhz(240);
		tft.pushImage(0, 0, 320, 170, ((angle < TANK_LOW_WARNING_ANGLE)?light_on_small:light_off_small));
		needle.pushRotateZoom(160, 60 + 180, angle, 1.0f, 1.0f, TFT_TRANSPARENT);
		lastAngle = angle;
		}

	if (LIGHTS_GPIO_PIN) check_lights();

	setCpuFrequencyMhz(80);
	delay(250);
}
