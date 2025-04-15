#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/sensor/ams_as5600.h>

#include "scroller_config.h"
#include "scroller_sensor.h"

LOG_MODULE_REGISTER(scroller_sensor, LOG_LEVEL_DBG);

/* Sensor value message queue */
K_MSGQ_DEFINE(sensor_msgq, sizeof(int16_t), 2, 1);

/* Sensor read thread
 *
 * Responsible for reading the AS5600 sensor and enqueuing the data to be
 * consumed by either a USB or BT sender.
 */
void sensor_thread_handler(void)
{
    const struct device *sensor;
    int ret;

    sensor = DEVICE_DT_GET(DT_NODELABEL(as5600));
    if (!device_is_ready(sensor))
    {
        LOG_ERR("Sensor not ready");
        return;
    }

    // Read off the AS5600
    ret = sensor_sample_fetch_chan(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS);
    if (ret < 0)
    {
        LOG_ERR("Could not fetch samples (%d)", ret);
        return;
    }

    struct sensor_value prev_angle;
    struct sensor_value curr_angle;

    while (1)
    {
        ret = sensor_channel_get(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS, &prev_angle);
        if (ret < 0)
        {
            LOG_ERR("Could not get samples (%d)", ret);
        }

        k_msleep(SCROLLER_REPORT_FREQUENCY);

        ret = sensor_sample_fetch_chan(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS);
        if (ret < 0)
        {
            LOG_ERR("Could not fetch samples (%d)", ret);
            continue;
        }

        ret = sensor_channel_get(sensor, AS5600_SENSOR_CHAN_FILTERED_STEPS, &curr_angle);
        if (ret < 0)
        {
            LOG_ERR("Could not get samples (%d)", ret);
            continue;
        }

        /* Process the step delta accounting for roll over at 0/4095 */
        int16_t delta = prev_angle.val1 - curr_angle.val1;

        /* If there is not change just continue */
        if (!delta)
        {
            continue;
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
        int steps = SCROLLER_CONFIG.scroll_accumulator / SCROLLER_CONFIG.internal_divider;
        SCROLLER_CONFIG.scroll_accumulator %= SCROLLER_CONFIG.internal_divider;

        /* Release the global config */
        k_mutex_unlock(&scroller_config_mutex);

        /* If steps > 0 */
        if (steps)
        {
            k_msgq_put(&sensor_msgq, &steps, K_NO_WAIT);
        }

        /* Move cur to prev*/
        prev_angle.val1 = curr_angle.val1;
    }
}

/* Sensor thread */
K_THREAD_DEFINE(sensor_thread, 1024, sensor_thread_handler, NULL, NULL, NULL, SCROLLER_SENSOR_THREAD_PRIORITY, 0, 0);
