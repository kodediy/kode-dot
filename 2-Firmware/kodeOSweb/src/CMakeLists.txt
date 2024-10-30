# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

# Añade el directorio de componentes
set(EXTRA_COMPONENT_DIRS "${CMAKE_SOURCE_DIR}/src/components")

# Recolecta los archivos de 'src' como antes
FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.c)

idf_component_register(SRCS ${app_sources})

include_directories("components/wifi")
include_directories("components/web_server")
include_directories("components/handlers")
include_directories("components/boot_config")

