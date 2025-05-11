#define MODULE idle_waker
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#include <caf/events/power_event.h>
#include <zephyr/drivers/sensor/ams_as5600.h>

#define MODULE_INIT_VAR MODULE##_init
static bool MODULE_INIT_VAR = false;

static struct k_timer wake_up_timer;
static struct k_work wake_up_work;

static struct sensor_value delta = {
    .val1 = 0xFF000000,
    .val2 = 0,
};

extern void idle_waker_callback(struct k_timer *timer_id)
{
    k_work_submit(&wake_up_work);
}

static void wake_up_work_callback(struct k_work *work)
{
    const struct device *sensor;
    // Check i2c pheripheral state:
    const struct device *i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    int err;

    if (!device_is_ready(i2c_dev))
    {
        printk("I2C0 device is not ready\n");
    }

    sensor = DEVICE_DT_GET(DT_NODELABEL(as5600));
    if (!device_is_ready(sensor))
    {
        LOG_ERR("Sensor not ready");
        return;
    }

    err = sensor_sample_fetch_chan(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS);
    if (err < 0)
    {
        LOG_ERR("Could not fetch samples (%d)", err);
        return;
    }

    struct sensor_value reading;
    err = sensor_channel_get(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS, &reading);
    if (err < 0)
    {
        LOG_ERR("Could not get samples (%d)", err);
    }
    LOG_INF("Pos: %d", reading.val1);

    // If not initialized the grab an initial position and return
    if (delta.val1 == 0xFF000000)
    {
        LOG_INF("Set initial reading: %d", reading.val1);
        delta = reading;
        return;
    }

    // 10 step threshold:
    int change = abs(delta.val1 - reading.val1);
    if (change > 10)
    {
        LOG_WRN("Change detectect: %d", change);

        struct wake_up_event *event = new_wake_up_event();
        APP_EVENT_SUBMIT(event);
    }
    // keep sleeping
}

static int init()
{
    k_timer_init(&wake_up_timer, idle_waker_callback, NULL);
    k_work_init(&wake_up_work, wake_up_work_callback);

    return 0;
}

static void process_power_down_event(struct power_down_event *event)
{
    LOG_INF("Starting idle timer");
    k_timer_start(&wake_up_timer, K_MSEC(500), K_MSEC(1000));
}

static void process_wake_up_event(struct wake_up_event *event)
{
    LOG_INF("Idle timer stopped");
    k_timer_stop(&wake_up_timer);
    delta.val1 = 0xFF000000;
}

static void process_module_state_event(struct module_state_event *event)
{
    int err;

    if (check_state(event, MODULE_ID(main), MODULE_STATE_READY))
    {
        if (MODULE_INIT_VAR)
        {
            LOG_ERR("Already initialized");
        }

        err = init();
        if (err)
        {
            module_set_state(MODULE_STATE_ERROR);
            LOG_ERR("Init err: %d", err);
        }
        else
        {
            module_set_state(MODULE_STATE_READY);
        }
    }
}

static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_module_state_event(aeh))
    {
        struct module_state_event *event = cast_module_state_event(aeh);

        process_module_state_event(event);
    }
    else if (is_power_down_event(aeh))
    {
        struct power_down_event *event = cast_power_down_event(aeh);
        process_power_down_event(event);
    }
    else if (is_wake_up_event(aeh))
    {
        struct wake_up_event *event = cast_wake_up_event(aeh);
        process_wake_up_event(event);
    }

    /* Don't consume the event */
    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);