#include "esphome/defines.h"

#ifdef USE_MHZ19

#include "esphome/sensor/mhz19_component.h"
#include "esphome/log.h"

ESPHOME_NAMESPACE_BEGIN

namespace sensor {

static const char *TAG = "sensor.mhz19";
static const uint8_t MHZ19_REQUEST_LENGTH = 8;
static const uint8_t MHZ19_RESPONSE_LENGTH = 9;
static const uint8_t MHZ19_COMMAND_GET_PPM[] = {0xFF, 0x01, 0x86, 0x00, 0x00, 0x00, 0x00, 0x00};
static const uint8_t MHZ19_COMMAND_ABC_DISABLE[] = {0xFF, 0x01, 0x79, 0x00, 0x00, 0x00, 0x00, 0x00, 0x86};

MHZ19Component::MHZ19Component(UARTComponent *parent, const std::string &co2_name, uint32_t update_interval)
    : PollingComponent(update_interval), UARTDevice(parent), co2_sensor_(new MHZ19CO2Sensor(co2_name, this)) {}
uint8_t mhz19_checksum(const uint8_t *command) {
  uint8_t sum = 0;
  for (uint8_t i = 1; i < MHZ19_REQUEST_LENGTH; i++) {
    sum += command[i];
  }
  return 0xFF - sum + 0x01;
}

void MHZ19Component::update() {
  uint8_t response[MHZ19_RESPONSE_LENGTH];
  if (!this->mhz19_write_command_(MHZ19_COMMAND_GET_PPM, response)) {
    ESP_LOGW(TAG, "Reading data from MHZ19 failed!");
    this->status_set_warning();
    return;
  }

  if (response[0] != 0xFF || response[1] != 0x86) {
    ESP_LOGW(TAG, "Invalid preamble from MHZ19!");
    this->status_set_warning();
    return;
  }

  /* Sensor reports U(15000) during boot, ingnore reported CO2 until it boots */
  uint16_t u = (response[6] << 8) + response[7];
  if (u == 15000) {
    ESP_LOGD(TAG, "Sensor is booting");
    return;
  }

  /* MH-Z19B(s == 0) and MH-Z19(s != 0) */
  uint8_t s = response[5];
  if (response[5] == 0 && this->model_b == false) {
    ESP_LOGD(TAG, "MH-Z19B detected");
    this->model_b = true;
  }

  if (this->model_b && this->abc_disabled == false) {
    uint8_t abc_ack[MHZ19_RESPONSE_LENGTH];
    /*
     * MH-Z19B allows to enable/disable 'automatic baseline calibration' (datasheet MH-Z19B v1.2),
     * disable it to prevent sensor baseline drift in not well ventilated area
     */
    ESP_LOGI(TAG, "Disabling ABC on boot");
    if (!this->mhz19_write_command_(MHZ19_COMMAND_ABC_DISABLE, abc_ack)) {
      ESP_LOGW(TAG, "Failed to read ABC disable ack!");
      return;
    }
    this->abc_disabled = true;
    /*
     * TODO: implement an option to recalibrate sensor, to allow for occasional
     * manual recalibration
     */
  }

  uint8_t checksum = mhz19_checksum(response);
  if (response[8] != checksum) {
    ESP_LOGW(TAG, "MHZ19 Checksum doesn't match: 0x%02X!=0x%02X", response[8], checksum);
    this->status_set_warning();
    return;
  }

  this->status_clear_warning();
  const uint16_t ppm = (uint16_t(response[2]) << 8) | response[3];
  const int temp = int(response[4]) - 40;
  const uint8_t status = response[5];

  ESP_LOGD(TAG, "MHZ19 Received CO₂=%uppm Temperature=%d°C Status=0x%02X", ppm, temp, status);
  this->co2_sensor_->publish_state(ppm);
  if (this->temperature_sensor_ != nullptr)
    this->temperature_sensor_->publish_state(temp);
}

bool MHZ19Component::mhz19_write_command_(const uint8_t *command, uint8_t *response) {
  this->flush();
  this->write_array(command, MHZ19_REQUEST_LENGTH);
  this->write_byte(mhz19_checksum(command));

  if (response == nullptr)
    return true;

  bool ret = this->read_array(response, MHZ19_RESPONSE_LENGTH);
  this->flush();
  return ret;
}
MHZ19TemperatureSensor *MHZ19Component::make_temperature_sensor(const std::string &name) {
  return this->temperature_sensor_ = new MHZ19TemperatureSensor(name, this);
}
MHZ19CO2Sensor *MHZ19Component::get_co2_sensor() const { return this->co2_sensor_; }
float MHZ19Component::get_setup_priority() const { return setup_priority::HARDWARE_LATE; }
void MHZ19Component::dump_config() {
  ESP_LOGCONFIG(TAG, "MH-Z19%s%s", this->model_b ? "B:" : "", this->abc_disabled ? " (auto calibration: disabled)": " (auto calibration: enabled)");
  LOG_SENSOR("  ", "CO2", this->co2_sensor_);
  LOG_SENSOR("  ", "Temperature", this->temperature_sensor_);
}

}  // namespace sensor

ESPHOME_NAMESPACE_END

#endif  // USE_MHZ19
