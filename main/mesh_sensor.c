#include <driver/adc.h>
#include <driver/rtc_io.h>
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "mesh_sensor.h"

#define SENSOR_ADC_CHANNEL ADC1_CHANNEL_7
#define SENSOR_ADC_ATTENUATION ADC_ATTEN_DB_11
#define SENSOR_ADC_UNIT ADC_UNIT_1
#define SENSOR_POWER_GPIO GPIO_NUM_25
#define DEFAULT_SENSOR_HIGH_VOLTAGE 2707
#define DEFAULT_SENSOR_LOW_VOLTAGE 1344

static esp_adc_cal_characteristics_t* adc_chars;
static bool sensor_powered = false;
static uint32_t sensor_high_voltage = DEFAULT_SENSOR_HIGH_VOLTAGE;
static uint32_t sensor_low_voltage = DEFAULT_SENSOR_LOW_VOLTAGE;

void set_sensor_high_voltage(uint32_t high_voltage) {
    memcpy(&sensor_high_voltage, &high_voltage, sizeof(uint32_t));
}

void set_sensor_low_voltage(uint32_t low_voltage) {
    memcpy(&sensor_low_voltage, &low_voltage, sizeof(uint32_t));
}

void hibernate_sensor() {
    rtc_gpio_isolate(GPIO_NUM_35);
}

void power_on_sensor() {
    if (!sensor_powered) {
        LOGI("Initializing sensor power.");
        gpio_pad_select_gpio(SENSOR_POWER_GPIO);
        gpio_set_direction(SENSOR_POWER_GPIO, GPIO_MODE_OUTPUT);

        LOGI("Powering up sensor");
        gpio_set_level(25, 1);
        sensor_powered = true;
    }
}

void power_off_sensor() {
    if (sensor_powered) {
        LOGI("Powering down sensor");
        gpio_set_level(25, 0);
        sensor_powered = false;
    }
}

void init_adc_channel() {
    LOGI("Initializing ADC for reading sensor data.");
    // Characterize ADC at particular attenuation
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(SENSOR_ADC_UNIT, SENSOR_ADC_ATTENUATION, ADC_WIDTH_BIT_12, 0, adc_chars);

    // Configure ADC channel
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(SENSOR_ADC_CHANNEL, SENSOR_ADC_ATTENUATION);
}

void init_moisture_sensor() {
    // init adc for data pin
    init_adc_channel();

    power_on_sensor();

    vTaskDelay(1000 / portTICK_RATE_MS);
}

void shutdown_sensor() {
    power_off_sensor();

    free(adc_chars);
}

uint32_t read_soil_moisture_voltage() {
    init_moisture_sensor();

    uint32_t reading = adc1_get_raw(ADC1_CHANNEL_7);

    uint32_t voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);

    LOGI("Soil Moisture raw Reading: %d\n", reading);
    LOGI("Soil Moisture voltage: %d\n", voltage);

    shutdown_sensor();
    return voltage;
}

uint32_t convert_moisture_voltage_to_pct(uint32_t voltage) {
    if (voltage < sensor_low_voltage) return 100;
    if (voltage > sensor_high_voltage) return 0;
    return 100 - (((voltage - sensor_low_voltage) * 100) / (sensor_high_voltage - sensor_low_voltage));
}



