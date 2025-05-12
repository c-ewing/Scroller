
# Nordic nRF Connect SDK:
## CAF Configuration files
Need to define the configuration file location in your CMakeLists.txt. This doesn't seem to be mentioned anywhere
in the CAF documentation
```cmake
set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/configuration/\${NORMALIZED_BOARD_TARGET}")

zephyr_include_directories(
  configuration/common
  ${APPLICATION_CONFIG_DIR}
)
```

## CAF sensor module
Sensor events *MUST NOT* be consumed by any listener. The sensor module registers itself as the final listener
and decrements the sensor event count.

## CAF Power manager module
Need to listen to:
APP_EVENT_SUBSCRIBE(MODULE, power_down_event);
APP_EVENT_SUBSCRIBE(MODULE, wake_up_event);

not POWER_EVENT.

# Future research:
- ["the RTC should be used if you need a counter to keep track of time or similar while in sleep mode, but should not be necessary in otherwise."](https://devzone.nordicsemi.com/f/nordic-q-a/118387/nrf52832-low-power-mode-sleep-mode-zephyrsos)