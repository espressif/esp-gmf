set(srcs
    "src/esp_bt_audio_stream.c"
    "src/esp_bt_audio.c"
    "src/esp_bt_audio_host.c"
    "src/bt_audio_ops.c"
    "src/bt_audio_evt_dispatcher.c"
)

set(esp_bt_audio_requires bt esp_driver_i2s esp_driver_gpio)

set(includes "include")
set(private_includes
    "private_inc"
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
    if (CONFIG_BT_PBAC_ENABLED)
        list(APPEND srcs "src/classic/profiles/bt_audio_pbac.c")
    endif()
    list(APPEND private_includes "private_inc/classic")
endif()

if (CONFIG_BT_NIMBLE_ENABLED AND CONFIG_BT_AUDIO AND CONFIG_BT_ISO)
    list(APPEND srcs
         "src/le/bt_audio_le.c"
         "src/le/le_stream/bt_audio_le_stream.c"
         "src/le/profiles/adv/bt_audio_le_adv_builder.c"
         "src/le/profiles/pacs/bt_audio_le_pacs.c")
    list(APPEND private_includes "private_inc/le")
    if (CONFIG_BT_BAP_UNICAST_SERVER)
        list(APPEND srcs "src/le/profiles/bap/bt_audio_le_unicast_server.c")
    endif()
    if (CONFIG_BT_VCP_VOL_REND)
        list(APPEND srcs "src/le/profiles/vcp/bt_audio_le_vcp.c")
    endif()
    if (CONFIG_BT_BAP_BROADCAST_SOURCE)
        list(APPEND srcs "src/le/profiles/bap/bt_audio_le_broadcast_source.c")
    endif()
    if (CONFIG_BT_BAP_BROADCAST_SINK)
        list(APPEND srcs "src/le/profiles/bap/bt_audio_le_broadcast_sink.c")
    endif()
    if (CONFIG_BT_BAP_SCAN_DELEGATOR)
        list(APPEND srcs "src/le/profiles/bap/bt_audio_le_scan_delegator.c")
    endif()
    if (CONFIG_BT_CSIP_SET_MEMBER)
        list(APPEND srcs "src/le/profiles/csip/bt_audio_le_csip.c")
    endif()
    if (CONFIG_BT_MICP_MIC_DEV)
        list(APPEND srcs "src/le/profiles/micp/bt_audio_le_micp.c")
    endif()
    if (CONFIG_BT_MCC)
        list(APPEND srcs "src/le/profiles/mcp/bt_audio_le_mcc.c")
    endif()
    if (CONFIG_BT_TBS_CLIENT)
        list(APPEND srcs "src/le/profiles/ccp/bt_audio_le_ccp.c")
    endif()
    if (CONFIG_SOC_MODEM_SUPPORT_ETM)
        list(APPEND srcs "src/le/playback_sync/bt_audio_le_playback_sync.c")
    endif()
endif()

if (CONFIG_ESP_BT_AUDIO_GMF_IO_SUPPORT)
    list(APPEND srcs "io/esp_gmf_io_bt.c")
    list(APPEND includes "io")
endif()

idf_component_register(SRCS "${srcs}"
    INCLUDE_DIRS "${includes}"
    PRIV_INCLUDE_DIRS "${private_includes}"
    REQUIRES ${esp_bt_audio_requires})
