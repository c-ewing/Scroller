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
#include <zephyr/kernel.h>

#include <zephyr/settings/settings.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>

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

    /* Enter BLE_CONNECTED state */
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
 */
int scroller_ble_advertise_pairing(const struct bt_le_adv_param *advertising_type)
{
    int err;

    if (k_sem_take(&scroller_ble_advertising, K_NO_WAIT))
    {
        /* Advertising is currently running, bail out */
        LOG_WRN("Advertising already running");
        return -EINPROGRESS;
    }

    err = bt_le_adv_start(BT_LE_ADV_CONN_FAST_1, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err)
    {
        LOG_ERR("Advertising failed to start: %d", err);
        k_sem_give(&scroller_ble_advertising);
        return err;
    }

    /* Enter BLE_ADVERTISING state */
    state = BLE_ADVERTISING;
    if (k_msgq_put(&state_change, &state, K_MSEC(5)))
    {
        LOG_ERR("Failed to send BLE_ADVERTISING");
    }

    LOG_INF("Advertising successfully started");
    return 0;
}

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
