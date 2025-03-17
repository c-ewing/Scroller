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

#include "scroller.h"

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

/* USB */
static const uint8_t hid_report_desc[] = HID_WHEEL_REPORT_DESC();
/* USB Device Status*/
static enum usb_dc_status_code usb_status;
/* Report Frequency (ms) */
#define REPORT_FREQUENCY 5

/* Mouse Report */
struct __packed wheel_report_t
{
        uint8_t report_id;
        int16_t wheel;
} wheel_report;

/* Create a message queue for mouse reports to be sent over USB */
K_MSGQ_DEFINE(mouse_msgq, sizeof(wheel_report), 2, 1);

/* Semaphore for writing reports over USB. When a write is started, take the semaphore,
 * when the in endpoint is ready again release the semaphore.
 */
static K_SEM_DEFINE(ep_write_sem, 0, 1);

/* Semaphore for enabling the USB after connection
 * Wait for the device state to be set to USB_DC_CONFIGURED before enabling the USB
 */
static K_SEM_DEFINE(usb_conf_sem, 0, 1);

/* Take the status reported by the callback and process it */
static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
        ARG_UNUSED(param);

        /* Try taking the semaphore on all states other than configured */
        switch (status)
        {
        case USB_DC_CONFIGURED:
                k_sem_give(&usb_conf_sem);
                break;
        default:
                k_sem_take(&usb_conf_sem, K_NO_WAIT);
                break;
        }

        usb_status = status;
}

/* Callback for when the IN endpoint transfer has completed. Releases the write semaphore */
static void int_in_ready_cb(const struct device *dev)
{
        ARG_UNUSED(dev);
        k_sem_give(&ep_write_sem);
}

/* Log set reports */
static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len, uint8_t **data)
{
        ARG_UNUSED(dev);
        ARG_UNUSED(setup);

        /* Get the feature report (0x0300) with report ID 2 (0x0002)*/
        if (setup->wValue == 0x0302) {
                // FIXME: Respond with the resolution multiplier
                LOG_INF("GET_REPORT: Resolution Multiplier");
        }

        return 0;
}

/* Callback for Set_Report requests */
static int set_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len, uint8_t **data)
{
        ARG_UNUSED(dev);
        ARG_UNUSED(setup);

        uint16_t resolution_mult;

        /* Check to see if the first byte is 0x02, the report id for the Resolution Multiplier report */
        if ((*data)[0] != 0x02)
        {
                return 0;
        }

        /* uint8 or uint16 resolution modifier depending on report setting */
        if (*len == 2)
        {
                /* Extract the resolution multiplier */
                resolution_mult = (*data)[1];

                /* Log the resolution multiplier */
                LOG_INF("Resolution Multiplier: %d", resolution_mult - 1);
        }
        else if (*len == 3)
        {
                /* Extract the resolution multiplier */
                resolution_mult = (*data)[1] | (*data)[2] << 8;

                /* Log the resolution multiplier */
                LOG_INF("Resolution Multiplier: %d", resolution_mult - 1);
        }

        return 0;
}

/* Registers the only used HID callback*/
static const struct hid_ops ops = {
    .get_report = get_report_cb,
    .set_report = set_report_cb,
    .int_in_ready = int_in_ready_cb,
};

int send_hid_report(const struct device *hid_dev, uint8_t *report, size_t report_size)
{
        int ret;

        /* Make sure the USB device is configured before trying to write */
        k_sem_take(&usb_conf_sem, K_FOREVER);

        /* Write the report to the HID interrupt endpoint */
        ret = hid_int_ep_write(hid_dev, report, report_size, NULL);
        if (ret)
        {
                LOG_ERR("HID write error, %d", ret);
                return ret;
        }
        else
        {
                // Wait for the write to complete
                k_sem_take(&ep_write_sem, K_FOREVER);
        }

        k_sem_give(&usb_conf_sem);

        return 0;
}

int main(void)
{
        const struct device *sensor;
        const struct device *hid_dev;
        int ret;

        printk("Scroller v0.1 Test Application\n");

        sensor = DEVICE_DT_GET(DT_NODELABEL(as5600));
        if (!device_is_ready(sensor))
        {
                LOG_ERR("Sensor not ready");
                return 0;
        }

        // Log the HID report descriptor
        LOG_HEXDUMP_INF(hid_report_desc, sizeof(hid_report_desc), "HID Report Descriptor");

        // Get the usb hid device binding, should only be one?
        hid_dev = device_get_binding("HID_0");

        if (hid_dev == NULL)
        {
                LOG_ERR("Cannot get USB HID Device");
                return 0;
        }

        // Register the device with the HID report descriptor
        usb_hid_register_device(hid_dev,
                                hid_report_desc, sizeof(hid_report_desc),
                                &ops);

        // Initialize the HID device
        usb_hid_init(hid_dev);

        // Enable USB and binds the status callback
        ret = usb_enable(status_cb);

        if (ret != 0)
        {
                LOG_ERR("Failed to enable USB");
                return 0;
        }

        // Read off the AS5600
        ret = sensor_sample_fetch_chan(sensor, AS5600_SENSOR_CHAN_SCALED_ANGLE);
        if (ret < 0)
        {
                LOG_ERR("Could not fetch samples (%d)", ret);
                return 0;
        }

        struct sensor_value prev_angle;
        struct sensor_value curr_angle;
        UDC_STATIC_BUF_DEFINE(report, sizeof(wheel_report));
        /*
         * FIXME: Apply an internal scroll accumulator. The linux kernel only supports down to
         * (int)(steps * 120 / RES MULT) resulting a maximum of 120 steps per detent. Fractional
         * scrolling is not supported. The sensor emits 4096/120 ~34 detents per revolution
         * which is high.
         *
         * This may also be possible to solve using a custom Mouse definition in the kernel or
         * libinput allowing for more steps per detent.
         */
        while (1)
        {
                ret = sensor_channel_get(sensor, AS5600_SENSOR_CHAN_SCALED_ANGLE, &prev_angle);
                if (ret < 0)
                {
                        LOG_ERR("Could not get samples (%d)", ret);
                        return 0;
                }

                k_msleep(REPORT_FREQUENCY);

                // Log the Delta:
                ret = sensor_sample_fetch_chan(sensor, AS5600_SENSOR_CHAN_SCALED_ANGLE);
                if (ret < 0)
                {
                        LOG_ERR("Could not fetch samples (%d)", ret);
                        return 0;
                }

                ret = sensor_channel_get(sensor, AS5600_SENSOR_CHAN_SCALED_ANGLE, &curr_angle);
                if (ret < 0)
                {
                        LOG_ERR("Could not get samples (%d)", ret);
                        return 0;
                }

                /* Process the step delta accounting for roll over at 0/4095 */
                int16_t delta = curr_angle.val1 - prev_angle.val1;
                if (delta > 2048)
                {
                        delta -= 4096; // Negative direction wrap
                }
                else if (delta < -2048)
                {
                        delta += 4096; // Positive direction wrap
                }

                /* USB */
                if (delta)
                {
                        // LOG_INF("Angle delta: %d", delta);
                        /* Prepare the report */
                        wheel_report.report_id = 0x01;
                        wheel_report.wheel = -1 * delta; /* Invert Scroller Direction */

                        /* Copy the report to the static buffer */
                        memcpy(report, &wheel_report, sizeof(wheel_report));

                        send_hid_report(hid_dev, report, sizeof(wheel_report));
                }

                /* Move cur to prev*/
                prev_angle.val1 = curr_angle.val1;
        }

        return 0;
}
