/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor/ams_as5600.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include "scroller_config.h"
#include "scroller_sensor.h"
#include "scroller_ble.h"
#include "scroller_hog.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* Initialize config and config mutex */
struct scroller_config_t SCROLLER_CONFIG = {
    .scroll_accumulator = 0,
    .internal_divider = SCROLLER_STEPS_LOW_RES,
};
K_MUTEX_DEFINE(scroller_config_mutex);

int main(void)
{
        int err;

        printk("Scroller v0.1 Test Application\n");

        err = bt_enable(bt_ready);
        if (err)
        {
                LOG_ERR("Bluetooth init failed: %d", err);
                return 0;
        }

        return 0;
}
