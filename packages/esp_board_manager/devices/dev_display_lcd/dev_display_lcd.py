# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO., LTD
# SPDX-License-Identifier: LicenseRef-Espressif-Modified-MIT
#
# See LICENSE file for details.

# LCD Display device config parser
VERSION = 'v1.0.0'

from generators.utils.idf_version import get_idf_version

DEV_DISPLAY_LCD_IO_LIST = {
    'dsi': [
        'reset_gpio_num',
    ],
    'spi': [
        'cs_gpio_num',
        'dc_gpio_num',
        'reset_gpio_num',
    ],
    'parlio': [
        'dc_gpio_num',
        'clk_gpio_num',
        'cs_gpio_num',
        'data_gpio_nums',
        'reset_gpio_num',
    ],
    'rgb': [
        'hsync_gpio_num',
        'vsync_gpio_num',
        'de_gpio_num',
        'pclk_gpio_num',
        'disp_gpio_num',
        'data_gpio_nums',
    ],
}

def _iter_peripheral_names(peripherals_list):
    """Yield normalized peripheral names from YAML list items."""
    if not peripherals_list:
        return

    for periph in peripherals_list:
        if isinstance(periph, dict):
            periph_name = periph.get('name', '')
        elif isinstance(periph, str):
            periph_name = periph
        else:
            continue

        if periph_name:
            yield periph_name


def _find_peripheral_name_in_list(device_name: str, peripherals_list, prefix: str, peripherals_dict=None):
    """Find the first peripheral name matching prefix from configured peripherals list."""
    for periph_name in _iter_peripheral_names(peripherals_list):
        if periph_name.startswith(prefix):
            if peripherals_dict is not None and periph_name not in peripherals_dict:
                raise ValueError(f"LCD display device {device_name} references undefined peripheral '{periph_name}'")
            return periph_name
    return None


def _find_peripheral_name_in_dict(peripherals_dict, prefix: str):
    """Fallback lookup from peripherals_dict using a prefix match."""
    if not peripherals_dict:
        return None

    peripheral_names = peripherals_dict.keys() if hasattr(peripherals_dict, 'keys') else peripherals_dict
    for periph_name in peripheral_names:
        if isinstance(periph_name, str) and periph_name.startswith(prefix):
            return periph_name
    return None

def get_includes() -> list:
    """Return list of required include headers for LCD Display device"""
    return [
        'dev_display_lcd.h',
    ]

def parse_dsi_sub_config(full_config: dict = None, peripherals_dict=None) -> dict:
    """Parse DSI sub configuration"""
    # Get basic DSI configuration
    sub_config = full_config.get('config', {})
    reset_gpio_num = sub_config.get('reset_gpio_num', -1)
    reset_active_high = sub_config.get('reset_active_high', 0)

    # Get DBI configuration
    dbi_config = sub_config.get('dbi_config', {})
    dbi_config_parsed = {
        'virtual_channel': dbi_config.get('virtual_channel', 0),
        'lcd_cmd_bits': dbi_config.get('lcd_cmd_bits', 8),
        'lcd_param_bits': dbi_config.get('lcd_param_bits', 8)
    }

    # Get DPI configuration
    dpi_config = sub_config.get('dpi_config', {})

    pixel_format_val = dpi_config.get('pixel_format')
    in_color_format_val = dpi_config.get('in_color_format', 'LCD_COLOR_FMT_RGB565')

    dpi_config_parsed = {
        'virtual_channel': dpi_config.get('virtual_channel', 0),
        'dpi_clk_src': dpi_config.get('dpi_clk_src', 'MIPI_DSI_DPI_CLK_SRC_DEFAULT'),
        'dpi_clock_freq_mhz': dpi_config.get('dpi_clock_freq_mhz', 48),
        'in_color_format': in_color_format_val,
        'out_color_format': dpi_config.get('out_color_format', 'LCD_COLOR_FMT_RGB565'),
        'num_fbs': dpi_config.get('num_fbs', 1),
        'flags': {
            'disable_lp': dpi_config.get('flags', {}).get('disable_lp', False)
        }
    }
    use_dma2d_flag = dpi_config.get('flags', {}).get('use_dma2d', False)

    idf_major = get_idf_version()[0]
    if idf_major < 6:
        # v5: include legacy pixel_format and use_dma2d struct fields
        dpi_config_parsed['pixel_format'] = pixel_format_val if pixel_format_val else 'LCD_COLOR_PIXEL_FORMAT_RGB565'
        dpi_config_parsed['flags']['use_dma2d'] = use_dma2d_flag

    # Get video timing configuration
    video_timing = dpi_config.get('video_timing', {})
    dpi_config_parsed['video_timing'] = {
        'h_size': video_timing.get('h_size', 1024),
        'v_size': video_timing.get('v_size', 600),
        'hsync_back_porch': video_timing.get('hsync_back_porch', 120),
        'hsync_pulse_width': video_timing.get('hsync_pulse_width', 10),
        'hsync_front_porch': video_timing.get('hsync_front_porch', 120),
        'vsync_back_porch': video_timing.get('vsync_back_porch', 20),
        'vsync_pulse_width': video_timing.get('vsync_pulse_width', 1),
        'vsync_front_porch': video_timing.get('vsync_front_porch', 20)
    }

    # Get DSI / LDO peripheral names
    device_name = full_config.get('name')
    dsi_bus_name = None
    ldo_name = None
    peripherals_list = None

    # First try to get peripherals from config (for backward compatibility)
    peripherals_list = full_config.get('peripherals')
    if peripherals_list is None or len(peripherals_list) == 0:
        peripherals_list = sub_config.get('peripherals')

    if peripherals_list:
        dsi_bus_name = _find_peripheral_name_in_list(device_name, peripherals_list, 'dsi', peripherals_dict)
        ldo_name = _find_peripheral_name_in_list(device_name, peripherals_list, 'ldo', peripherals_dict)

    # Fallback to peripherals_dict if not found in config
    if not dsi_bus_name and peripherals_dict:
        dsi_bus_name = _find_peripheral_name_in_dict(peripherals_dict, 'dsi')

    if not ldo_name and peripherals_dict:
        ldo_name = _find_peripheral_name_in_dict(peripherals_dict, 'ldo')

    if not dsi_bus_name:
        raise ValueError(f'LCD display device {device_name} require valid DSI peripherals.')

    if not ldo_name:
        raise ValueError(f'LCD display device {device_name} require valid LDO peripherals.')

    ret_dict = {
        'dsi_name': dsi_bus_name,
        'ldo_name': ldo_name,
        'reset_gpio_num': reset_gpio_num,
        'reset_active_high': reset_active_high,
        'dbi_config': dbi_config_parsed,
        'dpi_config': dpi_config_parsed
    }

    if idf_major >= 6:
        ret_dict['use_dma2d'] = use_dma2d_flag

    return ret_dict

def parse_spi_sub_config(full_config: dict = None, peripherals_dict=None) -> dict:
    """Parse SPI sub configuration"""
    # Get basic SPI configuration
    sub_config = full_config.get('config', {})
    # Get IO SPI configuration
    io_spi_config = sub_config.get('io_spi_config', {})
    io_spi_config_parsed = {
        'cs_gpio_num': io_spi_config.get('cs_gpio_num', -1),
        'dc_gpio_num': io_spi_config.get('dc_gpio_num', -1),
        'spi_mode': io_spi_config.get('spi_mode', 0),
        'pclk_hz': io_spi_config.get('pclk_hz', 40000000),
        'trans_queue_depth': io_spi_config.get('trans_queue_depth', 2),
        'lcd_cmd_bits': io_spi_config.get('lcd_cmd_bits', 8),
        'lcd_param_bits': io_spi_config.get('lcd_param_bits', 8),
        'cs_ena_pretrans': io_spi_config.get('cs_ena_pretrans', 0),
        'cs_ena_posttrans': io_spi_config.get('cs_ena_posttrans', 0),
        'flags': {
            'dc_high_on_cmd': io_spi_config.get('flags', {}).get('dc_high_on_cmd', False),
            'dc_low_on_data': io_spi_config.get('flags', {}).get('dc_low_on_data', False),
            'dc_low_on_param': io_spi_config.get('flags', {}).get('dc_low_on_param', False),
            'octal_mode': io_spi_config.get('flags', {}).get('octal_mode', False),
            'quad_mode': io_spi_config.get('flags', {}).get('quad_mode', False),
            'sio_mode': io_spi_config.get('flags', {}).get('sio_mode', False),
            'lsb_first': io_spi_config.get('flags', {}).get('lsb_first', False),
            'cs_high_active': io_spi_config.get('flags', {}).get('cs_high_active', False)
        }
    }

    panel_config_parsed = _parse_lcd_panel_dev_config_dict(sub_config)

    # Get SPI bus name from peripherals
    device_name = full_config.get('name')
    spi_bus_name = None
    peripherals_list = None
    peripherals_list = full_config.get('peripherals')
    if peripherals_list is None or len(peripherals_list) == 0:
        peripherals_list = sub_config.get('peripherals')
    if peripherals_list is None or len(peripherals_list) == 0:
        # Try to get peripherals from nested io_spi_config
        peripherals_list = io_spi_config.get('peripherals')

    if peripherals_list:
        spi_bus_name = _find_peripheral_name_in_list(device_name, peripherals_list, 'spi', peripherals_dict)

    # Fallback to peripherals_dict if not found in config
    if not spi_bus_name and peripherals_dict:
        spi_bus_name = _find_peripheral_name_in_dict(peripherals_dict, 'spi')

    if not spi_bus_name:
        raise ValueError(f'LCD display device {device_name} require valid SPI peripherals.')

    return {
        'spi_name': spi_bus_name,
        'panel_config': panel_config_parsed,
        'io_spi_config': io_spi_config_parsed,
    }

def _parse_lcd_panel_dev_config_dict(sub_config: dict) -> dict:
    """Shared esp_lcd_panel_dev_config_t-style block (SPI / PARLIO)."""
    lcd_panel_config = sub_config.get('lcd_panel_config', {})
    return {
        'reset_gpio_num': lcd_panel_config.get('reset_gpio_num', -1),
        'rgb_ele_order': lcd_panel_config.get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB'),
        'data_endian': lcd_panel_config.get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG'),
        'bits_per_pixel': lcd_panel_config.get('bits_per_pixel', 16),
        'flags': {
            'reset_active_high': lcd_panel_config.get('flags', {}).get('reset_active_high', False)
        },
        'vendor_config': lcd_panel_config.get('vendor_config', '')
    }

def parse_parlio_sub_config(full_config: dict = None, peripherals_dict=None) -> dict:
    """Parse PARLIO (esp_lcd_io_parl) sub configuration; no SPI peripheral reference."""
    sub_config = full_config.get('config', {})
    x_max = int(sub_config.get('x_max', 320))
    io = sub_config.get('io_parl_config', {})

    data_nums = io.get('data_gpio_nums')
    if data_nums is not None:
        data_nums = [int(x) for x in data_nums]
    else:
        data_nums = [int(io.get(f'data_gpio_{i}', -1)) for i in range(8)]

    while len(data_nums) < 8:
        data_nums.append(-1)
    data_nums = data_nums[:8]

    default_max_transfer = x_max * 20 * 2
    max_transfer = int(io.get('max_transfer_bytes', default_max_transfer))
    if max_transfer <= 0:
        max_transfer = default_max_transfer

    io_parl_parsed = {
        'dc_gpio_num': int(io.get('dc_gpio_num', -1)),
        'clk_gpio_num': int(io.get('clk_gpio_num', -1)),
        'cs_gpio_num': int(io.get('cs_gpio_num', -1)),
        'data_gpio_nums': data_nums,
        'data_width': int(io.get('data_width', 1)),
        'pclk_hz': int(io.get('pclk_hz', 40000000)),
        'clk_src': io.get('clk_src', 'PARLIO_CLK_SRC_DEFAULT'),
        'max_transfer_bytes': max_transfer,
        'dma_burst_size': int(io.get('dma_burst_size', 32)),
        'trans_queue_depth': int(io.get('trans_queue_depth', 10)),
        'lcd_cmd_bits': int(io.get('lcd_cmd_bits', 8)),
        'lcd_param_bits': int(io.get('lcd_param_bits', 8)),
        'dc_levels': {
            'dc_cmd_level': int(io.get('dc_levels', {}).get('dc_cmd_level', 0)),
            'dc_data_level': int(io.get('dc_levels', {}).get('dc_data_level', 1)),
        },
        'flags': {
            'cs_active_high': io.get('flags', {}).get('cs_active_high', False),
        },
    }

    return {
        'io_parl': io_parl_parsed,
        'panel_config': _parse_lcd_panel_dev_config_dict(sub_config),
    }

def _rgb_bits_per_pixel_from_color_format(color_format: str, default: int = 16) -> int:
    """Best-effort RGB bits-per-pixel from ESP-IDF LCD color format constants."""
    if not isinstance(color_format, str):
        return default
    if 'RGB888' in color_format:
        return 24
    if 'RGB666' in color_format:
        return 18
    if 'RGB565' in color_format:
        return 16
    return default

def _parse_rgb_data_gpio_nums(rgb_config: dict) -> list:
    data_nums = rgb_config.get('data_gpio_nums')
    if data_nums is not None:
        return list(data_nums)
    return [rgb_config.get(f'data_gpio_{i}', -1) for i in range(int(rgb_config.get('data_width', 16)))]

def parse_rgb_sub_config(full_config: dict = None, peripherals_dict=None) -> dict:
    """Parse RGB (esp_lcd_new_rgb_panel) sub configuration."""
    sub_config = full_config.get('config', {})
    rgb_config = sub_config.get('rgb_panel_config', {})
    timings = rgb_config.get('timings', {})
    timing_flags = timings.get('flags', {})
    flags = rgb_config.get('flags', {})
    idf_major = get_idf_version()[0]

    user_fbs_func = rgb_config.get('user_fbs_func', sub_config.get('user_fbs_func', ''))
    if user_fbs_func and idf_major < 6:
        raise ValueError('RGB user_fbs_func requires ESP-IDF v6.0 or later')

    in_color_format = rgb_config.get('in_color_format', 'LCD_COLOR_FMT_RGB565')
    out_color_format = rgb_config.get('out_color_format', in_color_format)
    bits_per_pixel = int(rgb_config.get(
        'bits_per_pixel',
        sub_config.get('bits_per_pixel', _rgb_bits_per_pixel_from_color_format(in_color_format)),
    ))

    panel_config = {
        'clk_src': rgb_config.get('clk_src', 'LCD_CLK_SRC_DEFAULT'),
        'timings': {
            'pclk_hz': int(timings.get('pclk_hz', rgb_config.get('pclk_hz', 18000000))),
            'h_res': int(timings.get('h_res', sub_config.get('x_max', 800))),
            'v_res': int(timings.get('v_res', sub_config.get('y_max', 480))),
            'hsync_pulse_width': int(timings.get('hsync_pulse_width', 1)),
            'hsync_back_porch': int(timings.get('hsync_back_porch', 40)),
            'hsync_front_porch': int(timings.get('hsync_front_porch', 20)),
            'vsync_pulse_width': int(timings.get('vsync_pulse_width', 1)),
            'vsync_back_porch': int(timings.get('vsync_back_porch', 10)),
            'vsync_front_porch': int(timings.get('vsync_front_porch', 5)),
            'flags': {
                'hsync_idle_low': timing_flags.get('hsync_idle_low', False),
                'vsync_idle_low': timing_flags.get('vsync_idle_low', False),
                'de_idle_high': timing_flags.get('de_idle_high', False),
                'pclk_active_neg': timing_flags.get('pclk_active_neg', True),
                'pclk_idle_high': timing_flags.get('pclk_idle_high', False),
            },
        },
        'data_width': int(rgb_config.get('data_width', 16)),
        'num_fbs': int(rgb_config.get('num_fbs', 1)),
        'bounce_buffer_size_px': int(rgb_config.get('bounce_buffer_size_px', 0)),
        'dma_burst_size': int(rgb_config.get('dma_burst_size', 64)),
        'hsync_gpio_num': rgb_config.get('hsync_gpio_num', -1),
        'vsync_gpio_num': rgb_config.get('vsync_gpio_num', -1),
        'de_gpio_num': rgb_config.get('de_gpio_num', -1),
        'pclk_gpio_num': rgb_config.get('pclk_gpio_num', -1),
        'disp_gpio_num': rgb_config.get('disp_gpio_num', -1),
        'data_gpio_nums': _parse_rgb_data_gpio_nums(rgb_config),
        'flags': {
            'disp_active_low': flags.get('disp_active_low', False),
            'refresh_on_demand': flags.get('refresh_on_demand', False),
            'fb_in_psram': flags.get('fb_in_psram', True),
            'double_fb': flags.get('double_fb', False),
            'no_fb': flags.get('no_fb', False),
            'bb_invalidate_cache': flags.get('bb_invalidate_cache', False),
        },
    }

    if idf_major >= 6:
        panel_config['in_color_format'] = in_color_format
        panel_config['out_color_format'] = out_color_format
    else:
        panel_config['bits_per_pixel'] = bits_per_pixel

    return {
        'user_fbs_func': user_fbs_func,
        'panel_config': panel_config,
    }

def parse(name: str, full_config: dict, peripherals_dict=None) -> dict:
    """Parse LCD Display device configuration from YAML"""
    sub_type = full_config.get('sub_type')
    chip = full_config.get('chip', 'generic_lcd')

    # sub_type is mandatory
    if not sub_type:
        raise ValueError(f"LCD Display device '{name}' is missing required 'sub_type' field")

    # Validate sub_type value
    if sub_type not in ['dsi', 'spi', 'parlio', 'rgb']:
        raise ValueError(f"LCD Display device '{name}' has invalid 'sub_type' value '{sub_type}'")

    # Parse sub configuration based on sub_type and extract common parameters
    if sub_type == 'dsi':
        sub_cfg = parse_dsi_sub_config(full_config, peripherals_dict)
        # Set the dsi member of the union
        sub_cfg_union = {'dsi': sub_cfg}
        # Extract common parameters from DSI sub config
        lcd_width = sub_cfg.get('dpi_config', {}).get('video_timing', {}).get('h_size', 1024)
        lcd_height = sub_cfg.get('dpi_config', {}).get('video_timing', {}).get('v_size', 600)
        swap_xy = False  # DSI typically doesn't use swap_xy
        mirror_x = False  # DSI typically doesn't use mirror_x
        mirror_y = False  # DSI typically doesn't use mirror_y
        need_reset = full_config.get('config').get('need_reset', True)
        invert_color = full_config.get('config').get('invert_color', False)
        rgb_ele_order = full_config.get('config').get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB')
        data_endian = full_config.get('config').get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG')
        bits_per_pixel = full_config.get('config').get('bits_per_pixel', 24)
    elif sub_type == 'spi':
        sub_cfg = parse_spi_sub_config(full_config, peripherals_dict)
        # Set the spi member of the union
        sub_cfg_union = {'spi': sub_cfg}
        # Extract common parameters from SPI sub config
        lcd_width = full_config.get('config').get('x_max', 320)
        lcd_height = full_config.get('config').get('y_max', 240)
        swap_xy = full_config.get('config').get('swap_xy', False)
        mirror_x = full_config.get('config').get('mirror_x', False)
        mirror_y = full_config.get('config').get('mirror_y', False)
        need_reset = full_config.get('config').get('need_reset', True)
        invert_color = full_config.get('config').get('invert_color', False)
        rgb_ele_order = sub_cfg.get('panel_config', {}).get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB')
        data_endian = sub_cfg.get('panel_config', {}).get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG')
        bits_per_pixel = sub_cfg.get('panel_config', {}).get('bits_per_pixel', 16)
    elif sub_type == 'parlio':
        sub_cfg = parse_parlio_sub_config(full_config, peripherals_dict)
        sub_cfg_union = {'parlio': sub_cfg}
        lcd_width = full_config.get('config').get('x_max', 320)
        lcd_height = full_config.get('config').get('y_max', 240)
        swap_xy = full_config.get('config').get('swap_xy', False)
        mirror_x = full_config.get('config').get('mirror_x', False)
        mirror_y = full_config.get('config').get('mirror_y', False)
        need_reset = full_config.get('config').get('need_reset', True)
        invert_color = full_config.get('config').get('invert_color', False)
        rgb_ele_order = sub_cfg.get('panel_config', {}).get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB')
        data_endian = sub_cfg.get('panel_config', {}).get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG')
        bits_per_pixel = sub_cfg.get('panel_config', {}).get('bits_per_pixel', 16)
    elif sub_type == 'rgb':
        sub_cfg = parse_rgb_sub_config(full_config, peripherals_dict)
        sub_cfg_union = {'rgb': sub_cfg}
        panel_config = sub_cfg.get('panel_config', {})
        timings = panel_config.get('timings', {})
        lcd_width = timings.get('h_res', 800)
        lcd_height = timings.get('v_res', 480)
        swap_xy = full_config.get('config').get('swap_xy', False)
        mirror_x = full_config.get('config').get('mirror_x', False)
        mirror_y = full_config.get('config').get('mirror_y', False)
        need_reset = full_config.get('config').get('need_reset', True)
        invert_color = full_config.get('config').get('invert_color', False)
        rgb_ele_order = full_config.get('config').get('rgb_ele_order', 'LCD_RGB_ELEMENT_ORDER_RGB')
        data_endian = full_config.get('config').get('data_endian', 'LCD_RGB_DATA_ENDIAN_BIG')
        bits_per_pixel = full_config.get('config').get(
            'bits_per_pixel',
            panel_config.get('bits_per_pixel',
                             _rgb_bits_per_pixel_from_color_format(panel_config.get('in_color_format'), 16)),
        )
    else:
        raise ValueError(f'Unsupported sub_type: {sub_type}')

    return {
        'struct_type': 'dev_display_lcd_config_t',
        'struct_var': f'{name}_cfg',
        'struct_init': {
            'name': name,
            'chip': chip,
            'sub_type': sub_type,
            'lcd_width': lcd_width,
            'lcd_height': lcd_height,
            'swap_xy': swap_xy,
            'mirror_x': mirror_x,
            'mirror_y': mirror_y,
            'need_reset': need_reset,
            'invert_color': invert_color,
            'rgb_ele_order': rgb_ele_order,
            'data_endian': data_endian,
            'bits_per_pixel': bits_per_pixel,
            'sub_cfg': sub_cfg_union
        }
    }
