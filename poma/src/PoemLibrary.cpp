#include "PoemLibrary.h"

#include <SD.h>

namespace {
const uint32_t SD_SPI_HZ = 4000000;  // core default; a poem reads in ~18 ms
}  // namespace

PoemLibrary::PoemLibrary(uint8_t csPin, SPIClass &spi)
  : csPin_(csPin), spi_(spi) {}

// The work is done in countMounted() and loadMounted() rather than inline so
// that their File objects are closed before SD.end() runs.

const char *PoemLibrary::count(int &count) {
  count = 0;
  const char *err = mount();
  if (!err) err = countMounted(count);
  SD.end();
  return err;
}

const char *PoemLibrary::load(int index, PoemBitmap &bitmap) {
  const char *err = mount();
  if (!err) err = loadMounted(index, bitmap);
  SD.end();
  return err;
}

const char *PoemLibrary::mount() {
  // format_if_empty defaults to false: a card that fails to mount is left
  // exactly as it was. We only ever open files for reading.
  if (!SD.begin(csPin_, spi_, SD_SPI_HZ)) return "No SD card";
  return nullptr;
}

const char *PoemLibrary::countMounted(int &count) {
  File dir = SD.open("/poems");
  if (!dir || !dir.isDirectory()) return "No /poems folder";

  int highest = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory() && isPoemName(f.name())) {
      count++;
      highest = max(highest, atoi(f.name()));
    }
  }
  if (count == 0) return "No poems on card";
  if (highest != count) return "Poem numbers have gaps";
  return nullptr;
}

const char *PoemLibrary::loadMounted(int index, PoemBitmap &bitmap) {
  char path[24];
  snprintf(path, sizeof(path), "/poems/%03d.bin", index + 1);

  File f = SD.open(path, FILE_READ);
  if (!f) return "Poem file missing";
  if (f.size() != PoemBitmap::BYTES) return "Poem file wrong size";
  if (f.read(bitmap.data, PoemBitmap::BYTES) != PoemBitmap::BYTES) return "Poem read failed";
  return nullptr;
}

// True for exactly "NNN.bin". Rejects the clutter macOS leaves on FAT
// volumes (.DS_Store, ._001.bin, .Spotlight-V100) so it is never counted.
bool PoemLibrary::isPoemName(const char *name) {
  return strlen(name) == 7 && isdigit(name[0]) && isdigit(name[1]) && isdigit(name[2]) && strcasecmp(name + 3, ".bin") == 0;
}
