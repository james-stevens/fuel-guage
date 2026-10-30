# fuel-guage

Custom car fuel gauge using a [LILYGO T-Display-S3 ESP32-S3 LCD Display](https://www.amazon.co.uk/dp/B0BRTT727Z)

I have a kit car with a custom fuel tank, mounted at an odd angle and an old Ford instrument cluster. Getting an accurate fuel gauge reading was becoming really hard, so I decided to build a digital one, with a “vintage” look and feel.

I’ve never coded for these devices before, or any similar, so all the coding was done by Grok. If you want to customise the code, I highly recommend uploading [`src/main.cpp`](src/main.cpp) to an AI and just asking for the changes you want. This is pretty much all I did.

The JPG images were converted to header files using [https\://projedefteri.com/tools/image-to-rgb565-converter/](https://projedefteri.com/tools/image-to-rgb565-converter/) 

You will need to tell it the image is 320x170, otherwise it’s default options are correct, e.g. the output needs to be “RGB656 16 bit, hex”.

The two images I used are in the `images/` directory called `light off small.jpg` and `light on small.jpg` and converted into `light_on_small.h` and `light_off_small.h` using that site. If you want to replace the background image, but do not want a warning light, you can either change [`src/main.cpp`](src/main.cpp) to remove that feature, or simply use the same image for both states.

## Your Fuel Sender

A car’s fuel sender is the unit in the fuel tank that tells the car the fuel level. It is nearly always a rotational variable resistor that changes resistance as a float moves up & down turning the dial of the resistor. Exactly like combining a toilet float value and a radio volume control.

Given the oddities of my car, it is highly likely your fuel sender will NOT read the same way as mine.

Below are the critical two lines, at the start of [`src/main.cpp`](src/main.cpp). You will need to get these resistance readings from your own car. Disconnect the fuel sender from the car and measure it when the tank is empty and full, either using a multimeter or using your module (better, see below).

You can try just guessing and see if the needle goes to the right place, but obviously measuring is better. If the fuel is at a known level and you measure the resistance, you can guess that the sender will be linear and calculate the top & bottom, but measuring is better. If you have a normal tank mounted at a normal angle (flat), then it’s likely your float can free travel through its full range. Mine can’t.

```c
const float EMPTY_OHMS = 150.0f;
const float FULL_OHMS  = 29.0f;
```

These are the **ONLY** two lines of code you **MUST** change.

Typical ranges you might see:

| Style | Empty | Full | Notes |
| :---- | :---- | :---- | :---- |
| Many aftermarket | 0 Ω | 200 Ω | “normal” direction |
| Many aftermarket | 200 Ω | 0 Ω | reversed (like mine) |
| Classic Ford/GM | 0–30 Ω | 70–90 Ω | shorter range |
| Some European | 0 Ω | 180 Ω |  |
| Others | 240 Ω | 33 Ω | reversed |

A “normal” 0–200 Ω sender (empty \= low resistance) is widespread, but you always have to check the actual empty/full values for the unit being used. Your code only needs those two numbers changed.

The code doesn’t mind if yours goes forward or reverse. Mine actually works forward (0-200), but had to be fitted upside down, so actually goes in reverse & the odd shape of my tank means the float can’t travel its full distance. Hence building this.

### Import Note

It is **highly unlikely** that your module and your multimeter will give the same resistance readings from your fuel sender. Roughly the same, but not exactly the same. So the best way to get good “Empty” and “Full” readings, to put into the code, is to uncomment the `debug_sender();` line in `loop()` and use `pio device monitor` (see below) to see the real readings coming from your car, using as much of the same wiring you plan to use when the gauge is properly installed.

The module will not be 100% accurate and the wiring in the car will affect the reading. So wiring the module to the car in the way you intend, then taking an “Empty” and “Full” from the car, is the best way to ensure your fuel gauge is as accurate as possible.

## To Upload the Code

The development system is called *PlatformIO*, but if you tell the AI what device you have it will tell you how to get the code working.

I used an Ubuntu Desktop PC as my development platform, so all these commands are for an Ubuntu PC. For other environments, please ask AI. There are *PlatformIO* plug-ins for “VS Code”, if you prefer using that.

If you use a different device from me, you may need to change `platform.ini` and the panel set up at the start of [`src/main.cpp`](src/main.cpp). As an AI. Tell it which module I used, and which one you bought, and it will tell you what to change.

### Install PlatformIO CLI on Ubuntu

```sh
sudo apt update
sudo apt install python3 python3-venv python3-pip
curl -fsSL https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py -o get-platformio.py
python3 get-platformio.py
# Add to PATH (or put the lines in ~/.bashrc)
export PATH="$HOME/.platformio/penv/bin:$PATH"
# Permanent:
echo 'export PATH="$HOME/.platformio/penv/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc

# Add your user to dialout so you can upload without sudo
sudo usermod -a -G dialout $USER
# Log out and back in, then:
pio --version
```

You **will** need to log out & log back in to get the `usermod` command to take effect. This is required for uploading code to the device.

Once you have cloned this repo …

### 1\. Make sure you are in the project folder

```sh
cd ~/fuel-gauge          # or wherever you created the project
ls                       # you should see platformio.ini and a src/ folder
```

### 2\. Build (compile) the project

```sh
pio run
```

- First run will download the ESP32 toolchain and libraries (can take a few minutes).  
- When it finishes successfully you will see something like:

```
SUCCESS
RAM:   [          ]   4.8% (used 15744 bytes from 327680 bytes)
Flash: [==        ]  18.3% (used 240144 bytes from 1310720 bytes)
```

If you get errors, send them to an AI.

### 3\. Connect the board and find the port

Plug the T-Display-S3 into a USB port on your Ubuntu machine, then:

```sh
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

You should see something like `/dev/ttyACM0` or `/dev/ttyUSB0`.  
(If nothing appears, try a different cable — many cables are charge-only — or check `dmesg | tail` after plugging it in.)

### 4\. Upload the firmware

```sh
pio run -t upload --upload-port /dev/ttyACM0
```

(Replace `/dev/ttyACM0` with the port you saw from the `ls` above.)

- The board should reset and the upload progress will appear.  
- When it says `SUCCESS` the code is running on the device.


If you edit the [`src/main.cpp`](src/main.cpp) file, or change the background images, then just run this command again to rebuild the code & send it to the device.

### Useful extra commands

| Command | What it does |
| :---- | :---- |
| `pio run` | Build only |
| `pio run -t upload` | Build \+ upload (auto-detects port if only one is present) |
| `pio device monitor` | Open serial monitor (115200 baud) to see the `Serial.printf` output |
| `pio run -t clean` | Clean build files if something gets stuck |
| `pio pkg update` | Update libraries |

If you want to debug your code you can send a `printf()` to the serial port with `Serial.printf()` then view the output using

```sh
pio device monitor --port /dev/ttyACM0 --baud 115200
```

NOTE: if there is a lot of output, and you are not running `pio device monitor`, your program might stop running, waiting for you to read the output using this `monitor` command.

## Powering Your Module

If you have a spare USB outlet in your car, then use this to power the module. If not, you can buy power adapters intended for Dashcams that will do a good job. They are available in a wide range of different configurations, generally either ones that are hidden and wire (or connect) into the car or ones that plug into a car’s 12v “lighter” socket.

I bought [this one](https://www.amazon.co.uk/dp/B0CWNKW2LH), but any will do. Just make sure you get one that ends with a USB-C as many car dash cams have older USB socket types (mini & micro). You might also want to get one with a 90 degree USB plug.

I also bought these [Dual Lock pads](https://www.amazon.co.uk/dp/B0BSTG1GH5) to secure the fuel gauge module and the USB power supply, so they are secure, but can still be easily removed, if necessary.
