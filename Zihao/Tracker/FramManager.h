#ifndef FRAMMANAGER_H  // Include guard to prevent multiple inclusions
#define FRAMMANAGER_H

#include "Adafruit_FRAM_SPI.h"
#include <SPI.h>

class FramManager {
  private:
    Adafruit_FRAM_SPI& fram;
    size_t SIZE;
    uint32_t writeIdx;
    size_t spaceAvailable;
    
    struct AudioRec {
      uint32_t idx;
      uint time;
      AudioRec* next;
    };
    
    AudioRec *audioRecsHead;
    AudioRec *audioRecsTail;

    int32_t readBack(uint32_t addr, int32_t data) {
      int32_t check = !data;
      int32_t wrapCheck, backup;
      fram.read(addr, (uint8_t *)&backup, sizeof(int32_t));
      fram.writeEnable(true);
      fram.write(addr, (uint8_t *)&data, sizeof(int32_t));
      fram.writeEnable(false);
      fram.read(addr, (uint8_t *)&check, sizeof(int32_t));
      fram.read(0, (uint8_t *)&wrapCheck, sizeof(int32_t));
      fram.writeEnable(true);
      fram.write(addr, (uint8_t *)&backup, sizeof(int32_t));
      fram.writeEnable(false);
      // Check for warparound, address 0 will work anyway
      if (wrapCheck == check)
        check = 0;
      return check;
    }

    uint32_t getMemSize() {
      uint32_t memSize = 0;
      while (readBack(memSize, memSize) == memSize) {
        memSize += 256;
      }
      return memSize;
    }

  public:
    FramManager(Adafruit_FRAM_SPI& f) : fram(f) {
      writeIdx = 0;
      audioRecsHead = new AudioRec{writeIdx, 0, nullptr};
      audioRecsTail = audioRecsHead;
    }

    bool begin() {
      if (fram.begin()) {
        SIZE = getMemSize();
        spaceAvailable = SIZE;
        return true;
      }
      return false;
    }

    bool framIsFull() {
      return spaceAvailable == 0;
    }

    bool hasAudioToRead() {
      return audioRecsHead->next != nullptr;
    }

    void startWritingAudio(int time) {
      audioRecsTail->time = time;
    }

    void writeAudio(const uint8_t *values, size_t length) {
      if (framIsFull()) {
        return;
      } else if (spaceAvailable < length) {
        length = spaceAvailable;
        spaceAvailable = 0;
      } else {
        spaceAvailable -= length;
      }

      fram.writeEnable(true);

      if (SIZE - writeIdx < length) {
        fram.write(writeIdx, values, SIZE - writeIdx);
        fram.write(0x0, values, writeIdx + length - SIZE);
        writeIdx = writeIdx + length - SIZE;
      } else {
        fram.write(writeIdx, values, length);
        writeIdx = writeIdx + length;
      }

      fram.writeEnable(false);
    }

    void endWritingAudio() {
      AudioRec *rec = new AudioRec{writeIdx, 0, nullptr};
      audioRecsTail->next = rec;
      audioRecsTail = rec;
    }

    size_t readAudio(uint8_t *values, size_t length, int *ID) {
      if (!hasAudioToRead()) {
        return 0;
      }

      *ID = audioRecsHead->time;

      int remainingLength = audioRecsHead->next->idx - audioRecsHead->idx;
      if (remainingLength < 0) {
        remainingLength += SIZE;
      }
      Serial.print("remainingLength = ");
      Serial.println(remainingLength);
      size_t readableLength = min(length, remainingLength);

      if (SIZE - audioRecsHead->idx < readableLength) {
        fram.read(audioRecsHead->idx, values, SIZE - audioRecsHead->idx);
        fram.read(0x0, &values[SIZE - audioRecsHead->idx], audioRecsHead->idx + readableLength - SIZE);
      } else {
        fram.read(audioRecsHead->idx, values, readableLength);
      }

      audioRecsHead->idx += readableLength;
      spaceAvailable += readableLength;
      if (audioRecsHead->idx == audioRecsHead->next->idx) {
        AudioRec *temp = audioRecsHead->next;
        delete audioRecsHead;
        audioRecsHead = temp;
      }

      return readableLength;
    }
};

#endif  // End of include guard
