# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file Copyright.txt or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION 3.5)

file(MAKE_DIRECTORY
  "C:/Users/Will/esp/v5.2.2/esp-idf/components/bootloader/subproject"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/tmp"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/src/bootloader-stamp"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/src"
  "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/src/bootloader-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/src/bootloader-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "C:/Users/Will/Desktop/DFS Work/Firmware/Auto_DEF/Auto_DEF_slave/build/bootloader-prefix/src/bootloader-stamp${cfgdir}") # cfgdir has leading slash
endif()
