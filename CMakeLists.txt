zephyr_library()

if ((NOT CONFIG_ZMK_SPLIT) OR CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
  zephyr_library_sources_ifdef(CONFIG_ZMK_BEHAVIOR_KEY_TURBO src/behavior_key_turbo.c)
  zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
endif()
