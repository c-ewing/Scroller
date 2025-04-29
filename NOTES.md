
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