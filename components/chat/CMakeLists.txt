# idf_component_register(
#     SRCS "chat_ble_client.c" "chat_ble_server.c" "chat_ble_dynamic.c" "chat.c"
#     INCLUDE_DIRS "."
#     REQUIRES esp_driver_usb_serial_jtag
# )

idf_component_register(
    SRCS
        "chat.c"
        "chat_ble_dynamic.c"
        "chat_ble_server.c"
        "chat_ble_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        nvs_flash
        esp_system
        esp_driver_usb_serial_jtag
)

# idf_component_register(
#     SRCS
#         "chat.c"
#         "chat_ble_dynamic.c"
#         "chat_ble_server.c"
#         "chat_ble_client.c"
#     INCLUDE_DIRS "."
#     REQUIRES
#         bt
#         driver
#         nvs_flash
#         esp_system
#         esp_driver_usb_serial_jtag
# )


