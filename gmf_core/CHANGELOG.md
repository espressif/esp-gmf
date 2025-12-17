# Changelog

## v0.7.9

### Bug Fixes

- Fixed block data_bus abort return miss unlock
- Fixed pbuf data_bus reset miss buffer and count process
- Fixed unit test for `esp_gmf_db_abort` through a random time delay before abort
- Corrected read and write task return logic for data_bus unit test

## v0.7.8

### Bug Fixes

- Fixed pre-release input port when bypass of payload in last element
- Use atomic operation to avoid task action flag race condition
- Fixed `esp_gmf_node_del_at` corner cases

## v0.7.7

### Bug Fixes

- Uniformed the `esp_gmf_db_abort` and `esp_gmf_db_done_write` behavior for all data_bus types
- Uniformed the `esp_gmf_db_reset` behavior for all data_bus types
- Fixed wrong return values for data_bus related api
- Fixed incorrect comments for data_bus related api
- Fixed create fifo data_bus use wrong handle type
- Added unit test for `esp_gmf_db_abort`
- Added `esp_gmf_db_reset` and `esp_gmf_db_done_write` in unit test

## v0.7.6

### Bug Fixes

- Fixed racing condition when load method and capability
- Fixed dependency and non-dependency element wrong open order
- Refine some warning log output
- Correct logic for API `esp_gmf_pipeline_get_next_el`

## v0.7.5

### Features

- Support C++ build

## v0.7.4

### Bug Fixes

- Fixed incorrect bit shift in the macro converting FourCC code to a string

## v0.7.3

### Bug Fixes

- Fixed possible dead lock for released port after close
- Fixed pipeline previous action state not cleared after stopped
- Fixed incorrect task stack information when using `esp_gmf_oal_sys_get_real_time_stats`

## v0.7.2

### Bug Fixes

- Fixed an issue where the pipeline overwrote the event callback state when both input and output IO operations failed

## v0.7.1

### Bug Fixes

- Fixed a bug where the pause and stop operations lagged and caused the run API call to fail

## v0.7.0

### Features
- Added `esp_gmf_pool_iterate_element` to support iteration over elements in the GMF pool
- Added FourCC codes to represent video element caps
- Optimized GMF argument and method name handling to avoid unnecessary copying
- Added `esp_gmf_oal_get_spiram_cache_align` for retrieving SPIRAM cache alignment
- Added helper macros for defining and retrieving GMF method and argument identifiers
- Added helper function for GMF method execution
- Added `esp_gmf_io_reset` API to reset the IO thread and reload jobs
- Added `meta_flag` field to `esp_gmf_payload_t` to support audio decoder recovery status tracking
- Added raw_pcm in `esp_fourcc.h`
- Added `esp_gmf_pool_register_element_at_head` for insertion of elements at the head of the pool
- Enhanced `esp_gmf_oal_thread_delete` to accept NULL handle as a valid input
- Enhanced GMF task to avoid race condition when stop

### Bug Fixes

- Fixed pause timeout caused by missing sync event when pause producer entered an error state
- Fixed a thread safety issue with the gmf_task `running` flag
- Fixed parameter type mismatch in memory transfer operations to ensure data integrity
- Corrected return value validation for *acq_write/read and *acq_release_write/read callback function which in esp_gmf_ring_buffer.c
- Fixed issue where element name renaming in the pool was lost when duplicate new one from it
- Fixed `esp_gmf_cache_acquire` still report OK even not cache enough data
- Fixed stop timeout occurred due to an element keep on report CONTINUE
- Fixed method helper to support method without argument
- Fixed used after free when once job return TRUNCATE
- Removed unused configuration buffer for GMF task
- Fixed memory leakage when clear-up when `esp_gmf_task_init` failed

## v0.6.1

### Bug Fixes

- Fixed an issue where gmf_task failed to wait for event bits
- Fixed compilation failure when building for P4


## v0.6.0

### Features
- Added GMF element capabilities
- Added GMF port attribution functionality
- Added gmf_cache APIs for GMF payload caching
- Added truncated loop path handling for gmf_task execution
- Added memory alignment support for GMF fifo bus
- Renamed component to `gmf_core`

### Bug Fixes

- Fixed support for NULL configuration in GMF object initialization
- Standardized return values of port and data bus acquire/release APIs to esp_gmf_err_io_t only
- Improved task list output formatting

## v0.5.1

### Bug Fixes

- Fixed an issue where the block data bus returns a valid size when the done flag is set
- Fixed an issue where the block unit test contained incorrect code


## v0.5.0

### Features

- Initial version of `esp-gmf-core`
