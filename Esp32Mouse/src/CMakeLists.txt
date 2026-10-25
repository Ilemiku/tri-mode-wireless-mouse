# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(SRCS ${app_sources}
    REQUIRES esp_hid nvs_flash esp_wifi esp_netif esp_http_server esp_timer
             esp_driver_uart esp_driver_gpio esp_driver_spi espressif__mqtt espressif__cjson
             app_update esp_http_client mbedtls spi_flash
    EMBED_TXTFILES "../.ota/ca.pem")
target_compile_definitions(${COMPONENT_LIB} PRIVATE MOUSE_OTA_TEST_ROLLBACK=${MOUSE_OTA_TEST_ROLLBACK})
