# Agent Guide for `esp_video_render` Emulation

This file is written for coding agents (LLVM/LLM/Codex-style) to quickly understand and generate a **video-first UI system** on top of `esp_video_render`, then validate it through emulation.

---

## 1) What this project is good at (core capability)

`esp_video_render` is optimized for:
- high-performance video rendering
- multi-stream composition
- overlay + widget rendering above video
- dirty-region based partial redraw
- dual-eye display scenarios

In other words: prefer this stack when UI is primarily attached to video (e.g., call UI, camera UI, AR overlay).

---

## 2) Emulation goal for agents

When user gives a UI requirement (even an image mockup), agent should:
1. map mockup into **containers + widgets** attached to stream overlay
2. generate widget code and state update code
3. run emulation build/test path
4. iterate until visual/logic is correct

---

## 3) Key folders you should read first

- `../README.md` — component capabilities and architecture
- `../RENDER_DESIGN.md` — dirty-region and compose design
- `../include/vui/` — public overlay/widget/container APIs
- `../src/vui/` — widget implementations
- `../test_apps/main/xiaozhi_panel_widget.c` — reference for practical UI widget composition
- `./src/main.c` — emulation app entry
- `./tests/` — emulation test patterns

---

## 4) Build and test loop (emulation)

From `emulation/`:

```bash
./build.sh
cd build
ctest --output-on-failure
```

If missing dependencies (SDL2/Freetype), report exact package issue and stop guessing.

---

## 5) How to design UI for video_render (agent workflow)

### Step A — Convert requirement (or image) into layout spec
Create an internal spec table:
- screen size
- stream region(s)
- each overlay container: x/y/w/h, z-order
- widgets per container: type, geometry, style, interaction state
- refresh model: static / periodic / event-driven

### Step B — Choose render strategy
- Video as base stream framebuffer
- Overlay containers for UI
- Prefer partial redraw with dirty rectangles
- For animated text, update only changed region

### Step C — Generate widget code
- Create or reuse widget(s)
- Implement `redraw()` + property setters
- Ensure setter changes trigger dirty update

### Step D — Integrate to emulation main/test
- Instantiate render + stream + overlay + containers + widgets
- Feed sample frames
- Add test path in `emulation/tests` if behavior is deterministic

---

## 6) How to create a custom widget (required pattern)

A custom widget should follow this structure:

1. **Context struct**
   - embeds `esp_vui_widget_t widget;`
   - stores visual/state fields (text/icon/pressed/etc)

2. **Init function**
   - allocate ctx
   - set `widget.rect`, defaults
   - assign callbacks:
     - `widget.redraw = your_widget_redraw`
     - `widget.destroy = your_widget_destroy`
   - add to container via `esp_vui_container_add_widget(...)`

3. **Redraw function**
   - validate args
   - clip to dirty rect
   - draw only necessary pixels
   - avoid full redraw unless required

4. **Setters / state update API**
   - update internal state
   - notify compose changed via container APIs / dirty rect update

5. **Destroy function**
   - release owned resources

Agent should always implement widget with dirty-region awareness.

---

## 7) Image-to-UI generation policy

If user inputs an image/mockup:

1. Detect components:
   - icon buttons (answer/hang/mute)
   - labels (name/timer/status)
   - panels/badges
2. Translate into widget types:
   - image widget for icons
   - text widget for labels
   - create new custom widget only when behavior needs it
3. Generate deterministic geometry from target resolution
4. Add style constants centrally (colors, spacing, radius, font sizes)
5. Emit code + emulation test harness

Agent output should include:
- mapping summary (mockup → widgets)
- generated files
- build/test commands and result

---

## 8) Performance rules (important)

- Avoid full-screen dirty updates for small UI changes
- Keep compose lock critical sections short
- For scrolling/animated widgets: dirty only old/new bounding union
- Avoid per-frame allocations in redraw path
- Cache expensive text metrics where possible

---

## 9) Minimal "video call UI" blueprint (reference)

Use this when user asks quick call UI:
- full-screen stream for remote video
- bottom bar container
  - answer icon widget
  - hang icon widget
  - mute icon widget
- top label container
  - caller name text widget
  - call timer text widget

State machine:
- incoming -> answered -> connected -> ended
- each transition updates only related widgets/labels

---

## 10) Done criteria for agent-generated UI

Consider task done only if:
1. code compiles in emulation build
2. tests/smoke checks are runnable
3. redraw path uses partial dirty updates
4. no obvious memory leaks in widget init/destroy
5. generated UI behavior matches requirement/mockup

