idf_component_register(SRCS "main.c" "pool_reg.c" "cmd_reg.c" "stream_proc.c"
                    INCLUDE_DIRS "."
                    REQUIRES "bt" "nvs_flash" "console"
                    )
