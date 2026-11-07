cmake_minimum_required(VERSION 3.20)

project(ticket_server LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)

find_path(MARIADB_INCLUDE_DIR NAMES mysql.h PATH_SUFFIXES mariadb mariadb/mysql)
find_library(MARIADB_LIBRARY NAMES mariadb)
if(NOT MARIADB_INCLUDE_DIR OR NOT MARIADB_LIBRARY)
    message(FATAL_ERROR
        "MariaDB Connector/C is required. "
        "Run: bash scripts/install_deps.sh")
endif()

add_library(MariaDB::MariaDB UNKNOWN IMPORTED)
set_target_properties(MariaDB::MariaDB PROPERTIES
    IMPORTED_LOCATION "${MARIADB_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${MARIADB_INCLUDE_DIR}"
)

find_path(HIREDIS_INCLUDE_DIR NAMES hiredis/hiredis.h)
find_library(HIREDIS_LIBRARY NAMES hiredis)
if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIBRARY)
    message(FATAL_ERROR
        "hiredis is required. "
        "Run: bash scripts/install_deps.sh")
endif()

add_library(hiredis::hiredis UNKNOWN IMPORTED)
set_target_properties(hiredis::hiredis PROPERTIES
    IMPORTED_LOCATION "${HIREDIS_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${HIREDIS_INCLUDE_DIR}"
)

file(GLOB FRAMEWORK_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/*.cpp")
list(REMOVE_ITEM FRAMEWORK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/framework/main.cpp")

add_executable(ticket_server
    ${FRAMEWORK_SOURCES}
    app/main.cpp
    app/CacheModule.cpp
    app/ConnectionRoster.cpp
    app/OrderEngine.cpp
    app/RedisClient.cpp
    app/TicketService.cpp
    app/Password.cpp
)

target_include_directories(ticket_server PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/framework"
    "${CMAKE_CURRENT_SOURCE_DIR}/app"
)

target_link_libraries(ticket_server PRIVATE
    Threads::Threads
    MariaDB::MariaDB
    OpenSSL::Crypto
    hiredis::hiredis
)

include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-fcoroutines" HAS_FCOROUTINES)
if(HAS_FCOROUTINES)
    target_compile_options(ticket_server PRIVATE -fcoroutines)
endif()
