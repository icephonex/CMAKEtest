# LittleFS Minimal Port Design

**Date:** 2026-04-01

## Goal

Keep the full upstream `littlefs` directory in the repository for reference, but integrate only the required core sources into the STM32 project:

- `unilib/thirdparty/littlefs-master/lfs.c`
- `unilib/thirdparty/littlefs-master/lfs.h`
- `unilib/thirdparty/littlefs-master/lfs_util.c`
- `unilib/thirdparty/littlefs-master/lfs_util.h`

Add a dedicated port layer under `Dev/port` for the STM32F103 + external W25Qxx flash use case.

## Scope

This change covers:

- CMake minimal integration for littlefs core sources
- A port directory at `Dev/port`
- A configuration header named `lfs_config.h`
- A littlefs port source/header pair: `lfs_port.c` and `lfs_port.h`

This change does not cover:

- Implementing the W25Qxx SPI command driver itself
- Mounting or formatting littlefs from `main.c`
- Adding application-level file APIs on top of littlefs

## Confirmed Decisions

1. Keep the full `unilib/thirdparty/littlefs-master` directory in place.
2. Only compile the two littlefs source files required by the core library.
3. Create `Dev/port/lfs_port.c`, `Dev/port/lfs_port.h`, and `Dev/port/lfs_config.h`.
4. The port layer only adapts littlefs to an external W25Qxx driver boundary.
5. The port layer must not implement the low-level SPI flash protocol itself.

## Architecture

The integration is split into three layers:

1. Upstream littlefs core
   The upstream files stay untouched under `unilib/thirdparty/littlefs-master`.

2. Project port layer
   `Dev/port/lfs_port.c` translates littlefs block operations into board-support flash operations suitable for a W25Qxx backend.

3. Future W25Qxx driver
   A later driver implementation will provide the actual flash init/read/program/erase/sync behavior expected by the port layer.

## File Responsibilities

### `Dev/port/lfs_config.h`

Centralize the littlefs geometry and port tuning values, including:

- flash base offset used by littlefs
- block size
- block count
- read size
- program size
- cache size
- lookahead size
- block cycle count

These values must be editable in one place and not scattered in the C source.

### `Dev/port/lfs_port.h`

Declare:

- the public littlefs port accessors
- the default mount/config objects
- the expected flash driver hook signatures the application can provide later

This header is the contract between the port layer and the rest of the firmware.

### `Dev/port/lfs_port.c`

Implement:

- static littlefs work buffers
- a default `struct lfs_config`
- block read/program/erase/sync callbacks
- helper functions for init, mount, format, unmount, and object access
- weak flash hook defaults so the project can still link before the real W25Qxx driver is added

The weak defaults must fail cleanly with `LFS_ERR_IO` rather than pretending the flash is usable.

## Driver Boundary

The port layer will call abstract flash hooks intended for the W25Qxx backend:

- `lfs_port_flash_init`
- `lfs_port_flash_read`
- `lfs_port_flash_prog`
- `lfs_port_flash_erase`
- `lfs_port_flash_sync`

The hook layer is intentionally generic even though the target medium is W25Qxx. This keeps littlefs independent from one specific driver file name or vendor API.

## Build Integration

The project should add the littlefs and port sources in user-owned CMake sections, not by editing generated CubeMX source lists.

Preferred integration point:

- top-level `CMakeLists.txt`

Add:

- `Dev/port/lfs_port.c`
- `unilib/thirdparty/littlefs-master/lfs.c`
- `unilib/thirdparty/littlefs-master/lfs_util.c`

Add include directories:

- `Dev/port`
- `unilib/thirdparty/littlefs-master`

This keeps the CubeMX-generated `cmake/stm32cubemx/CMakeLists.txt` stable.

## Error Handling

If the real flash backend is not implemented yet:

- `lfs_port_init` should fail cleanly
- read/program/erase/sync callbacks should return `LFS_ERR_IO`
- no fake success path should be introduced

This allows the project to compile while making integration gaps explicit.

## Verification

Success criteria for this change:

1. The project configures and builds with CMake after the new files are added.
2. Only the required littlefs core sources are compiled into the target.
3. The `Dev/port` directory exists with `lfs_port.c`, `lfs_port.h`, and `lfs_config.h`.
4. The project links successfully even before a concrete W25Qxx driver is added.
5. The port API clearly exposes where the future W25Qxx implementation must connect.
