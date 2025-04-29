#include <caf/sensor_manager.h>
#include <zephyr/drivers/sensor/ams_as5600.h>

#define STEP_SENSOR DT_NODELABEL(as5600)

static const struct caf_sampled_channel step_channel[] = {
    {
        .chan = AS5600_SENSOR_CHAN_FILTERED_STEPS,
        .data_cnt = 1,
    },
};
static const struct sm_sensor_config sensor_configs[] = {
    {
        .dev = DEVICE_DT_GET(STEP_SENSOR),
        .event_descr = "step",
        .chans = step_channel,
        .chan_cnt = ARRAY_SIZE(step_channel),
        .sampling_period_ms = 500,
        .active_events_limit = 3,
    },
};
