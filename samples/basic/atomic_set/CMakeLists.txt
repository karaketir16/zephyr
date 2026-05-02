# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(atomic_set)

target_sources(app PRIVATE src/main.c src/atomic_strex_probe.S)
