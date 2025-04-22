#ifndef SCROLLER_BLE_H
#define SCROLLER_BLE_H

void scroller_ble_init(int err);

extern struct k_work advertise_work;

#endif /* SCROLLER_BLE_H */