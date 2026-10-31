#pragma once

#include <WeatherSensor.h>

#include <cmath>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/component.h"

namespace esphome::bresser_weather {

class BresserWeatherComponent : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_frequency_offset(float offset) { frequency_offset_ = offset; }
  void set_max_sensors(uint8_t value) { max_sensors_ = value; }
  void set_log_sensor_id(bool value) { log_sensor_id_ = value; }
  void set_filter_sensor_id(uint32_t id) {
    filter_sensor_id_ = id;
    filter_enabled_ = true;
  }
  void set_preferred_sensor_id(uint32_t id) {
    preferred_sensor_id_ = id;
    preferred_enabled_ = true;
  }
  void set_station_timeout(uint32_t value) { station_timeout_ms_ = value; }

  void set_temperature_sensor(sensor::Sensor *value) { temperature_sensor_ = value; }
  void set_humidity_sensor(sensor::Sensor *value) { humidity_sensor_ = value; }
  void set_wind_gust_sensor(sensor::Sensor *value) { wind_gust_sensor_ = value; }
  void set_wind_speed_sensor(sensor::Sensor *value) { wind_speed_sensor_ = value; }
  void set_wind_direction_sensor(sensor::Sensor *value) { wind_direction_sensor_ = value; }
  void set_rain_sensor(sensor::Sensor *value) { rain_sensor_ = value; }
  void set_uv_sensor(sensor::Sensor *value) { uv_sensor_ = value; }
  void set_illuminance_sensor(sensor::Sensor *value) { illuminance_sensor_ = value; }
  void set_rssi_sensor(sensor::Sensor *value) { rssi_sensor_ = value; }
  void set_battery_low_sensor(binary_sensor::BinarySensor *value) { battery_low_sensor_ = value; }
  void set_sensor_id_text_sensor(text_sensor::TextSensor *value) { sensor_id_sensor_ = value; }
  void set_stations_text_sensor(text_sensor::TextSensor *value) { stations_sensor_ = value; }

  void set_neighbor_temperature_sensor(sensor::Sensor *value) { neighbor_temperature_sensor_ = value; }
  void set_neighbor_humidity_sensor(sensor::Sensor *value) { neighbor_humidity_sensor_ = value; }
  void set_neighbor_wind_gust_sensor(sensor::Sensor *value) { neighbor_wind_gust_sensor_ = value; }
  void set_neighbor_wind_speed_sensor(sensor::Sensor *value) { neighbor_wind_speed_sensor_ = value; }
  void set_neighbor_wind_direction_sensor(sensor::Sensor *value) { neighbor_wind_direction_sensor_ = value; }
  void set_neighbor_rain_sensor(sensor::Sensor *value) { neighbor_rain_sensor_ = value; }
  void set_neighbor_uv_sensor(sensor::Sensor *value) { neighbor_uv_sensor_ = value; }
  void set_neighbor_illuminance_sensor(sensor::Sensor *value) { neighbor_illuminance_sensor_ = value; }
  void set_neighbor_count_sensor(sensor::Sensor *value) { neighbor_count_sensor_ = value; }

 protected:
  struct Station {
    uint32_t id{0};
    uint32_t last_seen_ms{0};
    float rssi{NAN};
    float temperature{NAN};
    float humidity{NAN};
    float wind_gust{NAN};
    float wind_speed{NAN};
    float wind_direction{NAN};
    float rain{NAN};
    float uv{NAN};
    float illuminance{NAN};
  };

  void process_station_(const WeatherSensor::sensor_t &data);
  void publish_weather_(const WeatherSensor::sensor_t &data);
  Station &find_or_add_station_(uint32_t id);
  void expire_stations_();
  bool is_neighbor_(const Station &station) const;
  size_t count_neighbors_with_rain_() const;
  float neighbor_mean_(float Station::*field) const;
  float neighbor_wind_direction_() const;
  void publish_neighbors_();
  void publish_stations_text_();

  WeatherSensor receiver_;
  float frequency_offset_{0.0f};
  uint8_t max_sensors_{4};
  uint32_t filter_sensor_id_{0};
  bool filter_enabled_{false};
  bool log_sensor_id_{true};
  uint32_t preferred_sensor_id_{0};
  bool preferred_enabled_{false};
  uint32_t station_timeout_ms_{900000};
  std::vector<Station> stations_;
  float neighbor_rain_total_{0.0f};

  sensor::Sensor *temperature_sensor_{nullptr};
  sensor::Sensor *humidity_sensor_{nullptr};
  sensor::Sensor *wind_gust_sensor_{nullptr};
  sensor::Sensor *wind_speed_sensor_{nullptr};
  sensor::Sensor *wind_direction_sensor_{nullptr};
  sensor::Sensor *rain_sensor_{nullptr};
  sensor::Sensor *uv_sensor_{nullptr};
  sensor::Sensor *illuminance_sensor_{nullptr};
  sensor::Sensor *rssi_sensor_{nullptr};
  binary_sensor::BinarySensor *battery_low_sensor_{nullptr};
  text_sensor::TextSensor *sensor_id_sensor_{nullptr};
  text_sensor::TextSensor *stations_sensor_{nullptr};

  sensor::Sensor *neighbor_temperature_sensor_{nullptr};
  sensor::Sensor *neighbor_humidity_sensor_{nullptr};
  sensor::Sensor *neighbor_wind_gust_sensor_{nullptr};
  sensor::Sensor *neighbor_wind_speed_sensor_{nullptr};
  sensor::Sensor *neighbor_wind_direction_sensor_{nullptr};
  sensor::Sensor *neighbor_rain_sensor_{nullptr};
  sensor::Sensor *neighbor_uv_sensor_{nullptr};
  sensor::Sensor *neighbor_illuminance_sensor_{nullptr};
  sensor::Sensor *neighbor_count_sensor_{nullptr};
};

}  // namespace esphome::bresser_weather
