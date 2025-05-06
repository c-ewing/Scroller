cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(scroller_mvp)

set(APPLICATION_CONFIG_DIR "${CMAKE_CURRENT_LIST_DIR}/configuration/\${NORMALIZED_BOARD_TARGET}")

zephyr_include_directories(
  configuration/common
  ${APPLICATION_CONFIG_DIR}
)

target_sources(app PRIVATE src/main.c)

# Include application events and configuration headers
zephyr_library_include_directories(
  src/events
  src/modules
)

# Application sources
add_subdirectory(src/events)
add_subdirectory(src/modules)
