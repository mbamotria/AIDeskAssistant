# ESP32 Voice Assistant with AI Integration

A full-featured ESP32-based voice assistant that records audio, transcribes speech using Deepgram, generates intelligent responses using OpenAI’s Chat Completion API, and plays responses using text-to-speech — all while displaying system status, time, and weather on an OLED display.

This project focuses on **reliable voice interaction**, **modular task-based design**, and **practical embedded system integration** rather than experimental features.

---

## Key Features

* **Voice Input via I2S Microphone**

  * High-quality audio capture using an I2S MEMS microphone
  * WAV recording stored locally on SD card

* **Speech-to-Text (STT)**

  * Audio transcription using Deepgram’s speech recognition API

* **AI Response Engine**

  * Uses OpenAI’s Chat Completion API (gpt-3.5-turbo)
  * Responses optimized for short spoken output and small displays

* **Text-to-Speech (TTS) Playback**

  * Uses Google Translate’s TTS endpoint (unofficial)
  * Audio streamed and played through I2S DAC + speaker

* **OLED Display Interface**

  * Displays time, weather, system messages, and AI responses
  * Optimized for 128×64 SSD1306 screens

* **FreeRTOS-Based Multitasking**

  * Separate tasks for recording, transcription, and playback
  * Non-blocking system operation

* **SD Card Storage**

  * Stores WAV recordings, transcripts, and TTS audio files

---

## Hardware Requirements

### Core Components

* ESP32 Dev Board (ESP32-WROOM recommended)
* I2S MEMS Microphone (e.g., INMP441)
* I2S DAC / Amplifier (MAX98357A or similar)
* 4–8Ω Speaker
* 128×64 OLED Display (SSD1306, I2C)
* MicroSD Card Module (SPI)
* Push Button (for voice trigger)
* 5V / 2A Power Supply

---

## Pin Configuration

### I2S Microphone

| Signal | GPIO |
| ------ | ---- |
| BCLK   | 15   |
| WS     | 25   |
| SD     | 35   |

### I2S Speaker

| Signal | GPIO |
| ------ | ---- |
| BCLK   | 26   |
| LRC    | 25   |
| DIN    | 22   |

### OLED (I2C)

| Signal | GPIO |
| ------ | ---- |
| SDA    | 21   |
| SCL    | 22   |

### SD Card (SPI)

| Signal | GPIO |
| ------ | ---- |
| CS     | 5    |
| MOSI   | 23   |
| MISO   | 19   |
| SCK    | 18   |

### Control Input

| Function             | GPIO |
| -------------------- | ---- |
| Voice Trigger Button | 13   |

---

## Circuit Diagram
![Circuit Diagram](images/AIDeskAssistant_Diagram.png)

---

## Software Dependencies

### PlatformIO Libraries

```ini
lib_deps =
  olikraus/U8g2
  bblanchon/ArduinoJson
  earlephilhower/ESP8266Audio
```

### Built-In ESP32 Libraries

* WiFi
* HTTPClient
* SPI / SD
* FreeRTOS (ESP-IDF)

---

## Required API Keys

You must provide valid API keys for:

* **OpenAI API** – Chat responses
* **Deepgram API** – Speech-to-text
* **OpenWeatherMap API** – Weather information

All keys are defined directly in the source code for simplicity.

---

## System Workflow

1. Button press triggers recording
2. Audio recorded via I2S → saved as WAV
3. Audio sent to Deepgram for transcription
4. Transcription sent to OpenAI Chat API
5. AI response displayed on OLED
6. Response converted to speech and played through speaker
7. System returns to idle state

---

## System Architecture Overview

```
Mic → I2S → WAV File → Deepgram STT
                          ↓
                    OpenAI Chat API
                          ↓
                  Google TTS (MP3)
                          ↓
                 I2S DAC → Speaker
```

---

## Display Behavior

* Idle mode shows:

  * Current time (via NTP)
  * Weather temperature + condition
* During interaction:

  * Listening status
  * Transcribing indicator
  * AI response (paged text)
  * Playback status

---

## Known Limitations

* No wake-word detection (button required)
* Uses unofficial Google TTS endpoint (may break)
* ESP32 memory limits restrict response length
* Not suitable for long conversations
* Requires stable 2.4GHz WiFi connection
* Not optimized for battery-powered use

---

## Tested Configuration

* ESP32-WROOM-32
* 16kHz mono audio
* 10-second max recording length
* FAT32 SD card (Class 10 recommended)

---

## Future Improvements

* Wake-word detection
* Offline TTS support
* Conversation memory
* Web-based configuration portal
* Power management / deep sleep
* OTA firmware updates

---

## Final Notes

This project is designed as a **learning-focused, modular voice assistant**, not a commercial smart speaker.
It prioritizes transparency, debuggability, and extensibility over abstraction.

If you understand this project, you understand **embedded systems + networking + real-world AI integration**.
