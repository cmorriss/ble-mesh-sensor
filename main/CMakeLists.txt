set(srcs "mesh_main.c"
        "mesh_misc.c"
        "mesh_sensor.c"
        "mesh_battery_state.c"
        "mesh_node.c"
        "mesh_peer.c"
        "mesh_data_packet.c"
        "mesh_ota_update.c"
        "mesh_wifi_connect.c")
idf_build_get_property(project_dir PROJECT_DIR)
idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "."
        EMBED_TXTFILES ${project_dir}/server_certs/ca_cert.pem)
