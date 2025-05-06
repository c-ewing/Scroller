#define MODULE scroller_scroll_calculate
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#include "scroller_config.h"
#include <caf/events/sensor_event.h>
#include <caf/events/power_event.h>

/* Message queue for step values to be emitted from the HID device
 * Only has space for 2 values but it should never fill.
 */
K_MSGQ_DEFINE(step_msgq, sizeof(int16_t), 2, 1);

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

    // FIXME: Move to local to avoid the global lock. Plus only needed here.
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

    if (steps > INT16_MAX)
    {
        LOG_WRN("Steps overflowing 16bits, truncating: %d", steps);
        return INT16_MAX;
    }
    else if (steps < INT16_MIN)
    {
        LOG_WRN("Steps overflowing 16bits, truncating: %d", steps);
        return INT16_MIN;
    }
    else
    {
        return (int16_t)steps;
    }
}

/* Process sensor event */
void process_sensor_event(struct sensor_event *event)
{
    int err;

    if (event->dyndata.size != 8)
    {
        LOG_ERR("Wrong size: %d", event->dyndata.size);
        return;
    }

    struct sensor_value position;
    /* memcpy to avoid alignment/aliasing issues and take ownership incase the event is consumed before being sent */
    memcpy(&position, event->dyndata.data, event->dyndata.size);

    int16_t steps = calculate_scroll(position.val1);

    /* Avoid filling the queue with no change */
    if (steps == 0)
    {
        return;
    }

    // FIXME: Race condition on the msg queue.
    err = k_msgq_put(&step_msgq, &steps, K_NO_WAIT);
    if (err < 0)
    {
        LOG_WRN("Failed to put queue: %d, %d, %d", err, k_msgq_num_used_get(&step_msgq), steps);
    }
}

/* No initialization needed */
int init()
{
    return 0;
}

/* Process module state event */
void process_module_state_event(struct module_state_event *event)
{
    int err;

    /* Check the state of main module. Wait for it to come up */
    if (check_state(event, MODULE_ID(main), MODULE_STATE_READY))
    {

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

/* Process wake up event */
void process_wake_up_event(struct wake_up_event *event)
{
    LOG_WRN("Wakeup");
}

/* Event handler for incoming events */
static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_module_state_event(aeh))
    {
        struct module_state_event *event = cast_module_state_event(aeh);
        process_module_state_event(event);
    }
    else if (is_sensor_event(aeh))
    {
        struct sensor_event *event = cast_sensor_event(aeh);
        process_sensor_event(event);
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
/* Listen for modules changing state */
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);
/* Listen for sensor events */
APP_EVENT_SUBSCRIBE(MODULE, sensor_event);
/* Listen for power events */
APP_EVENT_SUBSCRIBE(MODULE, power_event);