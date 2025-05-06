#include <caf/events/module_state_event.h>

static inline void get_req_modules(struct module_flags *mf)
{
    /* Wait for these modules to start before loading settings*/
    module_flags_set_bit(mf, MODULE_IDX(main));

    module_flags_set_bit(mf, MODULE_IDX(ble_state));
};