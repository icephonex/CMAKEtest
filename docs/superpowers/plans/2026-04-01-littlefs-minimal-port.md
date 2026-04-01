# LittleFS Minimal Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Integrate only the required littlefs core files into the STM32 project and add a `Dev/port` adapter layer for a future W25Qxx backend.

**Architecture:** Keep upstream littlefs untouched under `unilib/thirdparty/littlefs-master`, wire only `lfs.c` and `lfs_util.c` into the firmware build, and provide a project-owned adapter layer under `Dev/port` with centralized configuration in `lfs_config.h`. The adapter exposes weak flash hooks so the project links now and can later bind to a real W25Qxx driver without changing littlefs core files.

**Tech Stack:** STM32CubeMX-generated C project, CMake, ARM GCC, littlefs

---

### Task 1: Add the project-owned design documents

**Files:**
- Create: `docs/superpowers/specs/2026-04-01-littlefs-minimal-port-design.md`
- Create: `docs/superpowers/plans/2026-04-01-littlefs-minimal-port.md`

- [ ] **Step 1: Write the approved design doc**

```md
Document the confirmed decisions:
- keep `unilib/thirdparty/littlefs-master`
- compile only `lfs.c` and `lfs_util.c`
- add `Dev/port/lfs_port.c`
- add `Dev/port/lfs_port.h`
- add `Dev/port/lfs_config.h`
- keep W25Qxx logic outside the littlefs port layer
```

- [ ] **Step 2: Write the implementation plan**

```md
Break the work into:
- CMake integration
- port header/config creation
- port source implementation
- build verification
```

- [ ] **Step 3: Review both files for naming consistency**

Run: `rg -n "config.h|lfs_config.h|lfs_port" docs/superpowers/specs docs/superpowers/plans`

Expected: Only `lfs_config.h` is used for the configuration header name.

### Task 2: Wire minimal littlefs sources into the build

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add a failing build check for the intended new sources**

Run: `cmake --build --preset Debug`

Expected: The current build succeeds without littlefs integration, which confirms the baseline before changing the build graph.

- [ ] **Step 2: Add the minimal littlefs and port sources**

```cmake
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port/lfs_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs.c
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master/lfs_util.c
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Dev/port
    ${CMAKE_CURRENT_SOURCE_DIR}/unilib/thirdparty/littlefs-master
)
```

- [ ] **Step 3: Run the build to observe the expected red phase**

Run: `cmake --build --preset Debug`

Expected: FAIL because `Dev/port/lfs_port.c` and its headers do not exist yet.

### Task 3: Add the littlefs configuration and port interface

**Files:**
- Create: `Dev/port/lfs_config.h`
- Create: `Dev/port/lfs_port.h`

- [ ] **Step 1: Create the configuration header**

```c
#ifndef LFS_CONFIG_H
#define LFS_CONFIG_H

#define LFS_PORT_FLASH_BASE_OFFSET   (0u)
#define LFS_PORT_READ_SIZE           (16u)
#define LFS_PORT_PROG_SIZE           (256u)
#define LFS_PORT_BLOCK_SIZE          (4096u)
#define LFS_PORT_BLOCK_COUNT         (128u)
#define LFS_PORT_BLOCK_CYCLES        (500)
#define LFS_PORT_CACHE_SIZE          (256u)
#define LFS_PORT_LOOKAHEAD_SIZE      (32u)

#endif
```

- [ ] **Step 2: Create the port header contract**

```c
int lfs_port_init(void);
int lfs_port_mount(void);
int lfs_port_format(void);
int lfs_port_mount_or_format(void);
int lfs_port_unmount(void);
lfs_t *lfs_port_fs(void);
const struct lfs_config *lfs_port_cfg(void);
```

- [ ] **Step 3: Add the future flash hook declarations**

```c
int lfs_port_flash_init(void);
int lfs_port_flash_read(uint32_t address, void *buffer, uint32_t size);
int lfs_port_flash_prog(uint32_t address, const void *buffer, uint32_t size);
int lfs_port_flash_erase(uint32_t address, uint32_t size);
int lfs_port_flash_sync(void);
```

- [ ] **Step 4: Re-run the build to confirm the next expected failure**

Run: `cmake --build --preset Debug`

Expected: FAIL because `lfs_port.c` is still missing, while the new headers are now found.

### Task 4: Implement the adapter source

**Files:**
- Create: `Dev/port/lfs_port.c`

- [ ] **Step 1: Implement weak flash hook defaults**

```c
__attribute__((weak)) int lfs_port_flash_init(void) { return -1; }
__attribute__((weak)) int lfs_port_flash_read(uint32_t address, void *buffer, uint32_t size) { return -1; }
__attribute__((weak)) int lfs_port_flash_prog(uint32_t address, const void *buffer, uint32_t size) { return -1; }
__attribute__((weak)) int lfs_port_flash_erase(uint32_t address, uint32_t size) { return -1; }
__attribute__((weak)) int lfs_port_flash_sync(void) { return -1; }
```

- [ ] **Step 2: Implement littlefs block callbacks**

```c
static int lfs_port_bd_read(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);
static int lfs_port_bd_prog(const struct lfs_config *c, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);
static int lfs_port_bd_erase(const struct lfs_config *c, lfs_block_t block);
static int lfs_port_bd_sync(const struct lfs_config *c);
```

- [ ] **Step 3: Implement the static config and buffers**

```c
static uint8_t s_lfs_read_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_prog_buffer[LFS_PORT_CACHE_SIZE];
static uint8_t s_lfs_lookahead_buffer[LFS_PORT_LOOKAHEAD_SIZE];
static lfs_t s_lfs;
static struct lfs_config s_lfs_cfg = { ... };
```

- [ ] **Step 4: Implement helper APIs**

```c
int lfs_port_init(void) { ... }
int lfs_port_mount(void) { return lfs_mount(&s_lfs, &s_lfs_cfg); }
int lfs_port_format(void) { return lfs_format(&s_lfs, &s_lfs_cfg); }
int lfs_port_mount_or_format(void) { ... }
int lfs_port_unmount(void) { return lfs_unmount(&s_lfs); }
```

- [ ] **Step 5: Run the build to verify green**

Run: `cmake --build --preset Debug`

Expected: PASS and link succeeds with the weak hook defaults.

### Task 5: Final verification

**Files:**
- Verify: `CMakeLists.txt`
- Verify: `Dev/port/lfs_config.h`
- Verify: `Dev/port/lfs_port.h`
- Verify: `Dev/port/lfs_port.c`

- [ ] **Step 1: Verify only the intended littlefs source files are in the build**

Run: `rg -n "littlefs-master/.+\\.c|lfs_port.c" build/Debug/compile_commands.json`

Expected: Matches for `lfs.c`, `lfs_util.c`, and `lfs_port.c`, with no test runner or block-device helper sources.

- [ ] **Step 2: Verify the new directory layout**

Run: `rg --files Dev/port`

Expected:
`Dev/port/lfs_config.h`
`Dev/port/lfs_port.h`
`Dev/port/lfs_port.c`

- [ ] **Step 3: Verify the full build one more time**

Run: `cmake --build --preset Debug`

Expected: PASS with exit code 0.
