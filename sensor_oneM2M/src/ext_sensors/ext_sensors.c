/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
 */

#include <zephyr.h>
#include <stdio.h>
#include <string.h>
#include <drivers/sensor.h>
#include "ext_sensors.h"
#include <stdlib.h>


struct env_sensor {
	enum sensor_channel channel;
	const struct device *dev;
	struct k_spinlock lock;
};

static struct env_sensor temp_sensor = {
	.channel = SENSOR_CHAN_AMBIENT_TEMP,
	.dev = DEVICE_DT_GET(DT_ALIAS(temp_sensor)),

};

static struct env_sensor humid_sensor = {
	.channel = SENSOR_CHAN_HUMIDITY,
	.dev = DEVICE_DT_GET(DT_ALIAS(humidity_sensor)),
};

static ext_sensor_handler_t evt_handler;

int ext_sensors_init(ext_sensor_handler_t handler)
{
	struct ext_sensor_evt evt = {0};

	if (handler == NULL) {
		printk("External sensor handler NULL!");
		return -EINVAL;
	}

	evt_handler = handler;

	if (!device_is_ready(temp_sensor.dev)) {
		printk("Temperature sensor device is not ready");
		evt.type = EXT_SENSOR_EVT_TEMPERATURE_ERROR;
		evt_handler(&evt);
	}

	if (!device_is_ready(humid_sensor.dev)) {
		printk("Humidity sensor device is not ready");
		evt.type = EXT_SENSOR_EVT_HUMIDITY_ERROR;
		evt_handler(&evt);
	}

// 	if (!device_is_ready(accel_sensor.dev)) {
// 		LOG_ERR("Accelerometer device is not ready");
// 		evt.type = EXT_SENSOR_EVT_ACCELEROMETER_ERROR;
// 		evt_handler(&evt);
// 	} else {
// #if defined(CONFIG_EXTERNAL_SENSORS_ACTIVITY_DETECTION_AUTO)
// 		struct sensor_trigger trig = {
// 			.chan = SENSOR_CHAN_ACCEL_XYZ,
// 			.type = SENSOR_TRIG_THRESHOLD
// 		};

// 		int err = sensor_trigger_set(accel_sensor.dev,
// 					     &trig, accelerometer_trigger_handler);

// 		if (err) {
// 			LOG_ERR("Could not set trigger for device %s, error: %d",
// 				accel_sensor.dev->name, err);
// 			return err;
// 		}
// #endif
// 	}

	return 0;
}

int ext_sensors_temperature_get(double *ext_temp)
{
	int err;
	struct sensor_value data = {0};
	struct ext_sensor_evt evt = {0};

	err = sensor_sample_fetch_chan(temp_sensor.dev, SENSOR_CHAN_ALL);
	if (err) {
		printk("Failed to fetch data from %s, error: %d",
			temp_sensor.dev->name, err);
		evt.type = EXT_SENSOR_EVT_TEMPERATURE_ERROR;
		evt_handler(&evt);
		return -ENODATA;
	}

	err = sensor_channel_get(temp_sensor.dev, temp_sensor.channel, &data);
	if (err) {
		printk("Failed to fetch data from %s, error: %d",
			temp_sensor.dev->name, err);
		evt.type = EXT_SENSOR_EVT_TEMPERATURE_ERROR;
		evt_handler(&evt);
		return -ENODATA;
	}

	k_spinlock_key_t key = k_spin_lock(&(temp_sensor.lock));
	*ext_temp = sensor_value_to_double(&data);
	k_spin_unlock(&(temp_sensor.lock), key);

	return 0;
}

int ext_sensors_humidity_get(double *ext_hum)
{
	int err;
	struct sensor_value data = {0};
	struct ext_sensor_evt evt = {0};

	err = sensor_sample_fetch_chan(humid_sensor.dev, SENSOR_CHAN_ALL);
	if (err) {
		printk("Failed to fetch data from %s, error: %d",
			humid_sensor.dev->name, err);
		evt.type = EXT_SENSOR_EVT_HUMIDITY_ERROR;
		evt_handler(&evt);
		return -ENODATA;
	}

	err = sensor_channel_get(humid_sensor.dev, humid_sensor.channel, &data);
	if (err) {
		printk("Failed to fetch data from %s, error: %d",
			humid_sensor.dev->name, err);
		evt.type = EXT_SENSOR_EVT_HUMIDITY_ERROR;
		evt_handler(&evt);
		return -ENODATA;
	}

	k_spinlock_key_t key = k_spin_lock(&(humid_sensor.lock));
	*ext_hum = sensor_value_to_double(&data);
	k_spin_unlock(&(humid_sensor.lock), key);

	return 0;
}