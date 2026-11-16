cmake_minimum_required(VERSION 3.16)
project(nvdla-conv-simulator CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Find SystemC ──────────────────────────────────────────────────────────────
# Reads $SYSTEMC_HOME from env (set to /opt/systemc by Dockerfile).
# Falls back to /opt/systemc if the env var is unset.
if(DEFINED ENV{SYSTEMC_HOME})
    set(SYSTEMC_HOME "$ENV{SYSTEMC_HOME}")
else()
    set(SYSTEMC_HOME "/opt/systemc")
endif()

find_path(SYSTEMC_INCLUDE_DIR systemc.h
    PATHS "${SYSTEMC_HOME}/include"
    NO_DEFAULT_PATH
    REQUIRED)

# SystemC cmake installs the static lib; try lib/ and lib-linux64/ both.
find_library(SYSTEMC_LIBRARY systemc
    PATHS
        "${SYSTEMC_HOME}/lib"
        "${SYSTEMC_HOME}/lib-linux64"
        "${SYSTEMC_HOME}/lib64"
    NO_DEFAULT_PATH
    REQUIRED)

message(STATUS "SystemC include : ${SYSTEMC_INCLUDE_DIR}")
message(STATUS "SystemC library : ${SYSTEMC_LIBRARY}")

# ── CMAC sources ─────────────────────────────────────────────────────────────
set(CMAC_SRCS src/cmac.cpp)

# ── tb_cmac ───────────────────────────────────────────────────────────────────
add_executable(tb_cmac
    tests/tb_cmac.cpp
    ${CMAC_SRCS})

target_include_directories(tb_cmac PRIVATE
    "${SYSTEMC_INCLUDE_DIR}"
    src)

target_link_libraries(tb_cmac PRIVATE
    "${SYSTEMC_LIBRARY}"
    m)

# ── CTest ─────────────────────────────────────────────────────────────────────
enable_testing()
add_test(NAME tb_cmac COMMAND tb_cmac)