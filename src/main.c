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

/* State change mesgq */
K_MSGQ_DEFINE(state_change, sizeof(int32_t), 16, 1);

int main(void)
{
        int err;

        printk("Scroller v0.1 Test Application\n");

        err = bt_enable(scroller_ble_init);
        if (err)
        {
                LOG_ERR("Bluetooth init failed: %d", err);
                return 0;
        }

        /* Control loop:
         * Depending on USB and BLE state selectively suspend threads
         */

        int32_t NEW_state;
        bool SENSOR_enabled = true;

        while (1)
        {
                k_msgq_get(&state_change, &NEW_state, K_FOREVER);
                LOG_INF("State change recieved: %d", NEW_state);

                if (NEW_state == BLE_CONNECTED)
                {
                        /* BLE is connected, start sensor and ble_send threads */
                        k_thread_resume(ble_send_thread);

                        if (!SENSOR_enabled)
                        {
                                // FIXME: return sensor to high power mode
                                k_thread_resume(sensor_thread);
                                SENSOR_enabled = true;
                        }
                }
                else if (NEW_state == BLE_ADVERTISING)
                {
                        /* BLE is disconnected, suspend sensor and ble_send threads */
                        k_thread_suspend(ble_send_thread);
                        // FIXME: Put the sensor in a low power mode
                        k_thread_suspend(sensor_thread);
                        SENSOR_enabled = false;
                }
                else if (NEW_state == BLE_DISCONNECTED)
                {
                        /* BLE is disconnected, suspend sensor and ble_send threads */
                        k_thread_suspend(ble_send_thread);
                        // FIXME: Put the sensor in a low power mode
                        k_thread_suspend(sensor_thread);
                        SENSOR_enabled = false;

                        scroller_ble_advertise_pairing();
                }
                else
                {
                        LOG_ERR("Unhandled state transition");
                }
        }
}
