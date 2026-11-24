# Hostitelske testy transportni vrstvy. Zadna deska, zadna LVGL, zadne
# stahovani - jen libc a dva soubory z src/can/. Proto to stoji vedle
# sim/ a ne v nem: sim si pres FetchContent tahne celou LVGL a na
# otestovani stavoveho automatu ISO-TP neni potreba.
#
#   cmake -S test -B test/build
#   cmake --build test/build
#   ./test/build/isotp_test        # nebo: ctest --test-dir test/build
#
# Navratovy kod 0 = vse proslo.

cmake_minimum_required(VERSION 3.16)
project(canograf_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CAN_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src/can")

enable_testing()

add_executable(isotp_test
  "${CMAKE_CURRENT_SOURCE_DIR}/isotp_test.c"
  "${CAN_DIR}/isotp.c"
)
target_include_directories(isotp_test PRIVATE "${CAN_DIR}")
target_compile_options(isotp_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME isotp COMMAND isotp_test)
