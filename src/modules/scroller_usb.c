#define MODULE scroller_usb
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

#include "usb_state_event.h"
#include "scroller_config.h"
#include "caf/events/sensor_event.h"

// #####
/* USB initialization state */
static bool USB_INIT = false;
/* USB state */
static enum usb_state USB_STATE;

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

// FIXME: Move to middleware
K_MSGQ_DEFINE(sensor_msgq, sizeof(int32_t), 2, 1);

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

// FIXME: Move to middleware
/* Convert raw position to step change */
int16_t calculate_scroll(int32_t sensor_steps)
{
    static int16_t prev_steps;
    int16_t curr_steps = (sensor_steps & 0xFFFF);

    int16_t delta = prev_steps - curr_steps;

    if (!delta)
    {
        return 0;
    }

    /* Handle wrapping the zero point */
    if (delta > 2048)
    {
        delta -= 4096; /* Negative direction wrap */
    }
    else if (delta < -2048)
    {
        delta += 4096; /* Positive direction wrap */
    }

    /* Invert scroll direction */
    delta *= -1;

    /* Lock the global config while manipulating */
    k_mutex_lock(&scroller_config_mutex, K_FOREVER);
    SCROLLER_CONFIG.scroll_accumulator += delta;

    /*
     * Apply an internal scroll accumulator. The linux kernel only supports down to
     * (int)(steps * 120 / RES MULT) resulting a maximum of 120 steps per detent. Fractional
     * scrolling is not supported. The sensor emits 4096/120 ~34 detents per revolution
     * which is high.
     */

    /* Steps are integer part of accumulated steps over the internal multiplier */
    int32_t steps = SCROLLER_CONFIG.scroll_accumulator / SCROLLER_CONFIG.internal_divider;
    SCROLLER_CONFIG.scroll_accumulator %= SCROLLER_CONFIG.internal_divider;

    /* Release the global config */
    k_mutex_unlock(&scroller_config_mutex);

    /* Move cur to prev*/
    prev_steps = curr_steps;

    // FIXME: Downcast without check
    return steps;
}

int send_report(const struct device *hid_dev, uint8_t *report, size_t report_size)
{
    int err;

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
void usb_thread_fn()
{
    const struct device *hid_dev;
    int err;

    LOG_INF("USB_Thread Started");

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
        int32_t pos;
        /* Wait for a message to be available */
        k_msgq_get(&sensor_msgq, &pos, K_FOREVER);

        wheel_report.wheel = calculate_scroll(pos);

        /* Copy the report to the static buffer */
        memcpy(report, &wheel_report, sizeof(wheel_report));

        err = send_report(hid_dev, report, sizeof(wheel_report));
        if (err)
        {
            LOG_WRN("Dropped report: %d", err);
        }
    }
}

/* Take the status reported by the callback and process it */
static inline void status_cb(enum usb_dc_status_code status, const uint8_t *param)
{
    ARG_UNUSED(param);
    enum usb_state transition;
    static enum usb_state before_suspend;

    switch (status)
    {
    case USB_DC_UNKNOWN:
        /* Connection status unknown */
        transition = USB_STATE_DISCONNECTED;
        break;
    case USB_DC_CONNECTED:
        /* Physically connected */
        transition = USB_STATE_POWER_ONLY;
        break;
    case USB_DC_RESET:
        /* Reset device state to defaults */
        transition = USB_STATE_POWER_ONLY;
        break;
    case USB_DC_CONFIGURED:
        /* Device configured for data transfer */
        transition = USB_STATE_CONFIGURED;
        break;
    case USB_DC_DISCONNECTED:
        /* Physically disconnected */
        transition = USB_STATE_DISCONNECTED;
        break;
    case USB_DC_SUSPEND:
        /* Suspend the device, does not change configuration just pauses connection */
        before_suspend = USB_STATE;
        transition = USB_STATE_SUSPENDED;
        break;
    case USB_DC_RESUME:
        /* Resume the device, no configuration change */
        transition = before_suspend;
        break;
    default:
        LOG_WRN("Unhandled USB state: %d", status);
        return;
    }

    /* On a state transition, broadcast an event */
    if (USB_STATE != transition)
    {
        struct usb_state_event *event = new_usb_state_event();
        event->state = transition;

        APP_EVENT_SUBMIT(event);
        USB_STATE = transition;
    }
}

/* Stack for USB thread*/
// FIXME: kconfig for stack size
static K_THREAD_STACK_DEFINE(usb_thread_stack, 1024);
static struct k_thread usb_thread;

/* USB initialization */
static int init()
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

    err = usb_enable(status_cb);
    if (err < 0)
    {
        LOG_ERR("Failed to enable USB");
    }

    /* USB thread */
    k_thread_create(&usb_thread, usb_thread_stack, 1024,
                    (k_thread_entry_t)usb_thread_fn, NULL, NULL, NULL,
                    SCROLLER_SEND_THREAD_PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(&usb_thread, "usb_sender");

    return err;
}

/* Event handler for all possible incoming events */
static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_module_state_event(aeh))
    {
        struct module_state_event *event = cast_module_state_event(aeh);

        /* Check the state of main module. Wait for it to come up and then enable the USB stack */
        if (check_state(event, MODULE_ID(main), MODULE_STATE_READY))
        {
            /* Check to make sure USB hasn't already been initialized */
            if (USB_INIT)
            {
                LOG_ERR("USB already initialized");
            }

            /* Initalize and set module state */
            int err = init();
            if (err)
            {
                module_set_state(MODULE_STATE_ERROR);
                LOG_ERR("USB init err: %d", err);
            }
            else
            {
                module_set_state(MODULE_STATE_READY);
            }
        }
    }
    else if (is_sensor_event(aeh))
    {
        struct sensor_event *event = cast_sensor_event(aeh);

        /* If configured and the HID device is setup, send report */
        if (USB_STATE == USB_STATE_CONFIGURED)
        {
            if (event->dyndata.size != 8)
            {
                LOG_ERR("Wrong size: %d", event->dyndata.size);
            }
            else
            {
                struct sensor_value position;
                /* memcpy to avoid alignment/aliasing issues and take ownership incase the event is consumed before being sent */
                memcpy(&position, event->dyndata.data, event->dyndata.size);
                // FIXME: Check return?
                k_msgq_put(&sensor_msgq, &position.val1, K_NO_WAIT);

                /* Consume the event so avoid sending to multiple senders */
                return false;
            }
        }
    }

    /* Don't consume the event */
    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
/* Listen for modules changing state */
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
/* Listen for sensor events */
APP_EVENT_SUBSCRIBE_FIRST(MODULE, sensor_event);