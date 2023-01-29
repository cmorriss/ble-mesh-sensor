#include <driver/adc.h>
#include "esp_adc_cal.h"
#include "esp_log.h"
#include "mesh_sensor.h"

#define BATTERY_ADC_CHANNEL ADC1_CHANNEL_0
#define BATTERY_ADC_ATTENUATION ADC_ATTEN_DB_11
#define BATTERY_ADC_UNIT ADC_UNIT_1
#define DEFAULT_BATTERY_HIGH_VOLTAGE 2100
#define DEFAULT_BATTERY_LOW_VOLTAGE 1450

static esp_adc_cal_characteristics_t* adc_chars;
static uint32_t battery_high_voltage = DEFAULT_BATTERY_HIGH_VOLTAGE;
static uint32_t battery_low_voltage = DEFAULT_BATTERY_LOW_VOLTAGE;

void set_battery_high_voltage(uint32_t high_voltage) {
    memcpy(&battery_high_voltage, &high_voltage, sizeof(uint32_t));
}

void set_battery_low_voltage(uint32_t low_voltage) {
    memcpy(&battery_low_voltage, &low_voltage, sizeof(uint32_t));
}

void init_adc() {
    LOGI("Initializing ADC for reading remaining battery level.");
    // Characterize ADC at particular attenuation
    adc_chars = calloc(1, sizeof(esp_adc_cal_characteristics_t));
    esp_adc_cal_characterize(BATTERY_ADC_UNIT, BATTERY_ADC_ATTENUATION, ADC_WIDTH_BIT_12, 0, adc_chars);

    // Configure ADC channel
    adc1_config_width(ADC_WIDTH_BIT_12);
    adc1_config_channel_atten(BATTERY_ADC_CHANNEL, ADC_ATTEN_DB_11);
}

void shutdown_adc() {
    free(adc_chars);
}

uint32_t read_battery_voltage() {
    init_adc();

    uint32_t reading = adc1_get_raw(BATTERY_ADC_CHANNEL);

    uint32_t voltage = esp_adc_cal_raw_to_voltage(reading, adc_chars);

    shutdown_adc();

    LOGI("Battery voltage raw Reading: %d\n", reading);
    LOGI("Calculated battery voltage: %d\n", voltage);
    return voltage;
}

uint32_t read_battery_remaining_percent() {
    uint32_t voltage = read_battery_voltage();

    LOGI("Calculating battery pct with lv: %d, hv: %d", battery_low_voltage, battery_high_voltage);
    if (voltage < battery_low_voltage) return 0;
    if (voltage > battery_high_voltage) return 100;
    return ((voltage - battery_low_voltage) * 100) / (battery_high_voltage - battery_low_voltage);
}