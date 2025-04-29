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

#include <app_event_manager.h>
#define MODULE main
#include <caf/events/module_state_event.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

int main(void)
{
        printk("Scroller v0.1 Test Application\n");

        if (app_event_manager_init())
        {
                LOG_ERR("Application event manager failed to init");
                return -EINVAL;
        }
        else
        {
                module_set_state(MODULE_STATE_READY);
        }

        return 0;
}
