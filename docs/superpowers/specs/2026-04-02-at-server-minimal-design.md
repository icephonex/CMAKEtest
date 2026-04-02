# Minimal AT Server Design

**Date:** 2026-04-02

## Goal

Add a minimal AT command path on top of the existing STM32 + FreeRTOS + UART DMA project so the firmware can:

- receive UART bytes by DMA
- move received bytes into an `lwrb` ring buffer on UART idle interrupt
- poll and parse commands from a FreeRTOS task named `task_loop`
- recognize the complete command `AT\r\n`
- reply with `OK\r\n` for `AT\r\n`
- reply with `ERROR\r\n` for any other complete command

This first step is only meant to validate the full receive -> buffer -> parse -> respond path.

## Scope

This change covers:

- a generic AT protocol engine
- a project-specific AT port layer
- UART DMA idle reception handoff into `lwrb`
- a polling FreeRTOS task in `main.c`
- minimal command handling for `AT`

This change does not cover:

- a full `AT+XXX` command table
- event-driven task wakeup
- multi-UART AT services
- persistent configuration changes
- business features such as MAC, MODE, REBOOT, NFC, EVC, display, or relay control

## Confirmed Decisions

1. A command is complete only when it ends with `\r\n`.
2. Split packets must be supported. For example, `AT` followed later by `\r\n` must still be recognized as one valid command.
3. Unknown complete commands must return `ERROR\r\n`.
4. The parsing task uses simple polling rather than interrupt-to-task notification.
5. The FreeRTOS task is created in `Core/Src/main.c` and named `task_loop`.
6. The AT protocol layer and the project business layer must remain separate.

## Architecture

The design is split into four layers:

1. UART DMA + interrupt handoff
   DMA receives bytes into a temporary UART RX buffer. When USART idle is detected, the interrupt layer determines how many bytes arrived and forwards that byte block to the AT engine input path.

2. Generic AT engine
   `at_server.c` and `at_server.h` implement byte buffering, line assembly, session state, AT syntax recognition, command dispatch, and unified response formatting.

3. Project port layer
   `at_server_port.c` and `at_server_port.h` adapt the generic engine to this firmware project by providing output hooks, compile-time configuration, and command-specific business handling.

4. FreeRTOS polling task
   `task_loop` repeatedly calls the AT engine poll function. Parsing and response generation happen in task context, not in interrupt context.

This keeps the interrupt path short, keeps protocol parsing centralized, and leaves project-specific behavior outside the generic parser.

## File Responsibilities

### `unilib/src/at_server.h`

Declare the generic AT engine API, including:

- initialization
- byte-input API used by the UART handoff
- polling API used by `task_loop`
- request and response data types shared with the port layer

This header defines how the project uses the AT engine without exposing business logic.

### `unilib/src/at_server.c`

Implement the generic AT engine. Its responsibilities are:

- receive byte streams and cache them in `lwrb`
- detect complete commands terminated by `\r\n`
- maintain line assembly state across split packets
- parse protocol shape
- distinguish basic forms such as `AT`, `AT+NAME`, `AT+NAME?`, and `AT+NAME=...`
- dispatch non-built-in commands to the port layer
- emit unified responses such as `OK\r\n`, `ERROR\r\n`, and header/data style responses

It must not know what any project command actually does.

### `Dev/port/at_server_port.h`

Declare the project-specific adaptation layer, including:

- project configuration values for the AT engine
- send/output hooks
- the command handling interface used by `at_server`

This header is the contract between the generic engine and the firmware project.

### `Dev/port/at_server_port.c`

Implement the project adaptation layer. Its responsibilities are:

- provide project configuration to the generic AT engine
- provide the concrete UART send path
- implement business handling for project commands
- read and write project state when later `AT+XXX` commands are added

It must not assemble commands from raw bytes and must not own `\r\n` detection logic.

### `Core/Src/main.c`

Own system integration for this feature:

- call the AT engine initialization after USART1 is initialized
- create the FreeRTOS task named `task_loop`
- run `AT_Server_Poll()` from inside the `task_loop` loop

### `Core/Src/stm32f1xx_it.c`

Own only interrupt-level reception handoff:

- detect USART1 idle
- calculate received DMA length
- pass the received byte block into the AT engine input API
- restart DMA reception

It must not parse commands and must not execute command business logic.

### `CMakeLists.txt`

Add the new AT sources and the third-party ring buffer source to the build:

- `unilib/src/at_server.c`
- `Dev/port/at_server_port.c`
- `unilib/thirdparty/lwrb-develop/lwrb.c`

Add the required include directories for:

- `unilib/src`
- `Dev/port`
- `unilib/thirdparty/lwrb-develop`

## Data Flow

1. The host sends bytes over USART1.
2. DMA stores the bytes in a UART RX memory buffer.
3. USART1 idle interrupt fires when a receive gap appears.
4. The interrupt handler determines the valid byte count from the DMA state.
5. The byte block is handed to the generic AT engine input function.
6. The AT engine writes the bytes into the `lwrb` ring buffer.
7. `task_loop` periodically calls `AT_Server_Poll()`.
8. `AT_Server_Poll()` reads bytes from `lwrb` and appends them to the current line buffer.
9. When `\r\n` is detected, the current line is considered one complete command.
10. If the command is exactly `AT`, the engine sends `OK\r\n`.
11. If the command is anything else in this minimal version, the engine sends `ERROR\r\n`.
12. The line buffer is reset so the next command can be assembled.

Because the line buffer persists across poll iterations, a command split across multiple DMA idle events is still reassembled correctly.

## Protocol Rules

The generic engine owns protocol recognition.

For this minimal version:

- empty partial input does not produce any response
- `AT\r\n` returns `OK\r\n`
- any other complete line returns `ERROR\r\n`
- command completion depends only on `\r\n`
- `AT` without `\r\n` is incomplete and must wait

The engine should already be shaped so later work can extend the parser to:

- `AT+NAME`
- `AT+NAME?`
- `AT+NAME=VALUE`

That future extensibility must not complicate the minimal implementation beyond what is needed for a clean interface.

## Session State

The generic engine keeps the following state:

- ring buffer instance and backing storage
- current line assembly buffer
- current assembled line length
- optional parser markers needed to classify a complete command
- UART output target or output callback references required by the port layer

Only the generic engine owns partial-line state. The port layer only receives complete parsed requests.

## Error Handling

### UART / DMA handoff

- If idle interrupt fires with zero received bytes, restart DMA reception and do nothing else.
- If byte insertion into `lwrb` cannot store the full byte block, drop the excess bytes and invalidate the current line assembly state so a corrupted partial command is not later accepted.
- DMA reception must always be restarted after the idle event is handled.

### Parser behavior

- If the line buffer overflows before a `\r\n` terminator is completed, discard the current line and return `ERROR\r\n` once the line is closed.
- If the completed command is not recognized in this minimal version, return `ERROR\r\n`.
- If the engine receives fragmented input, it must keep waiting until a complete `\r\n` terminated line is available.

### Layering rules

- `at_server` decides whether a command is complete, valid, and dispatchable.
- `at_server_port` decides what project-specific commands do.
- interrupt handlers move bytes only; they do not interpret command meaning.

## Minimal Command Behavior

The minimum supported behavior for this design is:

1. Recognize the bare command `AT`.
2. Handle bare `AT` inside the generic engine as a built-in base command.
3. Reserve port-layer dispatch for future `AT+XXX` commands.

This keeps the first implementation small while preserving the intended architecture.

## Verification

Success criteria for this change:

1. The project builds with the new AT engine, port layer, and `lwrb` source linked.
2. `main.c` creates a FreeRTOS task named `task_loop`.
3. `task_loop` repeatedly calls `AT_Server_Poll()`.
4. Sending `AT\r\n` over USART1 returns `OK\r\n`.
5. Sending `AT` and later `\r\n` still returns `OK\r\n`.
6. Sending `ATX\r\n` returns `ERROR\r\n`.
7. Sending multiple commands back-to-back such as `AT\r\nATX\r\nAT\r\n` returns `OK\r\nERROR\r\nOK\r\n` in order.
8. Sending `AT` without `\r\n` produces no premature response.
9. Sending an overlong line does not crash the firmware and results in the current line being discarded with `ERROR\r\n`.

## Implementation Notes

- The first implementation should stay minimal and readable.
- Parsing must happen in task context.
- The interrupt path should avoid string parsing and business branching.
- The design should leave room for future `AT+XXX` command dispatch without requiring a rewrite of the byte assembly path.
