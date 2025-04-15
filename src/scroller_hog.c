/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <zephyr/drivers/gpio.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

#include "scroller_config.h"
#include "scroller_sensor.h"

LOG_MODULE_REGISTER(scroller_hog, LOG_LEVEL_DBG);

enum
{
    HIDS_REMOTE_WAKE = BIT(0),
    HIDS_NORMALLY_CONNECTABLE = BIT(1),
};

struct hids_info
{
    uint16_t version; /* version number of base USB HID Specification */
    uint8_t code;     /* country HID Device hardware is localized for. */
    uint8_t flags;
} __packed;

struct hids_report
{
    uint8_t id;   /* report id */
    uint8_t type; /* report type */
} __packed;

static struct hids_info info = {
    .version = 0x0000,
    .code = 0x00,
    .flags = HIDS_NORMALLY_CONNECTABLE,
};

enum
{
    HIDS_INPUT = 0x01,
    HIDS_OUTPUT = 0x02,
    HIDS_FEATURE = 0x03,
};

static struct hids_report input = {
    .id = 0x01,
    .type = HIDS_INPUT,
};

static struct hids_report feature = {
    .id = 0x02,
    .type = HIDS_FEATURE,
};

static uint8_t simulate_input;
static uint8_t ctrl_point;
static uint8_t report_map[] = HID_WHEEL_REPORT_DESC();

static ssize_t read_info(struct bt_conn *conn,
                         const struct bt_gatt_attr *attr, void *buf,
                         uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_info));
}

static ssize_t read_report_map(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr, void *buf,
                               uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, report_map,
                             sizeof(report_map));
}

static ssize_t read_report(struct bt_conn *conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, attr->user_data,
                             sizeof(struct hids_report));
}

static void input_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    simulate_input = (value == BT_GATT_CCC_NOTIFY) ? 1 : 0;
}

static ssize_t read_input_report(struct bt_conn *conn,
                                 const struct bt_gatt_attr *attr, void *buf,
                                 uint16_t len, uint16_t offset)
{
    return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
}

static ssize_t read_feature_report(struct bt_conn *conn,
                                   const struct bt_gatt_attr *attr, void *buf,
                                   uint16_t len, uint16_t offset)
{
    // FIXME: This feels hacky however OS's always use a fixed 120 for the resolution multiplier
    int16_t fake_resolution;

    k_mutex_lock(&scroller_config_mutex, K_FOREVER);
    if (SCROLLER_CONFIG.internal_divider == SCROLLER_STEPS_LOW_RES)
    {
        fake_resolution = 1;
        LOG_INF("Reporting Low-Res");
    }
    else
    {
        fake_resolution = 120;
        LOG_INF("Reporting High-Res");
    }
    k_mutex_unlock(&scroller_config_mutex);

    return bt_gatt_attr_read(conn, attr, buf, len, offset, &fake_resolution, sizeof(int16_t));
}

static ssize_t write_feature_report(struct bt_conn *conn,
                                    const struct bt_gatt_attr *attr,
                                    const void *buf, uint16_t len,
                                    uint16_t offset, uint8_t flags)
{
    /* Feature Report Value */
    int16_t value;

    /* Check the data recieved before copying */
    if (offset + len > sizeof(value))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    memcpy(&value, (((uint8_t *)buf) + offset), len);

    /* If there is a value sent it is enabling high resolution */
    if (value)
    {
        k_mutex_lock(&scroller_config_mutex, K_FOREVER);
        SCROLLER_CONFIG.internal_divider = SCROLLER_STEPS_HI_RES;
        LOG_INF("Enabled high resolution scrolling");
        k_mutex_unlock(&scroller_config_mutex);
    }

    return len;
}

static ssize_t write_ctrl_point(struct bt_conn *conn,
                                const struct bt_gatt_attr *attr,
                                const void *buf, uint16_t len, uint16_t offset,
                                uint8_t flags)
{
    uint8_t *value = attr->user_data;

    if (offset + len > sizeof(ctrl_point))
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    memcpy(value + offset, buf, len);

    return len;
}

/* HID Service Declaration */
BT_GATT_SERVICE_DEFINE(hog_svc,
                       BT_GATT_PRIMARY_SERVICE(BT_UUID_HIDS),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_INFO, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_info, NULL, &info),
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT_MAP, BT_GATT_CHRC_READ,
                                              BT_GATT_PERM_READ, read_report_map, NULL, NULL),

                       /* Report 1: Input Report */
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
                                              BT_GATT_PERM_READ_ENCRYPT,
                                              read_input_report, NULL, NULL),
                       BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                                          read_report, NULL, &input),
                       BT_GATT_CCC(input_ccc_changed,
                                   BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT),

                       /* Report 2: Feature Report */
                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_REPORT,
                                              BT_GATT_CHRC_READ | BT_GATT_CHRC_WRITE,
                                              BT_GATT_PERM_READ_ENCRYPT | BT_GATT_PERM_WRITE_ENCRYPT,
                                              read_feature_report, write_feature_report, NULL),
                       BT_GATT_DESCRIPTOR(BT_UUID_HIDS_REPORT_REF, BT_GATT_PERM_READ,
                                          read_report, NULL, &feature),

                       BT_GATT_CHARACTERISTIC(BT_UUID_HIDS_CTRL_POINT,
                                              BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                                              BT_GATT_PERM_WRITE,
                                              NULL, write_ctrl_point, &ctrl_point), );

/* HID over GATT sending thread */
void send_report_ble()
{
    int16_t sens_val;

    while (1)
    {
        k_msgq_get(&sensor_msgq, &sens_val, K_FOREVER);

        /* .attrs[6] is the index of the CCC (input report) attribute*/
        bt_gatt_notify(NULL, &hog_svc.attrs[6], &sens_val, sizeof(int16_t));
    }
}

/* BLE report sending thread */
K_THREAD_DEFINE(ble_send_thread, 1024, send_report_ble, NULL, NULL, NULL, SCROLLER_SEND_THREAD_PRIORITY, 0, 0);