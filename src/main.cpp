// TLV320DAC3100 MP3 player — ESP32-S3 (Freenove ESP32-S3-WROOM, 8MB flash + 8MB PSRAM)
// PlatformIO version — see platformio.ini for board, partition, PSRAM,
// filesystem and library setup; nothing to configure by hand in an IDE menu.
// The pin map below (I2C on 8/9, I2S on 39/40/41, reset on 12) is unaffected
// by the board swap — all remain plain GPIOs on the Freenove board too.
//
// Plays an MP3 stored in onboard flash (LittleFS) through the external
// TLV320DAC3100 DAC over I2S. MP3 decoding + I2S output is handled by the
// ESP32-audioI2S library.
//
// One note on the audio library: as of v3.4.3+ it outputs at the *source's*
// sample rate by default (44.1kHz for this file) — which is what the codec
// PLL settings below are tuned for. If `SR_48K` ends up defined in the
// fetched library's Audio.h, comment that out, or playback will be
// pitched/sped up wrong.
//
// Build + flash:
//   pio run --target upload        (flashes this sketch)
//   pio run --target uploadfs      (flashes data/track.mp3 to LittleFS —
//                                    a separate step, needed once, or again
//                                    whenever the audio file changes)
//
// Wiring reminder:
//   - VIN: 3.3–5V. Headphone-only output works from 3.3V; if you want
//     the speaker (JST) output, VIN needs 5V.
//   - RST is active-low — held low briefly at boot, then released.

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_TLV320DAC3100.h>
#include <Audio.h>
#include <LittleFS.h>

// ---- Pin map ----
#define PIN_I2C_SDA  8
#define PIN_I2C_SCL  9
#define PIN_I2S_BCK  39
#define PIN_I2S_WS   40
#define PIN_I2S_DIN  41   // DAC's DIN pin — i.e. I2S *data out* from the ESP32
#define PIN_DAC_RST  12

// Renamed from the original upload — LittleFS on ESP32 has a short path
// length limit, and the original filename was too long for it.
#define TRACK_PATH "/track.mp3"

Adafruit_TLV320DAC3100 codec;
Audio audio;

void i2cScan() {
  Serial.println("Scanning I2C bus...");
  uint8_t found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X%s\n", addr,
                     addr == TLV320DAC3100_I2CADDR_DEFAULT ? "  <-- TLV320DAC3100" : "");
      found++;
    }
  }
  if (found == 0) {
    Serial.println("  Nothing found — check wiring, pull-ups, and power.");
  }
}

void setup() {
  Serial.begin(115200);
  uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  Serial.println("\nTLV320DAC3100 MP3 player");

  // Hardware reset (active low)
  pinMode(PIN_DAC_RST, OUTPUT);
  digitalWrite(PIN_DAC_RST, LOW);
  delay(10);
  digitalWrite(PIN_DAC_RST, HIGH);
  delay(10);

  // I2C bus on the given pins
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  i2cScan();

  Serial.print("Initializing codec... ");
  if (!codec.begin(TLV320DAC3100_I2CADDR_DEFAULT, &Wire)) {
    Serial.println("FAILED — codec did not respond over I2C.");
    while (1) delay(1000);
  }
  Serial.println("OK");
  codec.reset();

  // --- Codec interface + clocking (PLL fed from BCLK, tuned for 44.1kHz) ---
  codec.setCodecInterface(TLV320DAC3100_FORMAT_I2S, TLV320DAC3100_DATA_LEN_16);
  codec.setCodecClockInput(TLV320DAC3100_CODEC_CLKIN_PLL);
  codec.setPLLClockInput(TLV320DAC3100_PLL_CLKIN_BCLK);
  codec.setPLLValues(1, 1, 8, 0);
  codec.setNDAC(true, 8);
  codec.setMDAC(true, 2);
  codec.setDOSR(128);
  codec.powerPLL(true);

  // --- DAC data path ---
  codec.setDACDataPath(true, true,
                        TLV320_DAC_PATH_NORMAL, TLV320_DAC_PATH_NORMAL,
                        TLV320_VOLUME_STEP_1SAMPLE);
  codec.configureAnalogInputs(TLV320_DAC_ROUTE_MIXER, TLV320_DAC_ROUTE_MIXER,
                               false, false, false, false);
  codec.setDACVolumeControl(false, false, TLV320_VOL_INDEPENDENT); // unmute
  codec.setChannelVolume(false, 0.0);  // left,  0 dB
  codec.setChannelVolume(true, 0.0);   // right, 0 dB

  // --- Headphone output ---
  codec.configureHeadphoneDriver(true, true, TLV320_HP_COMMON_1_35V, false);
  codec.setHPLVolume(true, 0);
  codec.setHPRVolume(true, 0);
  codec.configureHPL_PGA(9, true);
  codec.configureHPR_PGA(9, true);

  // --- Speaker output (only if VIN is powered at 5V) ---
  codec.enableSpeaker(true);
  codec.configureSPK_PGA(TLV320_SPK_GAIN_6DB, true);
  codec.setSPKVolume(true, 0);
  // Auto-recover from a short-circuit-detect trip instead of latching off.
  codec.resetSpeakerOnSCD(true);

  // --- Flash filesystem holding the MP3 ---
  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount FAILED");
    while (1) delay(1000);
  }
  if (!LittleFS.exists(TRACK_PATH)) {
    Serial.println("Track not found on LittleFS — did `pio run -t uploadfs` run?");
    while (1) delay(1000);
  }

  // --- MP3 decoder + I2S output (handled entirely by the Audio library) ---
  audio.setPinout(PIN_I2S_BCK, PIN_I2S_WS, PIN_I2S_DIN);
  audio.setVolume(21); // library's own gain (0-21) — fine-tune loudness via the codec instead
  audio.connecttoFS(LittleFS, TRACK_PATH);

  Serial.println("Playing.");
}

void loop() {
  audio.loop();

  // Diagnostic: check every ~500ms whether the driver flagged a fault.
  static uint32_t lastCheck = 0;
  if (millis() - lastCheck > 500) {
    lastCheck = millis();
    if (codec.isSpeakerShorted()) {
      Serial.println("!! Speaker driver reports short-circuit / overcurrent");
    }
    if (codec.isOvertemperature()) {
      Serial.println("!! Codec reports over-temperature");
    }
    uint8_t irq = codec.readIRQflags(true);
    if (irq) {
      Serial.printf("!! Sticky IRQ flags: 0x%02X\n", irq);
    }
  }

  vTaskDelay(1); // yield to other tasks, as the library's own examples do
}

// --- ESP32-audioI2S optional callbacks (weak-linked; called if defined) ---
void audio_info(const char *info) {
  Serial.print("audio_info: ");
  Serial.println(info);
}

void audio_eof_mp3(const char *info) {
  Serial.println("Track finished — looping.");
  audio.connecttoFS(LittleFS, TRACK_PATH);
}
