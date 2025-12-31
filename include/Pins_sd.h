#pragma once
#include "Debug.h"
#include <Arduino.h>

// ===============================
// microSD (SPI) wiring - ESP32-S3
// ===============================
//
// IMPORTANT:
// - Pick pins that are NOT USB (19/20)
// - Avoid strapping pins if you can
//
// These are generally safe on many S3 boards.
// If your wiring differs, change these to match your module.
//
static const int PIN_SD_SCK  = 12;
static const int PIN_SD_MISO = 13;
static const int PIN_SD_MOSI = 11;
static const int PIN_SD_CS   = 10;

// SPI clock (SD modules vary; 10?20 MHz usually fine)
static const uint32_t SD_SPI_HZ = 16000000;
