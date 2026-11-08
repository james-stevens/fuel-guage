# Building and uploading

The firmware is a [PlatformIO](https://platformio.org/) project (`platformio.ini`, code in `src/main.cpp`). These commands are for an Ubuntu PC. PlatformIO also has a VS Code plugin. On another system, the same `pio` commands apply once the CLI is installed.

If you use a different board, you may need to change `platformio.ini` and the panel setup at the start of [`src/main.cpp`](../src/main.cpp).

## Install the PlatformIO CLI on Ubuntu

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

You **will** need to log out and log back in before the `usermod` command takes effect. That is required to upload without `sudo`.

If you already use pip, this also works. Put `~/.local/bin` on `PATH`:

```sh
python3 -m pip install --user platformio
```

## Build

From the project folder (the one that contains `platformio.ini` and `src/`):

```sh
pio run
```

The first run downloads the ESP32 toolchain and libraries, which can take a few minutes. A successful build ends with something like:

```
SUCCESS
RAM:   [=         ]  11.3% (used 37096 bytes from 327680 bytes)
Flash: [===       ]  25.8% (used 810683 bytes from 3145728 bytes)
```

If the build fails, the error text is enough for an AI to fix it. `pio run -t clean` clears a stuck build.

## Find the serial port

Plug the T-Display-S3 into USB, then:

```sh
ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
```

You should see something like `/dev/ttyACM0` or `/dev/ttyUSB0`. If nothing appears, try another cable (many are charge-only) or check `dmesg | tail` after plugging it in.

## Upload

```sh
pio run -t upload --upload-port /dev/ttyACM0
```

Replace `/dev/ttyACM0` with the port from the `ls` above. The board resets, the upload prints its progress, and `SUCCESS` means the new firmware is running.

If only one port is present, PlatformIO can pick it:

```sh
pio run -t upload
```

Editing `src/main.cpp` or the background images, then running that again, rebuilds and flashes.

This repo also has an `upload` script that flashes `/dev/ttyACM0`:

```sh
./upload
```

## Serial monitor

The baud rate is 115200 (`monitor_speed` in `platformio.ini`). What the firmware prints, and when, is described in the [debug screen](../README.md#debug-screen) section of the README. Printing is non-blocking: if nothing is reading the port, a line is dropped and the gauge keeps running.

```sh
pio device monitor --port /dev/ttyACM0 --baud 115200
```

Save the log to a file with the `log2file` filter:

```sh
pio device monitor -f log2file --port /dev/ttyACM0 --baud 115200
```

PlatformIO names that file `platformio-device-monitor-*.log` in the project folder. Those logs are gitignored.

The `monitor` script in the repo deletes any previous log, then opens the port with `log2file` and timestamps:

```sh
./monitor
```

It is hard-wired to `/dev/ttyACM0` and 115200 baud, same as `upload`. Change the port in the script if your board enumerates as something else.

## Useful commands

| Command | What it does |
| :---- | :---- |
| `pio run` | Build only |
| `pio run -t upload` | Build and upload (auto-detects the port if only one is present) |
| `pio run -t upload --upload-port /dev/ttyACM0` | Build and upload to a chosen port |
| `./upload` | Same, for `/dev/ttyACM0` |
| `pio device monitor` | Serial monitor at 115200 baud |
| `pio device monitor -f log2file --port /dev/ttyACM0 --baud 115200` | Monitor, and write a log file |
| `./monitor` | Delete old monitor logs, then log this session (with timestamps) to a file |
| `pio run -t clean` | Delete build files if something gets stuck |
| `pio pkg update` | Update libraries |
