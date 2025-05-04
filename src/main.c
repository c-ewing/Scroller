#include <app_event_manager.h>
#define MODULE main
#include <caf/events/module_state_event.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include "scroller_config.h"

struct scroller_config_t SCROLLER_CONFIG;
struct k_mutex scroller_config_mutex;

/* Initialize global config */
void init_conf()
{
        /* Initialize config and config mutex */
        SCROLLER_CONFIG = (struct scroller_config_t){
            .scroll_accumulator = 0,
            .internal_divider = SCROLLER_STEPS_LOW_RES,
        };

        k_mutex_init(&scroller_config_mutex);
}

// FIXME: Control over usb serial device descriptor? To be able to change settings? Could be later used for test harnessing?
int main(void)
{
        printk("Scroller v0.1 Test Application\n");

        init_conf();

        if (app_event_manager_init())
        {
                LOG_ERR("Application event manager failed to init");
                return -EINVAL;
        }
        else
        {
                module_set_state(MODULE_STATE_READY);
        }

        return 0;
}
