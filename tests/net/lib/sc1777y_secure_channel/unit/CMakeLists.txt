# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sc1777y_secure_channel)

target_sources(app PRIVATE src/test_channel.c src/test_gateway.c src/test_protocol.c)
target_include_directories(app PRIVATE
	${ZEPHYR_BASE}/subsys/net/lib/sc1777y_secure_channel)
