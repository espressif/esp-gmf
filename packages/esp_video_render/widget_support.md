# Widget Support

`esp_video_render` currently provides two built-in widget types:

- `esp_vui_text_widget`
- `esp_vui_image_widget`

Widgets are created inside a container, and containers are attached to an overlay. This keeps UI composition aligned with the video rendering pipeline and allows widget updates to participate in dirty-region based redraw.

## Common Behavior

Both widget types work with the overlay/container composition model:

- widgets redraw only when needed
- updates can be merged into container dirty regions
- cached containers can reduce redraw cost
- uncached containers can render directly into the current compose flow

## Image Widget

The image widget is intended for icons, buttons, status indicators, and other bitmap UI assets.

### Supported Features

- Draw raw image buffers through `esp_video_render_img_t`
- Place images at arbitrary positions inside a container
- Reuse one image buffer across multiple widgets
- Transparent-color blending through `esp_vui_image_widget_set_transparent_color()`

### Encoded Image Workflow

To use encoded image assets, decode them first with:

`esp_video_render_decode_image()`

This helper converts an encoded image into a raw frame buffer in the preferred render format.

### Notes

- One image can be reused by multiple image widgets to save resources.
- Transparent-color mode is useful for icon-style assets that use color-key transparency.

## Text Widget

The text widget is intended for labels, titles, playback information, status text, and other dynamic UI content.

### Supported Features

- UTF-8 text rendering
- Font loading from file
- Font loading from memory
- Font resource binding
- Emoji font configuration
- Text color configuration
- Background color with transparent or opaque mode
- Horizontal and vertical alignment
- Scrolling text
- Pause and resume for scrolling text
- Shadow effect
- Overflow mode control

### Typical Use Cases

- Time and progress labels
- File name display
- Status bars and lightweight control panels
- Scrolling titles when text is longer than the widget width

### Notes

- For frequently updated text, an opaque text background can reduce visual artifacts during partial redraw.
- Emoji support depends on the FreeType build configuration below.

## Container-Level Considerations

Widget behavior is also affected by the container that hosts it:

- Cached containers are useful for stable UI that changes infrequently.
- Uncached containers are useful when the UI is highly dynamic or should draw directly in the current frame.
- Container alpha, visibility, and transparent-color settings affect the final composed output.

## FreeType PNG Support for Emoji

To support image emoj support need turn on PNG decoder support for freetype
Can replace the `CmakeLists.txt` with following after downloaded into 'managed_components/espressif__freetype':
```
idf_component_register()
set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
set(FT_DISABLE_PNG OFF CACHE BOOL "" FORCE)
set(FT_DISABLE_ZLIB OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_PNG OFF CACHE BOOL "" FORCE)
set(FT_REQUIRE_ZLIB OFF CACHE BOOL "" FORCE)

set(SKIP_INSTALL_ALL TRUE)
set(BUILD_SHARED_LIBS OFF)

add_subdirectory(freetype output)
target_compile_options(freetype PRIVATE "-Wno-dangling-pointer")

idf_component_get_property(_png_lib espressif__libpng COMPONENT_LIB)
idf_component_get_property(_zlib_lib espressif__zlib COMPONENT_LIB)

if(_png_lib)
target_link_libraries(freetype PRIVATE ${_png_lib})
target_compile_definitions(freetype PRIVATE FT_CONFIG_OPTION_USE_PNG)
endif()

if(_zlib_lib)
target_link_libraries(freetype PRIVATE ${_zlib_lib})
endif()

target_link_libraries(${COMPONENT_LIB} INTERFACE freetype)
```
