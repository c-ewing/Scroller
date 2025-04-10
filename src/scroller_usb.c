#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

#include "scroller_config.h"
#include "scroller_sensor.h"

LOG_MODULE_REGISTER(scroller_usb, LOG_LEVEL_DBG);

/* USB HID report descriptor bytes */
static const uint8_t hid_report_desc[] = HID_WHEEL_REPORT_DESC();

/* Mouse Report */
struct __packed wheel_report_t
{
    uint8_t report_id;
    int16_t wheel;
};

/* Semaphore for writing reports over USB.
 * When a write on the IN endpoint is started, take the semaphore,
 * When all data is written to the IN endpoint, release the semaphore.
 */
static K_SEM_DEFINE(ep_write_sem, 0, 1);

/* Semaphore for enabling USB after connection */
static K_SEM_DEFINE(usb_conf_sem, 0, 1);

/* Semaphore for suspending USB connection */
static K_SEM_DEFINE(usb_suspend_sem, 1, 1);

/* Callback for IN endpoint transfer completion */
static void int_in_ready_cb(const struct device *dev)
{
    ARG_UNUSED(dev);
    /* Release the write semaphore allowing another write to occur */
    k_sem_give(&ep_write_sem);
}

/* Callback for Get_Report requests */
static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len, uint8_t **data)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(len);
    ARG_UNUSED(data);

    /* Get the feature report (0x0300) with report ID 2 (0x0002)*/
    if (setup->wValue == 0x0302)
    {
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

    /* Check to see if the first byte is 0x02, the report id for the Resolution Multiplier report
     * and enable high res scrolling if the next value is greater than 0. Linux and Windows use a fixed 120
     * high res scrolls per basic scroll so the set value resolution multiplier doesn't matter here
     */
    // FIXME: Better check here, and check the length of the data to make sure its not overrunning
    if ((*data)[0] == 0x02 && (*data)[1] > 0)
    {
        LOG_INF("HI-res enabled");
        SCROLLER_CONFIG.internal_divider = SCROLLER_STEPS_HI_RES;
    }

    return 0;
}

/* Registers HID callback */
static const struct hid_ops ops = {
    .get_report = get_report_cb,
    .set_report = set_report_cb,
    .int_in_ready = int_in_ready_cb,
};

/* Take the status reported by the callback and process it */
static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    ARG_UNUSED(param);

    /* Try taking the semaphore on all states other than configured */
    switch (status)
    {
    case USB_DC_UNKNOWN:
        /* Connection status unknown */
        break;
    case USB_DC_CONNECTED:
        /* Physically connected */
        k_sem_take(&usb_conf_sem, K_NO_WAIT);
        break;
    case USB_DC_RESET:
        /* Reset device state to defaults */
        k_mutex_lock(&scroller_config_mutex, K_FOREVER);
        SCROLLER_CONFIG.internal_divider = SCROLLER_STEPS_LOW_RES;
        SCROLLER_CONFIG.scroll_accumulator = 0;
        k_mutex_unlock(&scroller_config_mutex);
        /* Device is unconfigured and not suspended */
        k_sem_take(&usb_conf_sem, K_NO_WAIT);
        k_sem_give(&usb_suspend_sem);
        break;
    case USB_DC_CONFIGURED:
        /* Device configured for data transfer */
        k_sem_give(&usb_conf_sem);
        break;
    case USB_DC_DISCONNECTED:
        /* Physically disconnected */
        // FIXME: In the USB thread, stop trying to send packets
        k_sem_take(&usb_conf_sem, K_NO_WAIT);
        break;
    case USB_DC_SUSPEND:
        /* Suspend the device, does not change configuration just pauses connection */
        // FIXME: In the USB thread, stop trying to send packets
        k_sem_take(&usb_suspend_sem, K_NO_WAIT);
        break;
    case USB_DC_RESUME:
        /* Resume the device, no configuration change */
        // FIXME: In the USB thread, re-enable sending packets
        k_sem_give(&usb_suspend_sem);
        break;
    default:
        LOG_WRN("Unhandled USB state: %d", status);
    }
}

/* Initialize the USB HID device */
int scroller_usb_init()
{
    const struct device *hid_dev;
    int err;

    /* Get the usb hid device binding */
    hid_dev = device_get_binding("HID_0");

    if (hid_dev == NULL)
    {
        LOG_ERR("Cannot get USB device binding");
        return -EINVAL;
    }

    // Log the HID report descriptor
    // LOG_HEXDUMP_INF(hid_report_desc, sizeof(hid_report_desc), "HID Report Descriptor");

    /* Attach the HID report descriptor to the HID device */
    usb_hid_register_device(hid_dev,
                            hid_report_desc, sizeof(hid_report_desc),
                            &ops);

    /* Initialize HID device */
    err = usb_hid_init(hid_dev);
    if (err < 0)
    {
        LOG_ERR("Cannot initialize USB HID");
        return err;
    }

    /* Enable USB and bind status callback */
    err = usb_enable(status_cb);
    if (err < 0)
    {
        LOG_ERR("Failed to enable USB");
        return err;
    }

    return 0;
}

int send_report(const struct device *hid_dev, uint8_t *report, size_t report_size)
{
    int err;

    /* Make sure the USB device is configured and not suspended before trying to write */
    if (!k_sem_count_get(&usb_conf_sem) || !k_sem_count_get(&usb_suspend_sem))
    {
        LOG_INF("Report not sent: %d, %d", k_sem_count_get(&usb_conf_sem), k_sem_count_get(&usb_suspend_sem));
        return -EINVAL;
    }

    /* Write the report to the HID interrupt endpoint */
    err = hid_int_ep_write(hid_dev, report, report_size, NULL);
    if (err)
    {
        LOG_ERR("HID write error, %d", err);
        return err;
    }
    else
    {
        /* Wait for the write to complete */
        k_sem_take(&ep_write_sem, K_FOREVER);
    }

    return 0;
}

/* USB HID report sending thread */
void send_report_usb()
{
    const struct device *hid_dev;
    int err;

    struct wheel_report_t wheel_report = {
        .report_id = 0x01,
        .wheel = 0,
    };

    /* Get the usb hid device binding */
    hid_dev = device_get_binding("HID_0");

    if (hid_dev == NULL)
    {
        LOG_ERR("Cannot get USB device binding");
        return;
    }

    UDC_STATIC_BUF_DEFINE(report, sizeof(wheel_report));

    while (1)
    {
        /* Wait for a message to be available */
        k_msgq_get(&sensor_msgq, &wheel_report.wheel, K_FOREVER);

        /* Copy the report to the static buffer */
        memcpy(report, &wheel_report, sizeof(wheel_report));

        err = send_report(hid_dev, report, sizeof(wheel_report));
        if (err)
        {
            LOG_WRN("Dropped report: %d", err);
        }
    }
}

/* USB report sending thread */
// FIXME: Re-enable USB
// K_THREAD_DEFINE(usb_send_thread, 1024, send_report_usb, NULL, NULL, NULL, SCROLLER_SEND_THREAD_PRIORITY, 0, 0);