#include "bresser_weather.h"

#include <algorithm>
#include <cmath>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::bresser_weather {

static const char *const TAG = "bresser_weather";
static const uint32_t STATION_REFRESH_MS = 60000;
// Home Assistant rejects text states longer than 255 characters.
static const size_t MAX_STATIONS_TEXT = 240;

void BresserWeatherComponent::setup() {
  ESP_LOGI(TAG, "Initialisiere Bresser-Empfaenger bei %.3f MHz",
           868.3f + this->frequency_offset_);
  const int16_t state = this->receiver_.begin(this->max_sensors_, true, this->frequency_offset_);
  if (state != RADIOLIB_ERR_NONE) {
    ESP_LOGE(TAG, "Funkchip konnte nicht initialisiert werden (RadioLib-Fehler %d)", state);
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Bresser-Empfaenger ist bereit");
  this->set_interval("stations", STATION_REFRESH_MS, [this]() {
    this->expire_stations_();
    this->publish_neighbors_();
    this->publish_stations_text_();
  });
}

void BresserWeatherComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Bresser 868-MHz Wetterempfaenger:");
  ESP_LOGCONFIG(TAG, "  Empfangsfrequenz: %.3f MHz", 868.3f + this->frequency_offset_);
  ESP_LOGCONFIG(TAG, "  Maximale Sensoren: %u", this->max_sensors_);
  if (this->filter_enabled_) {
    ESP_LOGCONFIG(TAG, "  Sensorfilter: %08" PRIX32, this->filter_sensor_id_);
  } else {
    ESP_LOGCONFIG(TAG, "  Sensorfilter: aus");
  }
  if (this->preferred_enabled_) {
    ESP_LOGCONFIG(TAG, "  Eigene Station: %08" PRIX32, this->preferred_sensor_id_);
  }
  ESP_LOGCONFIG(TAG, "  Sensor-ID im Log: %s", YESNO(this->log_sensor_id_));
  LOG_SENSOR("  ", "Temperatur", this->temperature_sensor_);
  LOG_SENSOR("  ", "Luftfeuchtigkeit", this->humidity_sensor_);
  LOG_SENSOR("  ", "Windboe", this->wind_gust_sensor_);
  LOG_SENSOR("  ", "Windgeschwindigkeit", this->wind_speed_sensor_);
  LOG_SENSOR("  ", "Windrichtung", this->wind_direction_sensor_);
  LOG_SENSOR("  ", "Regen", this->rain_sensor_);
  LOG_SENSOR("  ", "UV-Index", this->uv_sensor_);
  LOG_SENSOR("  ", "Beleuchtungsstaerke", this->illuminance_sensor_);
  LOG_SENSOR("  ", "RSSI", this->rssi_sensor_);
  LOG_BINARY_SENSOR("  ", "Batterie schwach", this->battery_low_sensor_);
  LOG_TEXT_SENSOR("  ", "Sensor-ID", this->sensor_id_sensor_);
}

void BresserWeatherComponent::loop() {
  if (this->is_failed()) {
    return;
  }

  const DecodeStatus status = this->receiver_.getMessage();
  if (status != DECODE_OK) {
    return;
  }

  for (const auto &data : this->receiver_.sensor) {
    if (!data.valid) {
      continue;
    }
    if (this->log_sensor_id_) {
      ESP_LOGI(TAG, "Empfangene Sensor-ID: %08" PRIX32, data.sensor_id);
    }
    if (this->filter_enabled_ && data.sensor_id != this->filter_sensor_id_) {
      ESP_LOGD(TAG, "Ignoriere Sensor %08" PRIX32, data.sensor_id);
      continue;
    }
    if (data.s_type != SENSOR_TYPE_WEATHER0 && data.s_type != SENSOR_TYPE_WEATHER1 &&
        data.s_type != SENSOR_TYPE_WEATHER3 && data.s_type != SENSOR_TYPE_WEATHER8) {
      ESP_LOGD(TAG, "Ignoriere Nicht-Wettersensor %08" PRIX32 " (Typ %u)", data.sensor_id,
               static_cast<unsigned>(data.s_type));
      continue;
    }

    this->process_station_(data);
  }

  // Each packet must end in a free slot. A retained slot would be republished
  // for every later packet, and a full table would reject all new stations.
  this->receiver_.clearSlots();
  for (auto &data : this->receiver_.sensor) {
    data.w.temp_ok = false;
    data.w.humidity_ok = false;
    data.w.light_ok = false;
    data.w.uv_ok = false;
    data.w.wind_ok = false;
    data.w.rain_ok = false;
  }
}

void BresserWeatherComponent::process_station_(const WeatherSensor::sensor_t &data) {
  const bool is_preferred = this->preferred_enabled_ && data.sensor_id == this->preferred_sensor_id_;
  const size_t known_stations = this->stations_.size();
  Station &station = this->find_or_add_station_(data.sensor_id);
  station.last_seen_ms = millis();
  station.rssi = data.rssi;

  const float previous_rain = station.rain;
  if (data.w.temp_ok) {
    station.temperature = data.w.temp_c;
  }
  if (data.w.humidity_ok) {
    station.humidity = data.w.humidity;
  }
  if (data.w.wind_ok) {
    station.wind_gust = data.w.wind_gust_meter_sec;
    station.wind_speed = data.w.wind_avg_meter_sec;
    station.wind_direction = data.w.wind_direction_deg;
  }
  if (data.w.rain_ok) {
    station.rain = data.w.rain_mm;
  }
  if (data.w.uv_ok) {
    station.uv = data.w.uv;
  }
  if (data.w.light_ok) {
    station.illuminance = data.w.light_klx * 1000.0f;
  }

  if (!this->preferred_enabled_ || is_preferred) {
    this->publish_weather_(data);
  }
  if (!is_preferred) {
    // The rain counters of different stations have unrelated start values, so
    // the mean total advances by the mean increment. A falling counter is a
    // station reset and adds nothing.
    if (data.w.rain_ok && station.rain > previous_rain) {
      this->neighbor_rain_total_ += (station.rain - previous_rain) / this->count_neighbors_with_rain_();
    }
    this->publish_neighbors_();
  }
  if (this->stations_.size() != known_stations) {
    this->publish_stations_text_();
  }
}

BresserWeatherComponent::Station &BresserWeatherComponent::find_or_add_station_(uint32_t id) {
  for (auto &station : this->stations_) {
    if (station.id == id) {
      return station;
    }
  }
  if (this->stations_.size() >= this->max_sensors_) {
    // The table is full: the station with the oldest packet gives way.
    this->stations_.erase(std::min_element(
        this->stations_.begin(), this->stations_.end(), [](const Station &a, const Station &b) {
          return millis() - a.last_seen_ms > millis() - b.last_seen_ms;
        }));
  }
  Station station;
  station.id = id;
  this->stations_.push_back(station);
  return this->stations_.back();
}

void BresserWeatherComponent::expire_stations_() {
  const uint32_t now = millis();
  this->stations_.erase(std::remove_if(this->stations_.begin(), this->stations_.end(),
                                       [this, now](const Station &station) {
                                         return now - station.last_seen_ms > this->station_timeout_ms_;
                                       }),
                        this->stations_.end());
}

bool BresserWeatherComponent::is_neighbor_(const Station &station) const {
  return !this->preferred_enabled_ || station.id != this->preferred_sensor_id_;
}

size_t BresserWeatherComponent::count_neighbors_with_rain_() const {
  return std::count_if(this->stations_.begin(), this->stations_.end(), [this](const Station &station) {
    return this->is_neighbor_(station) && !std::isnan(station.rain);
  });
}

float BresserWeatherComponent::neighbor_mean_(float Station::*field) const {
  float sum = 0.0f;
  size_t count = 0;
  for (const auto &station : this->stations_) {
    if (this->is_neighbor_(station) && !std::isnan(station.*field)) {
      sum += station.*field;
      count++;
    }
  }
  return count == 0 ? NAN : sum / count;
}

float BresserWeatherComponent::neighbor_wind_direction_() const {
  // Degrees wrap at 360: the arithmetic mean of 350 and 10 would point south.
  float x = 0.0f;
  float y = 0.0f;
  bool has_value = false;
  for (const auto &station : this->stations_) {
    if (this->is_neighbor_(station) && !std::isnan(station.wind_direction)) {
      const float radians = station.wind_direction * static_cast<float>(M_PI) / 180.0f;
      x += std::cos(radians);
      y += std::sin(radians);
      has_value = true;
    }
  }
  if (!has_value) {
    return NAN;
  }
  const float degrees = std::atan2(y, x) * 180.0f / static_cast<float>(M_PI);
  return degrees < 0.0f ? degrees + 360.0f : degrees;
}

void BresserWeatherComponent::publish_neighbors_() {
  const auto publish = [](sensor::Sensor *target, float value) {
    if (target != nullptr) {
      target->publish_state(value);
    }
  };
  publish(this->neighbor_temperature_sensor_, this->neighbor_mean_(&Station::temperature));
  publish(this->neighbor_humidity_sensor_, this->neighbor_mean_(&Station::humidity));
  publish(this->neighbor_wind_gust_sensor_, this->neighbor_mean_(&Station::wind_gust));
  publish(this->neighbor_wind_speed_sensor_, this->neighbor_mean_(&Station::wind_speed));
  publish(this->neighbor_wind_direction_sensor_, this->neighbor_wind_direction_());
  publish(this->neighbor_rain_sensor_, this->neighbor_rain_total_);
  publish(this->neighbor_uv_sensor_, this->neighbor_mean_(&Station::uv));
  publish(this->neighbor_illuminance_sensor_, this->neighbor_mean_(&Station::illuminance));
  publish(this->neighbor_count_sensor_,
          std::count_if(this->stations_.begin(), this->stations_.end(),
                        [this](const Station &station) { return this->is_neighbor_(station); }));
}

void BresserWeatherComponent::publish_stations_text_() {
  if (this->stations_sensor_ == nullptr) {
    return;
  }
  std::string text;
  for (const auto &station : this->stations_) {
    const std::string entry = str_sprintf("%08" PRIX32 " %.0f dBm", station.id, station.rssi);
    if (text.size() + entry.size() + 2 > MAX_STATIONS_TEXT) {
      break;
    }
    text += text.empty() ? entry : ", " + entry;
  }
  this->stations_sensor_->publish_state(text);
}

void BresserWeatherComponent::publish_weather_(const WeatherSensor::sensor_t &data) {
  char id[9];
  snprintf(id, sizeof(id), "%08" PRIX32, data.sensor_id);

  if (this->sensor_id_sensor_ != nullptr) {
    this->sensor_id_sensor_->publish_state(id);
  }
  if (this->rssi_sensor_ != nullptr) {
    this->rssi_sensor_->publish_state(data.rssi);
  }
  if (this->battery_low_sensor_ != nullptr) {
    this->battery_low_sensor_->publish_state(!data.battery_ok);
  }
  if (data.w.temp_ok && this->temperature_sensor_ != nullptr) {
    this->temperature_sensor_->publish_state(data.w.temp_c);
  }
  if (data.w.humidity_ok && this->humidity_sensor_ != nullptr) {
    this->humidity_sensor_->publish_state(data.w.humidity);
  }
  if (data.w.wind_ok) {
    if (this->wind_gust_sensor_ != nullptr) {
      this->wind_gust_sensor_->publish_state(data.w.wind_gust_meter_sec);
    }
    if (this->wind_speed_sensor_ != nullptr) {
      this->wind_speed_sensor_->publish_state(data.w.wind_avg_meter_sec);
    }
    if (this->wind_direction_sensor_ != nullptr) {
      this->wind_direction_sensor_->publish_state(data.w.wind_direction_deg);
    }
  }
  if (data.w.rain_ok && this->rain_sensor_ != nullptr) {
    this->rain_sensor_->publish_state(data.w.rain_mm);
  }
  if (data.w.uv_ok && this->uv_sensor_ != nullptr) {
    this->uv_sensor_->publish_state(data.w.uv);
  }
  if (data.w.light_ok && this->illuminance_sensor_ != nullptr) {
    this->illuminance_sensor_->publish_state(data.w.light_klx * 1000.0f);
  }

  ESP_LOGD(TAG,
           "Werte: %.1f C, %u %%, Wind %.1f/%.1f m/s @ %.0f Grad, Regen %.1f mm, "
           "UV %.1f, Licht %.1f klx, RSSI %.1f dBm, Batterie %s",
           data.w.temp_c, data.w.humidity, data.w.wind_avg_meter_sec, data.w.wind_gust_meter_sec,
           data.w.wind_direction_deg, data.w.rain_mm, data.w.uv, data.w.light_klx, data.rssi,
           data.battery_ok ? "OK" : "schwach");
}

}  // namespace esphome::bresser_weather
