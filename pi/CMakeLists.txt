# SpoolmanScale for Raspberry Pi
#
# Builds the unchanged firmware sources from ../src on Linux, on top of an
# Arduino compatibility layer (compat/) and Pi drivers (hal/) for the display,
# the touch panel, the NAU7802 scale and the PN532 reader.
#
#   cmake -S pi -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j4
#
# LVGL 8.3.11 and ArduinoJson 7.4.3 are fetched from GitHub on the first run.

cmake_minimum_required(VERSION 3.16)
project(spoolmanscale_pi C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(FW_SRC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
get_filename_component(FW_SRC ${FW_SRC} ABSOLUTE)
set(PI_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# FetchContent_Populate(<name>) is deprecated in CMake 3.30, still fine here:
# the projects are only unpacked, their own CMake files are not used.
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()
include(FetchContent)
FetchContent_Declare(lvgl
  GIT_REPOSITORY https://github.com/lvgl/lvgl.git
  GIT_TAG v8.3.11
  GIT_SHALLOW TRUE)
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.3
  GIT_SHALLOW TRUE)
FetchContent_GetProperties(lvgl)
if(NOT lvgl_POPULATED)
  FetchContent_Populate(lvgl)
endif()
FetchContent_GetProperties(arduinojson)
if(NOT arduinojson_POPULATED)
  FetchContent_Populate(arduinojson)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED IMPORTED_TARGET libcurl)
find_package(Threads REQUIRED)

# ---- the firmware sources -----------------------------------------------------
file(GLOB_RECURSE FW_SOURCES CONFIGURE_DEPENDS
  ${FW_SRC}/*.cpp
  ${FW_SRC}/*.c)

# Files that only make sense on the ESP32 board; each has a Pi version below.
set(FW_REPLACED
  ${FW_SRC}/main.cpp
  ${FW_SRC}/hardware/display.cpp
  ${FW_SRC}/hardware/nfc.cpp
  ${FW_SRC}/hardware/scale.cpp
  ${FW_SRC}/services/ble_service.cpp
  ${FW_SRC}/services/backend_http.cpp
  ${FW_SRC}/services/wifi_roam.cpp
  ${FW_SRC}/services/improv_serial.cpp
)
list(REMOVE_ITEM FW_SOURCES ${FW_REPLACED})

file(GLOB PI_SOURCES CONFIGURE_DEPENDS
  ${PI_DIR}/compat/*.cpp
  ${PI_DIR}/hal/*.cpp
  ${PI_DIR}/override/*.cpp
  ${PI_DIR}/main_pi.cpp)

# ---- LVGL ---------------------------------------------------------------------
file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS ${lvgl_SOURCE_DIR}/src/*.c)
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC ${lvgl_SOURCE_DIR} ${lvgl_SOURCE_DIR}/src ${lvgl_SOURCE_DIR}/.. ${FW_SRC} ${PI_DIR}/compat)
target_compile_definitions(lvgl PUBLIC LV_CONF_PATH=${FW_SRC}/lv_conf.h SPOOLMANSCALE_PI=1)
target_compile_options(lvgl PRIVATE -O2 -w)

# ---- the program --------------------------------------------------------------
add_executable(spoolmanscale ${FW_SOURCES} ${PI_SOURCES})
target_include_directories(spoolmanscale PRIVATE
  ${PI_DIR}/compat
  ${PI_DIR}
  ${FW_SRC}
  ${arduinojson_SOURCE_DIR}/src)
target_compile_definitions(spoolmanscale PRIVATE
  SPOOLMANSCALE_PI=1
  ARDUINO=10819
  ARDUINOJSON_ENABLE_PROGMEM=0
  FW_PLATFORM_PI=1
  FW_PRODUCT_NAME_1="Filament"
  FW_PRODUCT_NAME_2="Lager")
target_compile_options(spoolmanscale PRIVATE
  -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
  $<$<COMPILE_LANGUAGE:CXX>:-Wno-deprecated-declarations>)
target_link_libraries(spoolmanscale PRIVATE lvgl PkgConfig::CURL Threads::Threads)

install(TARGETS spoolmanscale RUNTIME DESTINATION bin)
