# fuel-guage

A kit-car fuel gauge on a [LILYGO T-Display-S3](https://www.amazon.co.uk/dp/B0BRTT727Z) (ESP32-S3). The firmware is Arduino with [LovyanGFX](https://github.com/lovyan03/LovyanGFX), built with PlatformIO. It draws a vintage gauge face, a needle driven by the tank sender, and a low-fuel lamp.

The tank on this car sits at an odd angle and the cluster is an old Ford unit, so a normal gauge could not be made to read honestly. This one is calibrated to the sender that is actually fitted.

I’ve never coded for these devices before, so the firmware was written by asking an AI. To change it, upload [`src/main.cpp`](src/main.cpp) and ask for the change. That is pretty much the whole process.

Build, upload, and serial-monitor steps are in [docs/PLATFORMIO.md](docs/PLATFORMIO.md).

## Hardware and wiring

### Gauge face images

The JPG images were converted to header files with the [Proje Defteri RGB565 converter](https://projedefteri.com/tools/image-to-rgb565-converter/). Tell it the image is 320×170. The other defaults are right: output is RGB565, 16-bit, hex.

The two faces live in `images/` as `light off small.jpg` and `light on small.jpg`, and in `src/` as `light_off_small.h` and `light_on_small.h`. To draw a different face, replace those images and regenerate the headers. If you do not want a warning lamp, use the same image for both, or remove the lamp choice in `src/main.cpp`.

### Fuel sender

A fuel sender is the float and variable resistor in the tank. As the float moves, the resistance changes. Think of a toilet float bolted to a volume knob.

This sender will not match yours. The two numbers you have to change are at the top of [`src/main.cpp`](src/main.cpp). Measure them on the debug screen with the gauge installed, as described in [Calibration](#calibration).

```c
const float EMPTY_OHMS = 167.0f;
const float FULL_OHMS  = 29.0f;
```

These are the **only** two lines you **must** change. Guessing will move the needle, but measuring is better. If you know one level and assume the sender is linear, you can estimate the other end. A flat tank usually lets the float travel its full range. This one does not.

| Style | Empty | Full | Notes |
| :---- | :---- | :---- | :---- |
| Many aftermarket | 0 Ω | 200 Ω | “normal” direction |
| Many aftermarket | 200 Ω | 0 Ω | reversed (like mine) |
| Classic Ford/GM | 0–30 Ω | 70–90 Ω | shorter range |
| Some European | 0 Ω | 180 Ω |  |
| Others | 240 Ω | 33 Ω | reversed |

A “normal” 0–200 Ω sender (empty = low resistance) is common. You still have to use the empty and full values of the unit in the car. The code does not care which end is the higher resistance. This sender is a normal 0–200 Ω unit fitted upside down, so it reads backwards, and the tank shape stops the float short of its travel. That is why the gauge exists.

### Connecting the sender

Connect a board GPIO to the sender signal, and a board ground to the sender ground. Ground it as close to the sender as you can, on a terminal nothing else is using. If the sender has no ground terminal, a tank-strap bolt that is not already the tank ground is the next best thing.

Use a twisted pair for the signal and ground from the tank to the module. If you do not have twisted pair, chuck two wires in a drill and spin them slowly. Twisting shortens the cable: over a car length, allow about 8 to 10 inches extra. A shielded cable is optional. If you use one, ground the shield to the chassis at both ends.

```
  fuel tank                                                                T-Display-S3 pins
  ---------                                                                -----------------
                             twisted pair (preferably)

  sender signal -------+----- signal core ------+--[ 330 ohm ]---------------- 3.3V+
                       |                        |
                   [ sender ]                   |
                       |                        +----------------------------- GPIO1
                       |                        |
                       |                        +--------[ 100 nF ]--------+
                       |                          optional, at the module  |
  sender local ground -+----- ground core ---------------------------------+-- GND
```

GPIO1 is the ADC, on the sender side of the 330 Ω resistor. The series resistor should be roughly the sender’s mid-range resistance. The 100 nF capacitor from GPIO1 to GND, fitted at the module, is optional and reduces ADC jitter.

The same circuit, in one line:

```
3.3V -> 330 ohm -> GPIO1 (ADC) -> sender -> ground
```

### Power

A spare USB socket in the car is enough. If you do not have one, a dashcam adapter works. They are either hidden and wired into the car, or they plug into the 12 V socket. The module wants USB-C. Many dashcam leads are mini-USB or micro-USB, and a right-angle plug is worth having.

This car uses [this adapter](https://www.amazon.co.uk/dp/B0CWNKW2LH). The module and the adapter are held on with [Dual Lock](https://www.amazon.co.uk/dp/B0BSTG1GH5), so they stay put and can still come off.

### Lights input

Optional. The backlight dims when the car’s lights are on, and comes back up when they are off. The lights feed is about 12–14.7 V, and it is only live when the ignition is on. **Never connect 12 V directly to a GPIO.** That will damage the board.

Use a divider into GPIO16 (`LIGHTS_GPIO_PIN` in this firmware):

- 47k from the 12 V lights feed to GPIO16
- 10k from GPIO16 to GND
- 100 nF capacitor across the 10k (GPIO16 to GND)

```
  car lights +12 V                                      T-Display-S3 pins
  live only when the lights are on                      -----------------
  about 12-14.7 V

       +12 V --[ 47k ]--+
                        |
                        +------------------------------ GPIO16
                        |
                        +----------[ 10k ]----------+
                        |                           |
                        +--------[ 100 nF ]---------+
                               across the 10k       |
                                                    +-- GND
```

GPIO16 sees about 2.1 V at 12 V, about 2.6 V at 14.7 V, and 0 V with the lights off. About 2.1 V is a poor digital HIGH, so the firmware reads the pin with `analogReadMilliVolts()`. The pin is `INPUT` with no internal pull-up. The 10k to GND is the pulldown.

Set `LIGHTS_GPIO_PIN` to `0` if nothing is connected. An unconnected pin floats, and the screen would dim at random. This copy ships with the pin set to 16.

The reading has to pass the thresholds for `STABLE_LOOPS` loops in a row (4 × 150 ms, about 0.6 s) before the target brightness changes. Above 1600 mV counts as lights on. Below 1200 mV counts as lights off. A reading in between keeps the previous state, so a small change does not flicker the backlight.

The fade does not stop the gauge. Each loop steps the backlight toward the new level, and a full swing between 250 and 60 takes about a second (`FADE_MS`). Sampling, the needle, the cable-break check, and the debug screen keep running.

At boot, after the display is initialised and before the startup sweep, the pin is read once with the same 1600 / 1200 mV test. Lights already on start at brightness 60. Lights off, a reading in the gap, or pin 0, start at 250. That first result is stored as the current lights state, so the gauge does not fade on the first loops just because it woke up.

## Calibration

Every resistance below is **Filtered ohms** on the debug screen.

Calibrate from the supply the gauge will use in the car. A laptop USB port and a car USB adapter do not always agree. Use the wiring you will actually install, and copy the on-screen Filtered ohms into the code. A multimeter across the sender is only a rough check.

### Bench check

Optional. Fit a known resistor in place of the sender and confirm **Filtered ohms** is inside that resistor’s tolerance. A 47 Ω 5% resistor here read 46.0.

### Empty and full

1. Drain the tank to empty, or as near empty as you will run it. Read **Filtered ohms** and set `EMPTY_OHMS` to that number.
2. Fill the tank. Read **Filtered ohms** and set `FULL_OHMS` to that number.

The float can hit its stop, so the last few litres may not move the reading. Use the value on screen once the float is against the stop.

This gauge is set to `EMPTY_OHMS` 167 and `FULL_OHMS` 29.

### Angles

The sender is assumed to be roughly linear between empty and full. Needle angle is a straight map from those two resistances onto two angles. The mapping itself is unchanged by the lamp thresholds.

| Constant | Value | What it does |
| :---- | :---- | :---- |
| `EMPTY_ANGLE` | 315 | Needle on E at `EMPTY_OHMS` |
| `FULL_ANGLE` | 402 | Needle on F at `FULL_OHMS` |
| `ERROR_ANGLE` | 305 | Needle position on `ERR`, below the E mark |
| `TANK_LOW_ON_ANGLE` | 335 | Turn the low-fuel lamp on when the angle is below this |
| `TANK_LOW_OFF_ANGLE` | 338 | Turn the lamp off only once the angle reaches this |

`ERROR_ANGLE` is below `EMPTY_ANGLE`, so a fault parks the needle under E. 305 is also below `TANK_LOW_ON_ANGLE`, so the lamp is on for a fault.

Within `ERROR_GAP_OHMS` (10 Ω) past empty or full, the needle stays on that stop. Past the gap it moves to `ERROR_ANGLE` and the screen shows `ERR`. A cable break does the same once three raw readings are at or above `OPEN_CIRCUIT_OHMS` (7500 Ω). Those readings are not averaged, so the next reading below that can leave the error mark at once.

The lamp has a gap so it does not flicker when the needle sits on the threshold. Below 335 it turns on. It stays on through 335, 336 and 337, and turns off only at 338 and above. Raise the on-angle to warn sooner. Lower it to warn later. Keep the off-angle a few degrees higher.

On the startup sweep the lamp follows the needle: on at E, off once the sweep reaches 338, and on again on the way back only if the needle goes below 335. When the sweep finishes, the lamp is reset to the real level. A level in the 335–337 gap leaves the lamp off, because the needle arrived from full. The small bounce below the final angle does not latch the lamp on. With the startup animation disabled (`WITH_START_UP_ANIMATION` set to 0), the first real reading uses the same rule, starting from lamp off.

These angles match the gauge face in this repo. On a new face, put `EMPTY_ANGLE` on the E mark, `FULL_ANGLE` on the F mark, `ERROR_ANGLE` just below E, and the two lamp angles where the low-fuel warning should come on and go off.

### Flash

After editing the constants, rebuild and flash. Port selection and the rest of the steps are in [Building and uploading](docs/PLATFORMIO.md).

```sh
pio run -t upload
```

## Debug screen

Press the board’s KEY button (GPIO14) to show the debug screen. Press it again to return to the gauge. With the gauge in landscape and the buttons and USB socket on the left, KEY is the bottom button.

The screen is a 2×2 grid. The cells refresh about every 150 ms.

```
Sender volts     Sender ohms
Filtered ohms    Lights mV
```

- **Sender volts** (top left) is the voltage on GPIO1, the ADC on the sender side of the 330 Ω resistor. One sample, shown to three decimal places.
- **Sender ohms** (top right) is that sample converted with `330 * V / (3.3 - V)`, to one decimal place. A single sample, so it jitters. A reading under 0.01 V is reported as 0 Ω. A reading within 50 mV of 3.3 V is reported as 1000000 Ω, so a sample sitting on the supply rail cannot divide by zero or come out negative.
- **Filtered ohms** (bottom left) is a trimmed mean of up to 10 samples (`SENDER_AVG_SAMPLES`). The highest and the lowest sample are dropped and the rest are averaged (8 samples once the buffer is full), so one spiked reading does not pull the needle. Readings at or above `OPEN_CIRCUIT_OHMS` are not stored. Until there are three stored samples it is a plain average. The needle follows this number, shown to one decimal place. Out of range, or before any sample has been stored, the cell shows `ERR`.
- **Lights mV** (bottom right) is the lights-input voltage in millivolts, from `LIGHTS_GPIO_PIN` (16 in this file). When `LIGHTS_GPIO_PIN` is 0 the cell shows `n/a`.

`ERR` means the filtered reading is more than `ERROR_GAP_OHMS` (10) outside the empty-to-full range. On this car empty is the higher resistance, so that limit is above `EMPTY_OHMS + ERROR_GAP_OHMS` (167 + 10 = 177 Ω). The low end is the same gap past the smaller of `EMPTY_OHMS` and `FULL_OHMS` (below 19 Ω with these numbers).

A broken wire, or the sender unplugged, is an open circuit. The ADC tops out near 3.162 V, about 7561 Ω, and **Sender ohms** still shows that raw reading. The rail guard above does not change that normal open-circuit reading. Those readings are left out of the average. `OPEN_CIRCUIT_OHMS` is set just below that, at 7500. As soon as the last three raw readings are all at or above it, **Filtered ohms** shows `ERR`. With the cable open from power-on the buffer is empty, so the cell shows `ERR` straight away. Plug the cable back in and the first raw reading below `OPEN_CIRCUIT_OHMS` is the next sample in the average. The needle leaves the error mark on that reading when it is within `ERROR_GAP_OHMS` of the empty-to-full range.

Set `DEBUG_WIRING` to `1` at the top of `src/main.cpp` to print the same values on the serial port. The file ships with it set to `0`. About every 150 ms the port writes a line like `Sender calVolts=...  calOhms=...  filteredOhms=...`, and `Lights millivolts=...` when the lights pin is enabled. `ERR` on the screen is `filteredOhms=-1.0` in that log. The baud rate is 115200. Printing is non-blocking and drops a line if nobody is reading the port. The gauge keeps running. How to open the monitor, including the `log2file` filter and the `monitor` script, is in [Building and uploading](docs/PLATFORMIO.md#serial-monitor).

## Configuration

All of these are in [`src/main.cpp`](src/main.cpp).

| Constant | Default | What it does |
| :---- | :---- | :---- |
| `EMPTY_OHMS` | 167 | Sender resistance at empty. Set this for your tank |
| `FULL_OHMS` | 29 | Sender resistance at full. Set this for your tank |
| `ERROR_GAP_OHMS` | 10 | How far past empty or full still counts as that stop |
| `OPEN_CIRCUIT_OHMS` | 7500 | Raw reading at or above this is left out of the average. Three in a row is a cable break |
| `FAKE_CAR_READING` | 0 | Set above 0.01 to ignore the sender and force this resistance |
| `EMPTY_ANGLE` | 315 | Needle angle at empty. Tied to the gauge face |
| `FULL_ANGLE` | 402 | Needle angle at full. Tied to the gauge face |
| `ERROR_ANGLE` | 305 | Needle angle for `ERR`, below E. Also lights the lamp |
| `TANK_LOW_ON_ANGLE` | 335 | Lamp turns on when the needle angle is below this |
| `TANK_LOW_OFF_ANGLE` | 338 | Lamp turns off only when the angle reaches this |
| `LIGHTS_GPIO_PIN` | 16 | Lights-dimmer input. `0` disables it |
| `BRIGHTNESS_LIGHTS_OFF` | 250 | Backlight with the lights off. 255 glitches this panel; 250 looks the same |
| `BRIGHTNESS_LIGHTS_ON` | 60 | Backlight with the lights on |
| `FADE_MS` | 1000 | Length of a full brightness fade. Stepped inside `loop()`, not a blocking delay |
| `LIGHTS_ON_MV` | 1600 | Lights count as on above this (millivolts) |
| `LIGHTS_OFF_MV` | 1200 | Lights count as off below this (millivolts) |
| `STABLE_LOOPS` | 4 | Loops in a row the new lights state must hold. Each loop is ~150 ms, so ~0.6 s |
| `WITH_START_UP_ANIMATION` | 1 | Set to 0 to skip the E-to-F sweep |
| `SENDER_AVG_SAMPLES` | 10 | Length of the trimmed-mean buffer |
| `DEBUG_WIRING` | 0 | Set to 1 to print sender readings on the serial port |
| `DEBUG_BUTTON_PIN` | 14 | KEY button. Toggles the debug screen |
| `ADC_PIN` | 1 | Sender ADC |
| `SERIES_R` | 330 | Ohms from 3.3 V to the sender ADC |

## Build and upload

Install PlatformIO, compile, flash, and watch the serial log — including `pio device monitor -f log2file` and the `monitor` / `upload` scripts — from [docs/PLATFORMIO.md](docs/PLATFORMIO.md).
