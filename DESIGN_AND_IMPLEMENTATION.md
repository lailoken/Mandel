# Mandelbrot Explorer — Design and Implementation

This document describes the design and implementation of the application at a level that allows recreating it from scratch. It covers architecture, rendering, UI, dragging/panning, persistence, and platform choices.

---

## 1. High-level architecture

- **Single process:** SDL window, OpenGL for rendering, Dear ImGui for UI. Main thread runs the event loop and all UI; a shared **thread pool** does Mandelbrot work.
- **UI smoothness first:** The main thread never blocks on rendering. Input (drag, resize, controls) is handled every frame; pan triggers an async render; when the pool is idle and a new frame is ready, the app swaps it in and converts drag state so the view does not jump.
- **Layered responsibilities:** Platform (SDL/OpenGL) creates window and textures; **MandelUI** owns viewport, overscan, drag state, and worker; **MandelWorker** owns the renderer and talks to the thread pool; **MandelbrotRenderer** does the actual set computation and writes pixels. Control panel is a separate ImGui window that talks to the UI via an interface.

---

## 2. Main loop and threading

- **Main loop:** Poll SDL events → ImGui new frame → `MandelUI::draw()` (resize, draw texture, draw control window, then handle pan/drag input) → ImGui render → OpenGL clear and draw → swap buffers. V-sync on.
- **Thread pool:** Created once at startup with `hardware_concurrency() - 1` workers (reserving one for UI). Used only for Mandelbrot rendering (recursive tasks).
- **Worker model:** A **worker** abstraction (e.g. MandelWorker) holds canvas dimensions, complex bounds, and a reference to the pool and a **generation** counter. Starting a render increments the generation and submits work to the pool. The renderer and tasks check the generation and bail if it changed (cancel). The UI never blocks on the pool; it polls each frame and when the pool is idle and the current render’s generation matches, it merges the renderer’s buffer into the worker’s canvas and swaps the displayed texture.

---

## 3. Mandelbrot rendering

- **Algorithm:** Recursive divide-and-conquer over a rectangle of pixels. Standard iteration z := z² + c with |z|² > 4 as escape. Precision: long double for coordinates.
- **Y-Axis Handling:** Rendering uses a **Top-Down** approach (row 0 = y_max) to match standard image buffers and ImGui display coordinates.
- **Buffer:** Renderer owns an internal RGBA buffer. It is filled by the recursive tasks. When the pool is idle, the worker copies this buffer into its own canvas and the UI uploads it to a texture.

---

## 4. Viewport, overscan, and canvas

- **Viewport:** The visible screen region.
- **Overscan:** Always on. The **canvas** is larger than the viewport: viewport plus margins on all sides (e.g. ~1/6 of viewport size per side). The Mandelbrot is rendered for the full canvas bounds.
- **Bounds:** **Viewport bounds** (complex space) describe the visible rectangle. **Canvas bounds** are the viewport bounds extended by the margins. Conversion viewport ↔ canvas is linear.

---

## 5. Dragging and panning (Pixel-Perfect Smoothness)

Design follows a **display-offset** model where the image tracks the mouse exactly during drag, and seamlessly swaps textures without visual jumps.

- **Display offset:** Accumulates mouse delta in **screen pixels**. Drawn with integer snapping (`std::floor`) to prevent sub-pixel shimmer.
- **Pan Logic:**
  1. **Drag:** Accumulate offset.
  2. **Threshold:** When offset > threshold (40% margin) and no render pending, trigger **handle_pan**.
  3. **Snap & Store:** Calculate `pan_complex` from `offset`. Snap `pan_complex` to **exact integer pixels** (using `std::lround`) to ensure the new texture grid aligns perfectly with the old one. Store this integer shift as `pending_pan_pixels`.
  4. **Render:** Update `render_start` bounds by adding `pan_complex`. **CRITICAL:** Update worker's bounds (`worker_->canvas_x_min_` etc.) before starting render to ensure the new texture actually contains the shifted view.
  5. **Swap:** When render completes:
     - Check if `render_start` matches `pending_pan_pixels`. If divergence > 1px (drift), fallback to calculated shift.
     - Calculate `shift_pixels` (how much the texture moved).
     - Update `display_offset`: `offset_x += shift_x`, `offset_y -= shift_y`.
     - This cancels out the texture shift exactly, leaving the visual image stationary (relative to mouse).
- **Coordinate systems:** Screen: X right, Y down. Mandel: X right, Y up. Texture: Top-down.
- **Zero-Copy Swap:** `MandelWorker` merges buffer. UI uploads to `texture_back`. UI swaps `texture_front` / `texture_back`.

---

## 6. Texture and worker abstraction

- **Double buffering:** Two texture IDs (front/back). The visible image is always “front.”
- **Worker interface:** Abstract worker base: start_render(), try_complete_render().
- **Generation/cancellation:** Atomic generation counter.

---

## 7. Controls and settings

- **Control window:** ImGui window.
- **Input discipline:** Pan only when the mouse is on the background.

---

## 8. Persistence

- **ViewState:** One struct: x_min, x_max, y_min, y_max (complex bounds), max_iterations.
- **Saved views:** Named ViewStates in JSON config.

---

## 9. Platform and rendering

- **Window/context:** SDL2 + OpenGL 3.
- **Textures:** RGBA texture.
- **ImGui:** SDL2 + OpenGL3.

---

## 10. Key Implementation Details for Replication

1.  **Integer Snapping:** Use `std::lround` for pan deltas and `std::floor` for drawing offsets. Floating point drawing coordinates cause shimmering.
2.  **Worker Bounds Update:** Always update the worker's internal bounds before `start_render()`. Failure to do so results in the worker rendering the old view, causing massive visual jumps on swap ("opposite direction" effect).
3.  **Duplication Avoidance:** Never update `display_offset` twice for the same swap.
4.  **Sanity Checks:** Validate that `pending_pan_pixels` matches the actual `render_start` shift before applying the swap correction.
5.  **Thread Pool Reservation:** Reserve one core for the UI thread to prevent starvation during heavy rendering.

This, together with the code, provides a complete blueprint.
