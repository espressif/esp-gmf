set(srcs
    "src/esp_bt_audio_stream.c"
    "src/esp_bt_audio.c"
    "src/esp_bt_audio_host.c"
    "src/bt_audio_ops.c"
    "src/bt_audio_evt_dispatcher.c"
)

set(includes "include")
set(private_includes
    "priv_include"
    "priv_include/classic"
)

if (CONFIG_BT_CLASSIC_ENABLED)
    list(APPEND srcs "src/classic/bt_audio_classic.c"
                     "src/classic/classic_stream/bt_audio_classic_stream.c")
    list(APPEND private_includes "src/classic"
                                 "src/classic/classic_stream"
                                 "src/classic/profiles")
    if (CONFIG_BT_A2DP_ENABLE)
        list(APPEND srcs "src/classic/profiles/bt_audio_a2dp_src.c"
                         "src/classic/profiles/bt_audio_a2dp_sink.c")
    endif()
    if (CONFIG_BT_HFP_ENABLE)
        list(APPEND srcs "src/classic/profiles/bt_audio_hfp_hf.c")
    endif()
    if (CONFIG_BT_AVRCP_ENABLED)
        list(APPEND srcs "src/classic/profiles/bt_audio_avrcp_ct.c"
                         "src/classic/profiles/bt_audio_avrcp_tg.c")
    endif()
endif()

if (CONFIG_ESP_BT_AUDIO_GMF_IO_SUPPORT)
    list(APPEND srcs "io/esp_gmf_io_bt.c")
    list(APPEND includes "io")
endif()

idf_component_register(SRCS "${srcs}"
    INCLUDE_DIRS "${includes}"
    PRIV_INCLUDE_DIRS "${private_includes}"
    REQUIRES bt)
