#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "time.h"
#include <ArduinoJson.h>
#include <driver/i2s.h>
#include <SD.h>
#include <SPI.h>
#include <WiFiClientSecure.h>
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// Forward declarations for tasks
void recordTask(void *arg);
void transcriptionTask(void *arg);
void ttsTask(void *arg);
void transcribeWithDeepgram();
void cleanupAudio();

// WiFi Credentials
const char* ssid = "SSID";
const char* password = "Password";

// API Keys
const char* OPENAI_API_KEY = "OpenAIAPI";
const char* WEATHER_API_KEY = "OpenWeatherMapAPI";
#define DEEPGRAM_API_KEY "DeepGramAPI"

// Location for Weather
const char* city = "Dhaka";
const char* countryCode = "BD";

// OLED Display Configuration
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

// OpenWeatherMap API URL
String weatherURL = "http://api.openweathermap.org/data/2.5/weather?q=" + String(city) + "," + String(countryCode) + "&units=metric&appid=" + String(WEATHER_API_KEY);

// NTP Server for Time Sync
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 6 * 3600; // GMT+6 (Bangladesh)
const int daylightOffset_sec = 0;

// Mode flag for AI interaction
bool aiMode = false;
float temperature = 0.0;
String weatherCondition = "";

// SD Card pins
#define SD_CS 5
#define SPI_MOSI 23
#define SPI_MISO 19
#define SPI_SCK 18

// Voice Recording Configuration (I2S microphone)
#define I2S_MIC_WS 25
#define I2S_MIC_SD 35
#define I2S_MIC_SCK 15
#define I2S_MIC_PORT I2S_NUM_0

#define I2S_SAMPLE_RATE (16000)
#define I2S_SAMPLE_BITS (16)
#define I2S_READ_LEN (8 * 1024)
#define I2S_CHANNEL_NUM (1)
#define RECORD_TIME (10)
#define FLASH_RECORD_SIZE (I2S_CHANNEL_NUM * I2S_SAMPLE_RATE * I2S_SAMPLE_BITS / 8 * RECORD_TIME)

// Speaker (I2S DAC) Configuration
#define I2S_SPEAKER_BCLK 26
#define I2S_SPEAKER_LRC 25
#define I2S_SPEAKER_DIN 22
#define I2S_SPEAKER_PORT I2S_NUM_1

// Pin to trigger voice recording
#define VOICE_TRIGGER_PIN 13

// TTS Settings
const String ttsLanguage = "en";
const String ttsFilePath = "/tts_response.mp3";

// Audio objects for TTS playback
AudioGeneratorMP3 *mp3 = nullptr;
AudioFileSourceSD *audioFile = nullptr;
AudioOutputI2S *audioOut = nullptr;

// Current TTS state
enum TTSState {DOWNLOADING, PLAYING, IDLE};
TTSState ttsState = IDLE;

// SD Card and Recording variables
File file;
const char recordingFilename[] = "/recording.wav";
const int headerSize = 44;
bool isRecording = false;
String latestTranscript = "";
String aiResponseText = "";

// Deepgram API settings
#define DEEPGRAM_API_URL "https://api.deepgram.com/v1/listen?punctuate=true&model=general&tier=enhanced"

// Task handles
TaskHandle_t recordingTaskHandle = NULL;
TaskHandle_t transcriptionTaskHandle = NULL;
TaskHandle_t ttsTaskHandle = NULL;

// Flag to track if system is busy processing
bool systemBusy = false;

// Connect to WiFi
void connectWiFi() {
  Serial.print("Connecting to WiFi...");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi connection failed");
  }
}

// Fetch Time (Using NTP)
String getFormattedTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    return "Time Error";
  }
  char timeString[16];
  strftime(timeString, sizeof(timeString), "%I:%M:%S %p", &timeinfo);
  return String(timeString);
}

// Fetch Weather
void getWeather() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(weatherURL);
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      JsonDocument doc;
      deserializeJson(doc, payload);
      temperature = doc["main"]["temp"].as<float>();
      weatherCondition = doc["weather"][0]["description"].as<String>();
      Serial.println("Weather updated: " + String(temperature) + "°C, " + weatherCondition);
    } else {
      Serial.println("Weather API Error: " + String(httpCode));
    }
    http.end();
  } else {
    Serial.println("WiFi Disconnected");
  }
}

// Ask AI using ChatGPT API
String askChatGPT(String userQuery) {
  HTTPClient http;
  String apiUrl = "https://api.openai.com/v1/chat/completions";
  
  JsonDocument doc;
  doc["model"] = "gpt-3.5-turbo";
  JsonArray messages = doc["messages"].to<JsonArray>();

  JsonObject systemMessage = messages.add<JsonObject>();
  systemMessage["role"] = "system";
  systemMessage["content"] = "You are a helpful assistant. Provide concise responses suitable for display on a small screen and for text-to-speech reading.";
  
  JsonObject userMessage = messages.add<JsonObject>();
  userMessage["role"] = "user";
  userMessage["content"] = userQuery;

  doc["temperature"] = 0.7;
  doc["max_tokens"] = 150;

  String requestBody;
  serializeJson(doc, requestBody);

  http.begin(apiUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Bearer " + String(OPENAI_API_KEY));

  int httpCode = 0;
  String response = "AI Error";
  int retries = 3;
  
  for (int i = 0; i < retries; i++) {
    httpCode = http.POST(requestBody);
    if (httpCode == 200) {
      String payload = http.getString();
      JsonDocument responseDoc;
      DeserializationError error = deserializeJson(responseDoc, payload);
      if (!error) {
        response = responseDoc["choices"][0]["message"]["content"].as<String>();
        break;
      } else {
        Serial.println("JSON parsing error");
      }
    } else {
      Serial.println("HTTP request failed, status code: " + String(httpCode));
      Serial.println("Response Body: " + http.getString());
    }
    delay(2000);
  }
  http.end();
  return response;
}

// Update OLED Display
void updateOLED(String line1, String line2, String line3 = "", String line4 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB10_tr);
  u8g2.drawStr(0, 15, line1.c_str());
  u8g2.drawStr(0, 35, line2.c_str());
  if (line3.length() > 0) {
    u8g2.drawStr(0, 55, line3.c_str());
  }
  if (line4.length() > 0) {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 64, line4.c_str());
  }
  u8g2.sendBuffer();
}

// Display AI Response (with scrolling for long responses)
void displayAIResponse(String response) {
  const int maxLength = 16;
  int totalLines = (response.length() + maxLength - 1) / maxLength;
  
  for (int i = 0; i < totalLines; i += 2) {
    String line1 = (i < totalLines) ? response.substring(i * maxLength, min((i + 1) * maxLength, (int)response.length())) : "";
    String line2 = ((i + 1) < totalLines) ? response.substring((i + 1) * maxLength, min((i + 2) * maxLength, (int)response.length())) : "";
    
    updateOLED("AI Response:", line1, line2, "Page " + String((i / 2) + 1) + "/" + String((totalLines + 1) / 2));
    delay(3000);
  }

  aiResponseText = response;
  xTaskCreate(ttsTask, "ttsTask", 16384, NULL, 1, &ttsTaskHandle);
  Serial.println("AI response displayed, starting TTS playback");
}

// Initialize I2S for microphone
void i2sMicInit() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = I2S_SAMPLE_RATE,
    .bits_per_sample = i2s_bits_per_sample_t(I2S_SAMPLE_BITS),
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 1024,
  };

  i2s_driver_install(I2S_MIC_PORT, &i2s_config, 0, NULL);

  const i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_MIC_SCK,
    .ws_io_num = I2S_MIC_WS,
    .data_out_num = -1,
    .data_in_num = I2S_MIC_SD
  };
  
  i2s_set_pin(I2S_MIC_PORT, &pin_config);
}

// WAV Header Generator
void wavHeader(byte *header, int wavSize) {
  header[0] = 'R'; header[1] = 'I'; header[2] = 'F'; header[3] = 'F';
  unsigned int fileSize = wavSize + headerSize - 8;
  header[4] = (byte)(fileSize & 0xFF);
  header[5] = (byte)((fileSize >> 8) & 0xFF);
  header[6] = (byte)((fileSize >> 16) & 0xFF);
  header[7] = (byte)((fileSize >> 24) & 0xFF);
  header[8] = 'W'; header[9] = 'A'; header[10] = 'V'; header[11] = 'E';
  header[12] = 'f'; header[13] = 'm'; header[14] = 't'; header[15] = ' ';
  header[16] = 0x10; header[17] = 0x00; header[18] = 0x00; header[19] = 0x00;
  header[20] = 0x01; header[21] = 0x00; header[22] = 0x01; header[23] = 0x00;
  header[24] = 0x80; header[25] = 0x3E; header[26] = 0x00; header[27] = 0x00;
  header[28] = 0x00; header[29] = 0x7D; header[30] = 0x01; header[31] = 0x00;
  header[32] = 0x02; header[33] = 0x00; header[34] = 0x10; header[35] = 0x00;
  header[36] = 'd'; header[37] = 'a'; header[38] = 't'; header[39] = 'a';
  header[40] = (byte)(wavSize & 0xFF);
  header[41] = (byte)((wavSize >> 8) & 0xFF);
  header[42] = (byte)((wavSize >> 16) & 0xFF);
  header[43] = (byte)((wavSize >> 24) & 0xFF);
}

// Audio Data Scaling
void i2s_adc_data_scale(uint8_t *d_buff, uint8_t *s_buff, uint32_t len) {
  uint32_t j = 0;
  for (int i = 0; i < len; i += 2) {
    uint32_t dac_value = ((((uint16_t)(s_buff[i + 1] & 0xf) << 8) | ((s_buff[i + 0]))));
    d_buff[j++] = 0;
    d_buff[j++] = dac_value * 256 / 2048;
  }
}

// Clean up audio resources
void cleanupAudio() {
  if (mp3) {
    mp3->stop();
    delete mp3;
    mp3 = nullptr;
  }
  if (audioFile) {
    delete audioFile;
    audioFile = nullptr;
  }
  if (audioOut) {
    delete audioOut;
    audioOut = nullptr;
  }

  i2sMicInit();
}

// Transcribe audio using Deepgram API
void transcribeWithDeepgram() {
  Serial.println("Starting Deepgram transcription...");
  file = SD.open(recordingFilename, FILE_READ);
  
  if (!file) {
    Serial.println("Failed to open file for transcription!");
    latestTranscript = "";
    return;
  }

  HTTPClient client;
  client.begin(DEEPGRAM_API_URL);
  client.addHeader("Content-Type", "audio/wav");
  client.addHeader("Authorization", "Token " + String(DEEPGRAM_API_KEY));
  client.setTimeout(30000);

  int httpResponseCode = 0;

  if (file.size() > 1024 * 1024) {
    Serial.println("Large file detected, using chunked upload");
    client.addHeader("Transfer-Encoding", "chunked");
    httpResponseCode = client.sendRequest("POST", &file, file.size());
  } else {
    httpResponseCode = client.sendRequest("POST", &file, file.size());
  }

  if (httpResponseCode == 200) {
    String response = client.getString();
    Serial.println("Response received. Processing...");

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);

    if (!error) {
      latestTranscript = doc["results"]["channels"][0]["alternatives"][0]["transcript"].as<String>();
      Serial.println("Transcription: " + latestTranscript);

      File transcriptFile = SD.open("/transcript.txt", FILE_WRITE);
      if (transcriptFile) {
        transcriptFile.println(latestTranscript);
        transcriptFile.close();
        Serial.println("Transcript saved to SD card");
      }
    } else {
      Serial.print("JSON parsing failed: ");
      Serial.println(error.c_str());
      latestTranscript = "";
    }
  } else {
    Serial.print("Error in HTTP request: ");
    Serial.println(httpResponseCode);
    latestTranscript = "";
  }

  file.close();
  client.end();
}

// Start Voice Recording
void startRecording() {
  if (isRecording || systemBusy) {
    Serial.println("System busy, cannot start recording");
    return;
  }
  
  aiMode = true;
  isRecording = true;
  systemBusy = true;

  SD.remove(recordingFilename);
  file = SD.open(recordingFilename, FILE_WRITE);
  
  if (!file) {
    Serial.println("Failed to open file for recording!");
    isRecording = false;
    aiMode = false;
    systemBusy = false;
    return;
  }

  byte header[headerSize];
  wavHeader(header, FLASH_RECORD_SIZE);
  file.write(header, headerSize);

  xTaskCreate(recordTask, "recordTask", 8192, NULL, 2, &recordingTaskHandle);
  Serial.println("Recording started...");
  updateOLED("Voice Assistant", "Listening...", "Speak now");
}

// Recording Task
void recordTask(void *arg) {
  int i2s_read_len = I2S_READ_LEN;
  size_t bytes_read;
  int flash_wr_size = 0;
  char *i2s_read_buff = (char *)calloc(i2s_read_len, sizeof(char));
  uint8_t *flash_write_buff = (uint8_t *)calloc(i2s_read_len, sizeof(char));

  if (i2s_read_buff == NULL || flash_write_buff == NULL) {
    Serial.println("Memory allocation failed!");
    isRecording = false;
    systemBusy = false;
    vTaskDelete(NULL);
    return;
  }

  unsigned long startTime = millis();

  while (isRecording && flash_wr_size < FLASH_RECORD_SIZE) {
    i2s_read(I2S_MIC_PORT, (void *)i2s_read_buff, i2s_read_len, &bytes_read, portMAX_DELAY);
    i2s_adc_data_scale(flash_write_buff, (uint8_t *)i2s_read_buff, i2s_read_len);
    file.write((const byte *)flash_write_buff, i2s_read_len);
    flash_wr_size += i2s_read_len;

    if (millis() - startTime > RECORD_TIME * 1000) {
      isRecording = false;
    }
    
    vTaskDelay(1);
  }

  file.close();
  free(i2s_read_buff);
  free(flash_write_buff);

  Serial.println("Recording finished. Starting transcription...");
  updateOLED("Processing...", "Transcribing", "audio");
  isRecording = false;

  xTaskCreate(transcriptionTask, "transcription", 16384, NULL, 1, &transcriptionTaskHandle);
  vTaskDelete(NULL);
}

// Transcription Task
void transcriptionTask(void *arg) {
  transcribeWithDeepgram();

  if (latestTranscript.length() > 0) {
    Serial.println("Processing transcript: " + latestTranscript);
    updateOLED("Asking AI...", "Please wait");

    String aiResponse = askChatGPT(latestTranscript);
    aiResponseText = aiResponse;
    Serial.println("AI Response: " + aiResponse);
    displayAIResponse(aiResponse);
  } else {
    updateOLED("No valid speech", "detected", "Try again");
    delay(2000);
    aiMode = false;
    systemBusy = false;
  }
  
  vTaskDelete(NULL);
}

// URL encode text for TTS
String urlEncode(const String& str) {
  String encoded = "";
  char c;
  char buf[4];
  
  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      sprintf(buf, "%%%02X", c);
      encoded += buf;
    }
  }
  return encoded;
}

// Download TTS file from Google TTS
bool downloadTTSFile(const String& text, const String& path) {
  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure();

  String encodedText = urlEncode(text);
  String url = "https://translate.google.com/translate_tts?ie=UTF-8&q=" + encodedText + "&tl=" + ttsLanguage + "&client=tw-ob&ttsspeed=1";

  Serial.printf("Downloading TTS: %s\n", url.c_str());
  http.begin(client, url);
  http.addHeader("User-Agent", "Mozilla/5.0");

  int httpCode = http.GET();
  bool success = false;

  if (httpCode == HTTP_CODE_OK) {
    if (SD.exists(path.c_str())) {
      SD.remove(path.c_str());
    }

    File file = SD.open(path.c_str(), FILE_WRITE);
    if (!file) {
      Serial.println("Failed to open file for writing");
      http.end();
      return false;
    }

    int len = http.getSize();
    uint8_t buffer[512];
    int totalBytes = 0;

    while (http.getStreamPtr()->available()) {
      size_t size = http.getStreamPtr()->readBytes(buffer, sizeof(buffer));
      file.write(buffer, size);
      totalBytes += size;
      Serial.printf("Downloaded: %d%%\r", (totalBytes * 100) / len);
    }

    file.close();
    Serial.println("\nTTS Download complete! File saved to SD card.");
    success = true;
  } else {
    Serial.printf("HTTP request failed. Code: %d\n", httpCode);
  }

  http.end();
  return success;
}

// Play MP3 file from SD card
bool playMP3File(const String& path) {
  i2s_driver_uninstall(I2S_MIC_PORT);
  delay(100);

  audioFile = new AudioFileSourceSD(path.c_str());
  if (!audioFile->isOpen()) {
    Serial.println("Failed to open MP3 file!");
    if (audioFile) delete audioFile;
    audioFile = nullptr;
    i2sMicInit();
    return false;
  }

  audioOut = new AudioOutputI2S();
  audioOut->SetPinout(I2S_SPEAKER_BCLK, I2S_SPEAKER_LRC, I2S_SPEAKER_DIN);
  audioOut->SetGain(1.0);

  mp3 = new AudioGeneratorMP3();
  Serial.println("Starting TTS playback...");

  if (!mp3->begin(audioFile, audioOut)) {
    Serial.println("MP3 begin failed!");
    cleanupAudio();
    i2sMicInit();
    return false;
  }

  return true;
}

// TTS Task to handle Text-to-Speech download and playback
void ttsTask(void *arg) {
  updateOLED("Playing", "Response", "via TTS...");

  ttsState = DOWNLOADING;
  if (downloadTTSFile(aiResponseText, ttsFilePath)) {
    ttsState = PLAYING;
    if (playMP3File(ttsFilePath)) {
      while (mp3->isRunning()) {
        if (!mp3->loop()) {
          break;
        }
        vTaskDelay(1);
      }
    } else {
      Serial.println("Failed to play MP3 file");
    }
  } else {
    Serial.println("Failed to download TTS file");
  }

  ttsState = IDLE;
  cleanupAudio();
  aiMode = false;
  systemBusy = false;
  Serial.println("TTS playback completed");
  vTaskDelete(NULL);
}

void setup() {
  Serial.begin(115200);

  u8g2.begin();
  updateOLED("Initializing", "Voice Assistant", "Please wait...");

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    updateOLED("SD Card Error", "Check wiring", "Restart device");
    while (1) yield();
  }
  Serial.println("SD Card initialized successfully");

  i2sMicInit();

  updateOLED("Connecting WiFi", "Please wait...");
  connectWiFi();

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  updateOLED("System Ready", "Getting data...");

  getWeather();

  pinMode(VOICE_TRIGGER_PIN, INPUT_PULLUP);

  aiMode = false;
  isRecording = false;
  systemBusy = false;
  ttsState = IDLE;

  Serial.println("Setup complete. System is ready for voice commands.");
  Serial.println("Press 'S' in Serial Monitor to start recording");
  delay(2000);
}

void loop() {
  static unsigned long lastWeatherUpdate = 0;
  unsigned long currentMillis = millis();

  if (currentMillis - lastWeatherUpdate >= 300000) {
    getWeather();
    lastWeatherUpdate = currentMillis;
  }

  if (digitalRead(VOICE_TRIGGER_PIN) == LOW && !isRecording && !systemBusy) {
    Serial.println("Button pressed, starting voice recording...");
    startRecording();
    delay(500);
  }

  if (Serial.available()) {
    char input = Serial.read();
    if (input == 'S' || input == 's') {
      Serial.println("Starting voice recording...");
      startRecording();
    }
  }

  if (!aiMode && !isRecording && !systemBusy && ttsState == IDLE) {
    String currentTime = getFormattedTime();
    String tempStr = "Temp: " + String(temperature, 1) + " C";
    String weatherStr = weatherCondition;

    if (weatherStr.length() > 16) {
      weatherStr = weatherStr.substring(0, 13) + "...";
    }

    updateOLED("Time: " + currentTime, tempStr, weatherStr);
  }

  delay(100);
}