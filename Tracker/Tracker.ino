#define EIDSP_QUANTIZE_FILTERBANK 0

#include <PDM.h>
#include <ArduinoBLE.h>
#include <Hey_Arduino_inferencing.h>
#include "Adafruit_FRAM_SPI.h"
#include "FramManager.h"

// Audio buffers, pointers and selectors
typedef struct {
  int16_t *buffer;
  uint8_t buf_ready;
  uint32_t buf_count;
  uint32_t n_samples;
} inference_t;

static inference_t inference;
static signed short sampleBuffer[1024];
static bool debug_nn = false;

volatile bool recording = false;
volatile int noiseCount = 0;

const int SR = 8000;
const int RECORD_BUFFER_LENGTH = 80000;
const int MTU = 244;
const int MAX_BLE_TRANSMIT_LENGTH = 30*MTU;
int8_t recordBuffer[RECORD_BUFFER_LENGTH];
int8_t BLEBuffer[MTU];
BLEService audioService("180C");
BLECharacteristic dataCharacteristic("2A6E", BLERead | BLENotify, MTU);
BLEIntCharacteristic idCharacteristic("2A7E", BLERead | BLENotify);
static BLEDevice central;
int connectTime = -15000;

volatile int startIdx = 0;
volatile int recordIdx = 0;
volatile int BLEIdx;
volatile int saveIdx;
int audioID;
int sentSamples;
int j = 0;
int k = 0;
int m = 0;
int length;
int count = 0;

uint8_t FRAM_CS = D10;
Adafruit_FRAM_SPI fram = Adafruit_FRAM_SPI(FRAM_CS);  // use hardware SPI
static FramManager framManager(fram);

const int FRAM_READ_SEND_LENGTH = 4000;

/**
 * @brief      Arduino setup function
 */
void setup() {
  Serial.begin(9600);

  // Memory setup
  if (framManager.begin()) {
    Serial.println("Found SPI FRAM");
  } else {
    Serial.println("No SPI FRAM found ... check your connections\r\n");
    while (1);
  }

  // BLE setup
  if (!BLE.begin()) {
    Serial.println("Failed to start BLE\r\n");
    while (1);
  }

  BLE.setLocalName("ArduinoBLE");
  BLE.setAdvertisedService(audioService);
  audioService.addCharacteristic(dataCharacteristic);
  audioService.addCharacteristic(idCharacteristic);
  BLE.addService(audioService);
  BLE.advertise();

  // PDM recording setup
  PDM.onReceive(&pdm_data_ready_inference_callback);
  PDM.setBufferSize(2048);

  if (!PDM.begin(1, EI_CLASSIFIER_FREQUENCY)) {
    PDM.end();
    while (1);
  }

  // Keyword detection setup
  inference.buffer = (int16_t *)malloc(EI_CLASSIFIER_RAW_SAMPLE_COUNT * sizeof(int16_t));
  inference.buf_ready = 1;
  inference.buf_count = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
}

void sendAudio(int fromIdx, int length) {
  Serial.print("Sending ");
  Serial.print(fromIdx);
  Serial.print(" - ");
  Serial.println(fromIdx + length);

  unsigned long start = millis();
  
  if (RECORD_BUFFER_LENGTH - fromIdx < length) {
    for (int i = fromIdx; i < RECORD_BUFFER_LENGTH; i++) {
      BLEBuffer[k++] = recordBuffer[i];
      if (k >= MTU) {
        dataCharacteristic.writeValue(BLEBuffer, sizeof(BLEBuffer), false);
        delay(20);
        k = 0;
      }
    }
    for (int i = 0; i < fromIdx + length - RECORD_BUFFER_LENGTH; i++) {
      BLEBuffer[k++] = recordBuffer[i];
      if (k >= MTU) {
        dataCharacteristic.writeValue(BLEBuffer, sizeof(BLEBuffer), false);
        delay(20);
        k = 0;
      }
    }
    if (k > 0) {
      dataCharacteristic.writeValue(BLEBuffer, k, false);
      delay(20);
    }
  } else {
    for (int i = fromIdx; i < fromIdx + length; i++) {
      BLEBuffer[k++] = recordBuffer[i];
      if (k >= MTU) {
        dataCharacteristic.writeValue(BLEBuffer, sizeof(BLEBuffer), false);
        delay(20);
        k = 0;
      }
    }
    if (k > 0) {
      dataCharacteristic.writeValue(BLEBuffer, k, false);
      delay(20);
    }
  }

  if ((length >= MTU*5) && (millis() - start > (float)length / SR * 1000)) {
    Serial.print("Disconnect due to weak connection");
    central.disconnect();
  }
}

void saveAudio(int fromIdx, int length) {
  Serial.print("Saving ");
  Serial.print(fromIdx);
  Serial.print(" - ");
  Serial.println(fromIdx + length);

  if (RECORD_BUFFER_LENGTH - fromIdx < length) {
    framManager.writeAudio((uint8_t *)&recordBuffer[fromIdx], RECORD_BUFFER_LENGTH - fromIdx);
    framManager.writeAudio((uint8_t *)recordBuffer, RECORD_BUFFER_LENGTH - fromIdx);
  } else {
    framManager.writeAudio((uint8_t *)&recordBuffer[fromIdx], length);
  }
}

int getLength(int idx, int MAX_LENGTH) {
  int d = recordIdx - idx;
  if (recordIdx < idx) {
    d += RECORD_BUFFER_LENGTH;
  }
  if (d > MAX_LENGTH) {
    return MAX_LENGTH;
  } else {
    return d;
  }
}

void loop() {
  if ((!central) || (!central.connected())) {
    connectTime = millis();
    if (!recording) {
      central = BLE.central();
    }
  }

  if ((!central) || (!central.connected())) {
    BLE.advertise();
  } else {
    BLE.stopAdvertise();
  }

  signal_t sig;
  sig.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  sig.get_data = &microphone_audio_signal_get_data;

  ei_impulse_result_t result = { 0 };

  if ((central && central.connected()) || (!framManager.framIsFull())) {
    EI_IMPULSE_ERROR r = run_classifier(&sig, &result, debug_nn);
    if (r != EI_IMPULSE_OK) {
      ei_printf("ERR: Failed to run classifier (%d)\n", r);
      return;
    }
  } else {
    return;
  }

  for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
    if (result.classification[i].value > 0.5) {
      ei_printf("%d. %s - %.3f\n", i, result.classification[i].label, result.classification[i].value);
      break;
    }
  }

  Serial.print("recordIdx = ");
  Serial.println(recordIdx);

  if (central && central.connected() && (millis() - connectTime > 15000)) {
    if (!recording) {  // not recording, listening for keywords
      BLEIdx = recordIdx;
      if (sentSamples > 0) {
        Serial.print(sentSamples);
        Serial.println(" samples sent");
        sentSamples = 0;
      }

      if (result.classification[0].value > 0.8) {
        digitalWrite(LED_BUILTIN, HIGH);
        recording = true;
        audioID = millis() / 1000;
        idCharacteristic.writeValue(audioID);
      } else if (framManager.hasAudioToRead()) {
        int readToIdx = 0;
        if (recordIdx > RECORD_BUFFER_LENGTH - SR) {
          readToIdx = recordIdx - FRAM_READ_SEND_LENGTH;
        }
        int readLength = framManager.readAudio((uint8_t *)&recordBuffer[readToIdx], FRAM_READ_SEND_LENGTH, &audioID);
        idCharacteristic.writeValue(audioID);
        delay(10);
        Serial.print("Sending audio ");
        Serial.println(audioID);
        sendAudio(readToIdx, readLength);
      } else {
        idCharacteristic.writeValue(-1);
        delay(300);
      }
    } else {  // recording - will repeat: send 1s of audio and run classifier
      idCharacteristic.writeValue(audioID);
      delay(100);
      int length = getLength(BLEIdx, MAX_BLE_TRANSMIT_LENGTH);
      sendAudio(BLEIdx, length);
      if (((!central) || (!central.connected())) && (!framManager.framIsFull())) {
        framManager.startWritingAudio(audioID);
        saveIdx = BLEIdx;
      }
      sentSamples += length;
      BLEIdx += length;
    
      if (BLEIdx >= RECORD_BUFFER_LENGTH) {
        BLEIdx -= RECORD_BUFFER_LENGTH;
      }

      if (result.classification[1].value > 0.3) {
        noiseCount++;
        if (noiseCount >= 5) {  // stop recording if it's always noise
          digitalWrite(LED_BUILTIN, LOW);
          idCharacteristic.writeValue(-1);
          recording = false;
          noiseCount = 0;
        }
      } else {
        noiseCount = 0;
      }
    }

  #if EI_CLASSIFIER_HAS_ANOMALY == 1
    ei_printf("    anomaly score: %.3f\n", result.anomaly);
  #endif
  } else {
    if (framManager.framIsFull()) {
      Serial.println("Fram is full");
      delay(10000);
    } else if (!recording) {  // not recording, listening for keywords
      saveIdx = recordIdx;
      if (saveIdx >= RECORD_BUFFER_LENGTH) {
        saveIdx -= RECORD_BUFFER_LENGTH;
      }

      if (result.classification[0].value > 0.8) {
        digitalWrite(LED_BUILTIN, HIGH);
        audioID = millis() / 1000;
        framManager.startWritingAudio(audioID);
        recording = true;
      } else {
        delay(300);
      }
    } else {  // recording - will repeat: save 1s of audio and run classifier
      int length = getLength(saveIdx, SR*2);
      saveAudio(saveIdx, length);
      saveIdx += length;
    
      if (saveIdx >= RECORD_BUFFER_LENGTH) {
        saveIdx -= RECORD_BUFFER_LENGTH;
      }

      if (result.classification[1].value > 0.3) {
        noiseCount++;
      } else {
        noiseCount = 0;
      }
      if ((noiseCount >= 5) || framManager.framIsFull()) {  // stop recording if it's always noise
        digitalWrite(LED_BUILTIN, LOW);
        framManager.endWritingAudio();
        recording = false;
        noiseCount = 0;
      }
      delay(100);
    }
  }
}

int8_t get8BitSample(int16_t sample) {
  if (sample >= 0x400) {
    return 0x7F;
  } else if (sample < -0x400) {
    return -0x80;
  }
  int8_t result = (sample >> 3) & 0xFF;
  return result;
}

/**
 * @brief      PDM buffer full callback
 *             Get data and call audio thread callback
 */
static void pdm_data_ready_inference_callback(void) {
  int bytesAvailable = PDM.available();
  int bytesRead = PDM.read((char *)&sampleBuffer[0], bytesAvailable);

  for (int i = 0; i < bytesRead >> 1; i++) {
    inference.buffer[j++] = sampleBuffer[i];
    if (j >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
      j = 0;
    }
    if (i % 2) {
      recordBuffer[m++] = get8BitSample(sampleBuffer[i]);  // record buffer is 8kHz, 8-bit samples
      if (m >= RECORD_BUFFER_LENGTH) {
        m = 0;
      }
    }
  }

  startIdx = j;
  recordIdx = m;
}

/**
 * Get raw audio signal data
 */
static int microphone_audio_signal_get_data(size_t offset, size_t length, float *out_ptr) {
  int fromIdx = offset + startIdx;
  if (fromIdx >= EI_CLASSIFIER_RAW_SAMPLE_COUNT) {
    fromIdx -= EI_CLASSIFIER_RAW_SAMPLE_COUNT;
  }

  if (length > EI_CLASSIFIER_RAW_SAMPLE_COUNT - fromIdx) {
    int readLength = EI_CLASSIFIER_RAW_SAMPLE_COUNT - fromIdx;
    numpy::int16_to_float(&inference.buffer[fromIdx], out_ptr, readLength);
    numpy::int16_to_float(inference.buffer, out_ptr + readLength, length - readLength);
  } else {
    numpy::int16_to_float(&inference.buffer[fromIdx], out_ptr, length);
  }

  return 0;
}

#if !defined(EI_CLASSIFIER_SENSOR) || EI_CLASSIFIER_SENSOR != EI_CLASSIFIER_SENSOR_MICROPHONE
#error "Invalid model for current sensor."
#endif
