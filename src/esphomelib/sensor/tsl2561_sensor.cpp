//
//  tsl2561_sensor.cpp
//  esphomelib
//
//  Created by Otto Winter on 09.05.18.
//  Copyright © 2018 Otto Winter. All rights reserved.
//
// Based on:
//   - https://cdn-shop.adafruit.com/datasheets/TSL2561.pdf
//   - https://github.com/adafruit/TSL2561-Arduino-Library

#include "esphomelib/sensor/tsl2561_sensor.h"
#include "esphomelib/log.h"

#ifdef USE_TSL2561

namespace esphomelib {

namespace sensor {

static const char *TAG = "sensor.tsl2561";

static const uint8_t TSL2561_REGISTER_CONTROL = 0x00;
static const uint8_t TSL2561_REGISTER_TIMING = 0x01;
static const uint8_t TSL2561_REGISTER_DATA_0 = 0x0C;
static const uint8_t TSL2561_REGISTER_DATA_1 = 0x0E;

TSL2561Sensor::TSL2561Sensor(I2CComponent *parent, const std::string &name,
                             uint8_t address, uint32_t update_interval)
    : PollingSensorComponent(name, update_interval), I2CDevice(parent, address) {}

void TSL2561Sensor::setup() {
  ESP_LOGCONFIG(TAG, "Setting up TSL2561...");
  uint8_t timing;
  if (!this->read_byte(TSL2561_REGISTER_TIMING, &timing)) {
    ESP_LOGE(TAG, "Communication with TSL2561 on address 0x%02X failed!", this->address_);
    this->mark_failed();
    return;
  }

  timing &= ~0b00010000;
  if (this->gain_ == TSL2561_GAIN_1X) {
    ESP_LOGCONFIG(TAG, "    Gain: 1x");
  } else if (this->gain_ == TSL2561_GAIN_16X) {
    ESP_LOGCONFIG(TAG, "    Gain: 16x");
  }
  timing |= this->gain_ == TSL2561_GAIN_16X ? 0b0001000 : 0;

  timing &= ~0b00000011;
  timing |= this->integration_time_ & 0b11;
  if (this->integration_time_ == TSL2561_INTEGRATION_14MS) {
    ESP_LOGCONFIG(TAG, "    Integration time: 14ms");
  } else if (this->integration_time_ == TSL2561_INTEGRATION_101MS) {
    ESP_LOGCONFIG(TAG, "    Integration time: 101ms");
  } else if (this->integration_time_ == TSL2561_INTEGRATION_402MS) {
    ESP_LOGCONFIG(TAG, "    Integration time: 402ms");
  }

  this->write_byte(TSL2561_REGISTER_TIMING, timing);
}
void TSL2561Sensor::update() {
  // Power on
  this->write_byte(TSL2561_REGISTER_CONTROL, 0b00000011);

  // Make sure the data is there when we will read it.
  uint32_t timeout = this->get_integration_time_ms_() + 20.0f;

  this->set_timeout("illuminance", timeout, [this]() {
    this->read_data_();
  });
}

float TSL2561Sensor::calculate_lx_(uint16_t ch0, uint16_t ch1) {
  if ((ch0 == 0xFFFF) || (ch1 == 0xFFFF)) {
    ESP_LOGW(TAG, "TSL2561 sensor is saturated.");
    return NAN;
  }

  float d0 = ch0, d1 = ch1;
  float ratio = d1 / d0;

  float ms = this->get_integration_time_ms_();
  d0 *= (402.0f / ms);
  d1 *= (402.0f / ms);

  if (this->gain_ == TSL2561_GAIN_1X) {
    d0 *= 16;
    d1 *= 16;
  }

  if (this->package_cs_) {
    if (ratio < 0.52f) {
      return 0.0315f * d0 - 0.0593f * d0 * powf(ratio, 1.4f);
    } else if (ratio < 0.65f) {
      return 0.0229f * d0 - 0.0291f * d1;
    } else if (ratio < 0.80f) {
      return 0.0157f * d0 - 0.0153f * d1;
    } else if (ratio < 1.30f) {
      return 0.00338f * d0 - 0.00260f * d1;
    }

    return 0.0f;
  } else {
    if (ratio < 0.5f) {
      return 0.0304f * d0 - 0.062f * d0 * powf(ratio, 1.4f);
    } else if (ratio < 0.61f) {
      return 0.0224f * d0 - 0.031f * d1;
    } else if (ratio < 0.80f) {
      return 0.0128f * d0 - 0.0153f * d1;
    } else if (ratio < 1.30f) {
      return 0.00146f * d0 - 0.00112f * d1;
    }
    return 0.0f;
  }
}
void TSL2561Sensor::read_data_() {
  uint8_t data[4];
  if (!this->read_bytes(TSL2561_REGISTER_DATA_0, data, 2))
    return;
  if (!this->read_bytes(TSL2561_REGISTER_DATA_1, data + 2, 2))
    return;

  // Power off
  this->write_byte(TSL2561_REGISTER_CONTROL, 0b00000000);

  uint16_t channel0 = (data[0] & 0xFF) | ((data[1] & 0xFF) << 8);
  uint16_t channel1 = (data[2] & 0xFF) | ((data[3] & 0xFF) << 8);
  float lx = this->calculate_lx_(channel0, channel1);
  ESP_LOGD(TAG, "Got illuminance=%.1flx", lx);
  this->push_new_value(lx);
}
std::string TSL2561Sensor::unit_of_measurement() {
  return UNIT_LX;
}
std::string TSL2561Sensor::icon() {
  return ICON_BRIGHTNESS_5;
}
int8_t TSL2561Sensor::accuracy_decimals() {
  return 1;
}
float TSL2561Sensor::get_integration_time_ms_() {
  switch (this->integration_time_) {
    case TSL2561_INTEGRATION_14MS: return 13.7f;
    case TSL2561_INTEGRATION_101MS: return 100.0f;
    case TSL2561_INTEGRATION_402MS: return 402.0f;
  }
  return 0.0f;
}
void TSL2561Sensor::set_integration_time(TSL2561IntegrationTime integration_time) {
  this->integration_time_ = integration_time;
}
void TSL2561Sensor::set_gain(TSL2561Gain gain) {
  this->gain_ = gain;
}
void TSL2561Sensor::set_is_cs_package(bool package_cs) {
  this->package_cs_ = package_cs;
}
float TSL2561Sensor::get_setup_priority() const {
  return setup_priority::HARDWARE_LATE;
}

} // namespace sensor

} // namespace esphomelib

#endif //USE_TSL2561