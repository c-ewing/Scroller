/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/types.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/settings/settings.h>

#include "scroller_hog.h"
#include "scroller_config.h"

LOG_MODULE_REGISTER(scroller_ble, LOG_LEVEL_DBG);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA_BYTES(BT_DATA_UUID16_ALL,
                  BT_UUID_16_ENCODE(BT_UUID_HIDS_VAL),
                  BT_UUID_16_ENCODE(BT_UUID_BAS_VAL)),
};

static const struct bt_data sd[] = {
    BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME, sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

/* Prevent restarting advertising if it is already started */
K_SEM_DEFINE(scroller_ble_advertising, 1, 1);

/* Current BLE state */
static int32_t state;

static void connected(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err)
    {
        LOG_ERR("Failed to connect to %s, err 0x%02x %s", addr,
                err, bt_hci_err_to_str(err));
        return;
    }

    LOG_INF("Connected %s", addr);

    if (bt_conn_set_security(conn, BT_SECURITY_L2))
    {
        LOG_WRN("Failed to set security");
    }

    /* Enter BLE_CONNECTED state and clear advertising sem */
    state = BLE_CONNECTED;
    if (k_msgq_put(&state_change, &state, K_MSEC(5)))
    {
        LOG_ERR("Failed to send BLE_CONNECTED");
    }
    k_sem_give(&scroller_ble_advertising);
}

static void disconnected(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    LOG_INF("Disconnected from %s, reason 0x%02x %s", addr,
            reason, bt_hci_err_to_str(reason));

    /* Enter BLE_DISCONNECTED state */
    state = BLE_DISCONNECTED;
    if (k_msgq_put(&state_change, &state, K_MSEC(5)))
    {
        LOG_ERR("Failed to send BLE_DISCONNECTED");
    }
}

static void security_changed(struct bt_conn *conn, bt_security_t level,
                             enum bt_security_err err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (!err)
    {
        LOG_INF("Security changed: %s level %u", addr, level);
    }
    else
    {
        LOG_WRN("Security failed: %s level %u err %s(%d)", addr, level,
                bt_security_err_to_str(err), err);
    }
}

BT_CONN_CB_DEFINE(conn_callbacks) = {
    .connected = connected,
    .disconnected = disconnected,
    .security_changed = security_changed,
};

/* Advertise with the given type:
 * BT_LE_ADV_CONN_FAST_1 --> Pairing
 * TODO: Low power pairing mode? Longer interval to decrease battery usage.
 * const struct bt_le_adv_param *advertising_type
 */
void advertise_pairing(struct k_work *work)
{
    int err;

    if (k_sem_take(&scroller_ble_advertising, K_NO_WAIT))
    {
        /* Advertising is currently running, bail out */
        LOG_WRN("Advertising already running");
        return;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start: %d", err);
        k_sem_give(&scroller_ble_advertising);
        return;
    }

    /* Enter BLE_ADVERTISING state */
    state = BLE_ADVERTISING;
    if (k_msgq_put(&state_change, &state, K_MSEC(5)))
    {
        LOG_ERR("Failed to send BLE_ADVERTISING");
    }

    LOG_INF("Advertising successfully started");
    return;
}
K_WORK_DEFINE(advertise_work, advertise_pairing);

void scroller_ble_init(int err)
{
    if (err)
    {
        LOG_ERR("Bluetooth init failed: %d", err);
        return;
    }

    LOG_INF("Bluetooth initialized");

    if (IS_ENABLED(CONFIG_SETTINGS))
    {
        settings_load();
    }

    /* Enter BLE_CONNECTED state */
    state = BLE_DISCONNECTED;
    if (k_msgq_put(&state_change, &state, K_MSEC(5)))
    {
        LOG_ERR("Failed to send BLE_DISCONNECTED");
    }
}

// FIXME: Will need white/black listing of devices
// for a temporary period to allow others to connect? May need connnection pool info? --> Nordic course
static void input_callback(struct input_event *event, void *userdata)
{
    static bool pairing_button = true;

    /* Key codes are defined in the overlay file */
    switch (event->code)
    {
    case INPUT_KEY_A:
        LOG_INF("Short Press Key 1");
        break;
    case INPUT_KEY_X:
        if (pairing_button)
        {
            LOG_INF("Long press down key 1");
            // FIXME: trigger advertising
        }
        else
        {
            LOG_INF("Long press up key 1");
        }

        pairing_button = !pairing_button;
        break;
    }
}

INPUT_CALLBACK_DEFINE(NULL, input_callback, NULL);
