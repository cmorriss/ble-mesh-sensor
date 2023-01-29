/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#ifndef H_BLEPRPH_
#define H_BLEPRPH_

#include "mesh_sensor_constants.h"
#include "mesh_misc.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Moisture Sensor */
uint32_t read_soil_moisture_voltage();
uint32_t convert_moisture_voltage_to_pct(uint32_t voltage);
void set_sensor_high_voltage(uint32_t high_voltage);
void set_sensor_low_voltage(uint32_t low_voltage);
void hibernate_sensor();

/** Battery remaining */
uint32_t read_battery_remaining_percent();
uint32_t read_battery_voltage();
void set_battery_high_voltage(uint32_t high_voltage);
void set_battery_low_voltage(uint32_t low_voltage);

#ifdef __cplusplus
}
#endif

#endif
