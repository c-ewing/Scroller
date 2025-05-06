#include "usb_state_event.h"

/* Convert the state to human readable */
static const char *states[] = {
    [USB_STATE_DISCONNECTED] = "DISCONNECTED",
    [USB_STATE_POWER_ONLY] = "POWER ONLY",
    [USB_STATE_CONFIGURED] = "CONFIGURED",
    [USB_STATE_SUSPENDED] = "SUSPENDED",
};

static void log_usb_state_event(const struct app_event_header *aeh)
{
        struct usb_state_event *event = cast_usb_state_event(aeh);

        APP_EVENT_MANAGER_LOG(aeh, "USB state: %s", states[event->state]);
}

APP_EVENT_TYPE_DEFINE(usb_state_event,                                               /* Unique event name. */
                      log_usb_state_event,                                           /* Function logging event data. */
                      NULL,                                                          /* No event info provided. */
                      APP_EVENT_FLAGS_CREATE(APP_EVENT_TYPE_FLAGS_INIT_LOG_ENABLE)); /* Flags managing event type. */
