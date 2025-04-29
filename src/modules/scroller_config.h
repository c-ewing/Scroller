#ifndef SCROLLER_CONFIG_H
#define SCROLLER_CONFIG_H

#include <zephyr/usb/class/hid.h>
#include "hid_extensions.h"

/* Thread priority 2 for sensor reading to allow for preempting */
#define SCROLLER_SENSOR_THREAD_PRIORITY 0x02

/* Thread priority 1 for send threads, taking precedence over reading */
#define SCROLLER_SEND_THREAD_PRIORITY 0x01

/* Report Frequency (ms) */
#define SCROLLER_REPORT_FREQUENCY 5

/* Default step scaling values */
#define SCROLLER_STEPS_LOW_RES 120
#define SCROLLER_STEPS_HI_RES 1

/* Scroller config */
struct scroller_config_t
{
    int32_t scroll_accumulator;
    int32_t internal_divider;
};

/* Initialize config and config mutex */
struct scroller_config_t SCROLLER_CONFIG = {
    .scroll_accumulator = 0,
    .internal_divider = SCROLLER_STEPS_LOW_RES,
};
K_MUTEX_DEFINE(scroller_config_mutex);
extern struct k_mutex scroller_config_mutex;

/*-- HID REPORT --*/

#define SCROLLER_RESOLUTION_MULTIPLIER 128
#define SCROLLER_RESOLUTION_MULTIPLIER_REPORT_BITS 7
#define SCROLLER_L_MIN_L8 0x00
#define SCROLLER_L_MIN_H8 0x80
#define SCROLLER_L_MAX_L8 0xFF
#define SCROLLER_L_MAX_H8 0x7F
#define SCROLLER_WHEEL_INPUT 0b00001110 /* Data, Var, Abs, Wrap */
/**
 * @brief Define HID Wheel Report Descriptor.
 *
 * @return  HID Wheel Report Descriptor
 */
#define HID_WHEEL_REPORT_DESC()                                                                             \
    {                                                                                                       \
        HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),                                                              \
        HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),                                                           \
        HID_COLLECTION(HID_COLLECTION_APPLICATION),                                                         \
        HID_USAGE_PAGE(HID_USAGE_GEN_DESKTOP),                                                              \
        HID_USAGE(HID_USAGE_GEN_DESKTOP_POINTER),                                                           \
        HID_COLLECTION(HID_COLLECTION_PHYSICAL),                                                            \
        HID_COLLECTION(HID_COLLECTION_LOGICAL),                                                             \
        HID_REPORT_ID(2), /* Feature Report for Res Mult*/                                                  \
        HID_USAGE(0x48),  /* Resolution Multiplier */                                                       \
        HID_REPORT_COUNT(1),                                                                                \
        HID_REPORT_SIZE(SCROLLER_RESOLUTION_MULTIPLIER_REPORT_BITS),                                        \
        HID_LOGICAL_MIN8(0),                               /* Disable Res Mult */                           \
        HID_LOGICAL_MAX8(1),                               /* Enable Res Mult*/                             \
        HID_PHYSICAL_MIN8(1),                              /* 1 step per detent */                          \
        HID_PHYSICAL_MAX8(SCROLLER_RESOLUTION_MULTIPLIER), /* steps per detent, auto selected by windows */ \
        HID_FEATURE(0b00000010),                           /* Data, Var, Abs */                             \
        HID_REPORT_ID(1),                                  /* Input report for Wheel */                     \
        HID_USAGE(HID_USAGE_GEN_DESKTOP_WHEEL),                                                             \
        HID_PHYSICAL_MIN8(0),                                                                               \
        HID_PHYSICAL_MAX8(0),                                                                               \
        HID_LOGICAL_MIN16(SCROLLER_L_MIN_L8, SCROLLER_L_MIN_H8),                                            \
        HID_LOGICAL_MAX16(SCROLLER_L_MAX_L8, SCROLLER_L_MAX_H8),                                            \
        HID_REPORT_SIZE(16),                                                                                \
        HID_INPUT(SCROLLER_WHEEL_INPUT), /* Data, Var, Rel */                                               \
        HID_END_COLLECTION,                                                                                 \
        HID_COLLECTION(HID_COLLECTION_LOGICAL), /* Horz Scroll */                                           \
        HID_REPORT_ID(2),                       /* Feature Report for Res Mult*/                            \
        HID_USAGE(0x48),                        /* Resolution Multiplier */                                 \
        HID_REPORT_SIZE(SCROLLER_RESOLUTION_MULTIPLIER_REPORT_BITS),                                        \
        HID_LOGICAL_MIN8(0),                               /* Disable Res Mult */                           \
        HID_LOGICAL_MAX8(1),                               /* Enable Res Mult*/                             \
        HID_PHYSICAL_MIN8(1),                              /* 1 step per detent */                          \
        HID_PHYSICAL_MAX8(SCROLLER_RESOLUTION_MULTIPLIER), /* steps per detent, auto selected by windows */ \
        HID_FEATURE(0b00000010),                           /* Data, Var, Abs */                             \
        HID_PHYSICAL_MIN8(0),                                                                               \
        HID_PHYSICAL_MAX8(0),                                                                               \
        HID_REPORT_ID(1),     /* Input report for H. Wheel */                                               \
        HID_USAGE_PAGE(0x0C), /* Consumer Device */                                                         \
        HID_LOGICAL_MIN16(SCROLLER_L_MIN_L8, SCROLLER_L_MIN_H8),                                            \
        HID_LOGICAL_MAX16(SCROLLER_L_MAX_L8, SCROLLER_L_MAX_H8),                                            \
        HID_REPORT_SIZE(16),                                                                                \
        HID_USAGE_16(0x38, 0x02),        /* AC Pan */                                                       \
        HID_INPUT(SCROLLER_WHEEL_INPUT), /* Data, Var, Rel */                                               \
        HID_END_COLLECTION,                                                                                 \
        HID_END_COLLECTION,                                                                                 \
        HID_END_COLLECTION,                                                                                 \
    }

#endif /* SCROLLER_CONFIG_H */