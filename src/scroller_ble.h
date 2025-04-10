#ifndef SCROLLER_BLE_H
#define SCROLLER_BLE_H

void bt_ready(int err);

extern struct bt_conn_auth_cb auth_cb_display;

#endif /* SCROLLER_BLE_H */