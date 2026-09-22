# TLV320DAC3100 MP3 Player — ESP32-S3

Bring-up and test log for an [Adafruit TLV320DAC3100](https://www.adafruit.com/product/6090) I2S DAC breakout driven by an ESP32-S3, built up in PlatformIO from a bare I2C bus scan to full MP3 playback off onboard flash.

## Hardware

- **MCU:** Freenove ESP32-S3-WROOM (N8R8 — 8MB flash, 8MB octal PSRAM)
- **DAC:** Adafruit TLV320DAC3100 I2S DAC breakout
- **Output:** 8Ω speaker (JST) and/or 3.5mm headphone jack
- Breadboard + jumper wires

## Wiring

| Signal        | ESP32-S3 GPIO |
|---------------|:-------------:|
| I2C SDA       | 8             |
| I2C SCL       | 9             |
| I2S BCLK      | 39            |
| I2S WS (LRC)  | 40            |
| I2S DIN       | 41            |
| DAC RESET     | 12            |

DAC RESET is active-low, pulsed at boot. Headphone output runs fine off 3.3V; the speaker (Class-D) output needs VIN at 5V.

![Breadboard wiring, top-down](assets/Test_Setup.jpeg)
![Breadboard wiring, angled](assets/Test_Setup_2.jpeg)

## What it does

1. Scans the I2C bus at boot and confirms the DAC answers at `0x18`.
2. Brings up the codec — PLL fed from BCLK, DAC data path, headphone driver, and speaker driver (with short-circuit auto-recovery) — before any audio flows.
3. Plays an MP3 stored in onboard flash (LittleFS) through the DAC over I2S, decoded in real time with [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S), looping on end-of-track.
4. Runs a background check every ~500ms for short-circuit, over-temperature, or sticky IRQ flags on the codec, logged to Serial.

Earlier stages of this build (kept as a record of the process, not part of the final firmware) generated a fixed test tone, then alternating tones, then a synthesized melody directly over I2S — useful for confirming the signal chain before adding real audio decoding.

## Demo

[🎥 Watch the audio test](assets/Audio_Test.mp4)

## Software

Built with [PlatformIO](https://platformio.org/). Key `platformio.ini` points:

- `board = freenove_esp32_s3_wroom` — PlatformIO's manifest for this board already knows it's QIO flash + octal PSRAM, so flash mode/PSRAM type aren't overridden manually.
- A **custom partition table** (`partitions.csv`): 2MB app / ~5.9MB LittleFS. The stock partition schemes only give the filesystem ~1.5MB, which isn't enough to hold an MP3.
- `board_build.filesystem = littlefs`.
- Libraries pulled straight from their repos via `lib_deps`, rather than the registry:
  - [Adafruit_TLV320_I2S](https://github.com/adafruit/Adafruit_TLV320_I2S)
  - [Adafruit_BusIO](https://github.com/adafruit/Adafruit_BusIO)
  - [ESP32-audioI2S](https://github.com/schreibfaul1/ESP32-audioI2S)

### Building and flashing

Firmware and filesystem are **two separate uploads**:

```sh
pio run --target upload      # flashes the sketch
pio run --target uploadfs    # flashes data/track.mp3 to LittleFS
```

Both need to be run at least once; `uploadfs` only needs re-running when the audio file itself changes. If the board ever reports the track missing on LittleFS after a partition table change, do a full `pio run --target erase` and reflash both in order.

## Bring-up notes

A few non-obvious issues turned up along the way, in case they're useful to someone hitting the same thing:

- **No voltage at SPK+:** the speaker (Class-D) driver needs 5V on VIN, not just 3.3V, and needs to be explicitly enabled in code. It's also a bridge-tied load — a plain DC multimeter across SPK+ to GND often reads near 0V even when it's working; check with AC volts across SPK+/SPK-, a scope, or just a real speaker.
- **Speaker plays briefly, then stops:** the driver's short-circuit/overcurrent protection tripping — commonly caused by SPK− not actually being wired to the speaker (it's differential, both legs matter). `resetSpeakerOnSCD(true)` lets it auto-recover instead of latching off, and polling `isSpeakerShorted()` / `readIRQflags()` shows why it tripped.
- **Clicking between notes ("tok, tok") during the melody-test stage:** caused by the output jumping abruptly to/from silence at each note boundary. Fixed with a short fade-in/fade-out envelope on each note.
- **`/littlefs/track.mp3 does not exist` after flashing:** flashing the sketch and flashing the filesystem image are separate PlatformIO targets (`upload` vs `uploadfs`) — easy to do one and forget the other.

## License

Firmware code in this repo: MIT (adjust as you like). Third-party libraries above retain their own licenses.
