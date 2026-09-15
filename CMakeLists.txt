# =============================================================================
# OBSIDIAN STICKY PROJECT
# =============================================================================
# ESP-IDF project root for the voice-to-Markdown firmware; the app lives in
# main/, vendor drivers in components/.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(obsidian_sticky)

# The CJK font is not in the repository, so `idf.py flash` writes the font
# partition only after tools/fetch_cjk_font.py has built the subset.
if(EXISTS "${CMAKE_SOURCE_DIR}/build/font_cjk.ttf")
    esptool_py_flash_to_partition(flash "font" "${CMAKE_SOURCE_DIR}/build/font_cjk.ttf")
else()
    message(STATUS "No build/font_cjk.ttf, CJK font partition will not be flashed")
endif()
