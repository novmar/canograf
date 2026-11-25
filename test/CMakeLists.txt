# Hostitelske testy transportni a diagnosticke vrstvy. Zadna deska,
# zadna LVGL, zadne stahovani - jen libc a par souboru z src/. Proto to
# stoji vedle sim/ a ne v nem: sim si pres FetchContent tahne celou
# LVGL a na otestovani stavovych automatu neni potreba.
#
#   cmake -S test -B test/build
#   cmake --build test/build
#   ./test/build/isotp_test        # nebo: ctest --test-dir test/build
#   ./test/build/uds_test
#   ./test/build/elm_test
#
# Navratovy kod 0 = vse proslo.

cmake_minimum_required(VERSION 3.16)
project(canograf_tests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../src")
set(CAN_DIR "${SRC_DIR}/can")
set(DIAG_DIR "${SRC_DIR}/diag")

enable_testing()

add_executable(isotp_test
  "${CMAKE_CURRENT_SOURCE_DIR}/isotp_test.c"
  "${CAN_DIR}/isotp.c"
)
target_include_directories(isotp_test PRIVATE "${CAN_DIR}")
target_compile_options(isotp_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME isotp COMMAND isotp_test)

# Diagnosticka vrstva nad ISO-TP: UDS, stary protokol Renault a detekce
# pripojeni k vozidlu. Prekladaji se se stejnym "-I src" jako firmware,
# takze se includuje "can/isotp.h" a "diag/uds.h".
add_executable(uds_test
  "${CMAKE_CURRENT_SOURCE_DIR}/uds_test.c"
  "${DIAG_DIR}/uds.c"
  "${DIAG_DIR}/legacy.c"
  "${DIAG_DIR}/link.c"
  "${CAN_DIR}/isotp.c"
)
target_include_directories(uds_test PRIVATE "${SRC_DIR}")
target_compile_options(uds_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME uds COMMAND uds_test)

# Emulace ELM327 pro cizi diagnosticke nastroje. Prekladaji se jen
# elm.c a isotp.c - soket ani UI tu nejsou, protoze cely automat
# prikazu je cista logika s casem v parametru. CANOGRAF_MOD_ELM327 se
# schvalne NEdefinuje: hostitelsky preklad tim vynecha sitovou cast
# souboru a testuje presne to, co se da testovat bez desky.
add_executable(elm_test
  "${CMAKE_CURRENT_SOURCE_DIR}/elm_test.c"
  "${SRC_DIR}/net/elm.c"
  "${CAN_DIR}/isotp.c"
)
target_include_directories(elm_test PRIVATE "${SRC_DIR}")
target_compile_options(elm_test PRIVATE -Wall -Wextra -Werror)

add_test(NAME elm COMMAND elm_test)
