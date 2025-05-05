
# Nordic nRF Connect SDK:
## CAF Configuration files
Need to define the configuration file location in your CMakeLists.txt. This doesn't seem to be mentioned anywhere
in the CAF documentation
```cmake
set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/configuration/\${NORMALIZED_BOARD_TARGET}")

zephyr_include_directories(
  configuration/common
  ${APPLICATION_CONFIG_DIR}
)
```

## CAF sensor module
Sensor events *MUST NOT* be consumed by any listener. The sensor module registers itself as the final listener
and decrements the sensor event count.

# Templates:
## Module Template
#define MODULE scroller_ble
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(MODULE, LOG_LEVEL_DBG);

#define MODULE_INIT_VAR MODULE##_init
static bool MODULE_INIT_VAR = false;

static int init()
{
    return 0;
}

static void process_module_state_event(struct module_state_event *event)
{
    int err;

    /* Check the state of main module. Wait for it to come up and then enable the USB stack */
    if (check_state(event, MODULE_ID(main), MODULE_STATE_READY))
    {
        /* Check to make sure BLE hasn't already been initialized */
        if (MODULE_INIT_VAR)
        {
            LOG_ERR("Already initialized");
        }

        /* Initalize and set module state */
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

/* Event handler for incoming events */
static bool app_event_handler(const struct app_event_header *aeh)
{
    if (is_module_state_event(aeh))
    {
        struct module_state_event *event = cast_module_state_event(aeh);

        process_module_state_event(event);
    }

    /* Don't consume the event */
    return false;
}
APP_EVENT_LISTENER(MODULE, app_event_handler);
/* Listen for modules changing state */
APP_EVENT_SUBSCRIBE(MODULE, module_state_event);