# EconetWiFi
# Copyright (c) 2025 Paul G. Banks <https://paulbanks.org/projects/econet>
# 
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
# 
# See the LICENSE file in the project root for full license information.

idf_component_register(SRCS 
    "aun_bridge.c" 
    "econet.c" 
    "econet_tx.c" 
    "econet_rx.c" 
    "http.c"
    "http_ws.c"
    "logging.c"
    "wifi.c"
    "config.c"
    "main.c" 
    INCLUDE_DIRS ".")

spiffs_create_partition_image(storage ../fsroot FLASH_IN_PROJECT)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-format-truncation)