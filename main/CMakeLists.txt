idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    PRIV_REQUIRES esp_timer
                    REQUIRES spi_flash driver
                    )
