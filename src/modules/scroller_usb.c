#define MODULE scroller_usb
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#include <zephyr/usb/usb_device.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usb_hid.h>

#include "usb_state_event.h"
#include "scroller_config.h"
#include "scroller_scroll_calculate.h"
#include <caf/events/force_power_down_event.h>
#include <caf/events/power_event.h>

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

/* Stack for USB thread*/
// FIXME: kconfig for stack size
static K_THREAD_STACK_DEFINE(usb_thread_stack, 1024);
static struct k_thread usb_thread;

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
        int16_t steps;
        /* Wait for a message to be available */
        err = k_msgq_get(&step_msgq, &steps, K_FOREVER);
        if (err)
        {
            LOG_WRN("Recieve error: %d", err);
        }

        wheel_report.wheel = steps;

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

    struct usb_state_event *event = new_usb_state_event();
    event->state = transition;

    APP_EVENT_SUBMIT(event);
}

void process_usb_state_event(struct usb_state_event *event)
{
    /* Only process on state transitions */
    if (USB_STATE == event->state)
    {
        return;
    }

    /* Wake up device on resume */
    switch (event->state)
    {

    case USB_STATE_CONFIGURED:
    {
        /* Start sender then sensor to keep send buffer clear */
        struct wake_up_event *event = new_wake_up_event();
        APP_EVENT_SUBMIT(event);
        k_thread_resume(&usb_thread);

        break;
    }

    default:
        /* Don't re-suspend the device */
        if (USB_STATE != USB_STATE_CONFIGURED)
        {
            /* Stop the sender and then the sensor to avoid HID errors from trying to send data when suspended */
            struct force_power_down_event *event = new_force_power_down_event();
            APP_EVENT_SUBMIT(event);
            k_thread_suspend(&usb_thread);
        }

        break;
    }

    USB_STATE = event->state;
}

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
    k_thread_suspend(&usb_thread);

    return err;
}

/* Event handler for all possible incoming events */
static bool app_event_handler(const struct app_event_header *aeh)
{
    int err;

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
            err = init();
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

    if (is_usb_state_event(aeh))
    {
        struct usb_state_event *event = cast_usb_state_event(aeh);
        process_usb_state_event(event);
    }

    /* Don't consume the event */
    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
/* Listen for modules changing state */
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
/* Listen for usb_state_events */
APP_EVENT_SUBSCRIBE(MODULE, usb_state_event);
