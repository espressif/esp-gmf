#include <stdint.h>

// Minimal stub to satisfy logging helpers in esp_video_render.
const char *esp_gmf_video_get_format_string(uint32_t codec)
{
    (void)codec;
    return "raw";
}
