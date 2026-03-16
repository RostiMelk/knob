cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(sonos_radio)

# Pack assets/logos/ into the "storage" SPIFFS partition at flash time.
# Files inside assets/logos/ appear at the SPIFFS root, e.g. /spiffs/nrk_p1.png
spiffs_create_partition_image(storage assets/logos FLASH_IN_PROJECT)
