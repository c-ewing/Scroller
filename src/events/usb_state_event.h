#ifndef USB_STATE_EVENT_H
#define USB_STATE_EVENT_H

#include <app_event_manager.h>

enum usb_state
{
    /* State unknown or disconnected */
    USB_STATE_DISCONNECTED,
    /* State connected, no data transfer with host */
    USB_STATE_POWER_ONLY,
    /* State connected, data transfer with host */
    USB_STATE_CONFIGURED,
    /* State suspended, data transfer with host suspended */
    USB_STATE_SUSPENDED,
};

struct usb_state_event
{
    struct app_event_header header;

    enum usb_state state;
};
APP_EVENT_TYPE_DECLARE(usb_state_event);

#endif /* USB_STATE_EVENT_H */