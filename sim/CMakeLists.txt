# Hostitelsky build UI - prelozi src/ui.c proti LVGL 8.4.0 a vyrobi
# binarku lv_shot, ktera vyfoti kazdou obrazovku do PNG. Zadny displej,
# zadne SDL, zadny Arduino - jen libc, libm a LVGL.
#
#   cmake -S sim -B sim/build
#   cmake --build sim/build
#   ./sim/build/lv_shot /tmp/shots
#
# LVGL se stahne pres FetchContent. Kdyz uz ho nekde mate (treba
# .pio/libdeps/waveshare43b/lvgl), predejte cestu: -DLVGL_DIR=<cesta>.

cmake_minimum_required(VERSION 3.16)
project(talento_sim C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(FW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")

set(LVGL_DIR "" CACHE PATH "Hotovy strom LVGL 8.4.0; prazdne = stahnout")

if(LVGL_DIR STREQUAL "")
  include(FetchContent)
  FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v8.4.0
    GIT_SHALLOW TRUE
    # LVGL si vlastnim CMakeLists.txt stavi i examples a demos, ktere
    # nepotrebujeme - zdrojaky si posbirame sami, add_subdirectory se
    # preskoci tim, ze SOURCE_SUBDIR nema CMakeLists.txt.
    SOURCE_SUBDIR  env_support
  )
  FetchContent_MakeAvailable(lvgl)
  set(LVGL_DIR "${lvgl_SOURCE_DIR}")
endif()

if(NOT EXISTS "${LVGL_DIR}/lvgl.h")
  message(FATAL_ERROR "LVGL_DIR=${LVGL_DIR} nevypada jako strom LVGL")
endif()

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${LVGL_DIR}/src/*.c")

add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl SYSTEM PUBLIC "${LVGL_DIR}")
# lv_conf.h bereme ten z firmwaru, at je UI na hostiteli stejne
# nakonfigurovana jako na desce
target_include_directories(lvgl PUBLIC "${FW_SRC_DIR}")
target_compile_definitions(lvgl PUBLIC
  LV_CONF_INCLUDE_SIMPLE
  LV_LVGL_H_INCLUDE_SIMPLE
  LV_COLOR_16_SWAP=0
)
target_compile_options(lvgl PRIVATE -w)

# Vygenerovane fonty s diakritikou. Jsou to strojove generovane tabulky,
# takze na ne -Wall -Wextra nepousti smysl.
file(GLOB UI_FONTS CONFIGURE_DEPENDS "${FW_SRC_DIR}/font_ui_*.c")
set_source_files_properties(${UI_FONTS} PROPERTIES COMPILE_OPTIONS "-w")

add_executable(lv_shot
  "${CMAKE_CURRENT_SOURCE_DIR}/lv_shot.c"
  "${FW_SRC_DIR}/ui.c"
  "${FW_SRC_DIR}/i18n.c"
  ${UI_FONTS}
)
target_include_directories(lv_shot PRIVATE
  "${FW_SRC_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
)
target_compile_options(lv_shot PRIVATE -Wall -Wextra)
target_link_libraries(lv_shot PRIVATE lvgl m)
