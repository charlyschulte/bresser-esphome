#include "bresser_weather.h"

#include <cmath>

#include "esphome/core/log.h"

namespace esphome::bresser_weather {

static const char *const TAG = "bresser_weather";

// 16 Himmelsrichtungen im Uhrzeigersinn ab Norden, deutsche Abkuerzungen.
// Bei 8 Sektoren wird jede zweite Richtung verwendet.
static const char *const WIND_DIRECTIONS[16] = {
    "N", "NNO", "NO", "ONO", "O", "OSO", "SO", "SSO",
    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW",
};

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
  LOG_TEXT_SENSOR("  ", "Windrichtung (Text)", this->wind_direction_text_sensor_);
  if (this->wind_direction_text_sensor_ != nullptr) {
    ESP_LOGCONFIG(TAG, "    Sektoren: %u", this->wind_direction_sectors_);
  }
}

const char *BresserWeatherComponent::wind_direction_to_text_(float degrees) const {
  float normalized = std::fmod(degrees, 360.0f);
  if (normalized < 0.0f) {
    normalized += 360.0f;
  }
  const uint8_t sectors = this->wind_direction_sectors_ == 8 ? 8 : 16;
  const float sector_width = 360.0f / sectors;
  const uint8_t index = static_cast<uint8_t>(normalized / sector_width + 0.5f) % sectors;
  return WIND_DIRECTIONS[index * (16 / sectors)];
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

    this->publish_weather_(data);
    break;
  }
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
    if (this->wind_direction_text_sensor_ != nullptr && !std::isnan(data.w.wind_direction_deg)) {
      this->wind_direction_text_sensor_->publish_state(
          this->wind_direction_to_text_(data.w.wind_direction_deg));
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
