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
- **Thread pool:** Created once at startup with `hardware_concurrency()` workers. Used only for Mandelbrot rendering (recursive tasks). Pool supports add_task, pause/resume, is_idle; no work is run on the main thread except submission and completion checks.
- **Worker model:** A **worker** abstraction (e.g. MandelWorker) holds canvas dimensions, complex bounds, and a reference to the pool and a **generation** counter. Starting a render increments the generation and submits work to the pool. The renderer and tasks check the generation and bail if it changed (cancel). The UI never blocks on the pool; it polls each frame and when the pool is idle and the current render’s generation matches, it merges the renderer’s buffer into the worker’s canvas and swaps the displayed texture.

---

## 3. Mandelbrot rendering

- **Algorithm:** Recursive divide-and-conquer over a rectangle of pixels. For a rectangle, compute the iteration count on the boundary (perimeter). If all boundary pixels have the same count and the rectangle is not the “full set” (e.g. not the default view containing the whole set), fill the interior with that count; otherwise split into four subrectangles (e.g. quad split) and submit each to the thread pool recursively. Small rectangles or “direct” cases are computed without further subdivision. This gives good parallelism and avoids work in uniform regions.
- **Escape-time:** Standard iteration z := z² + c with |z|² > 4 as escape; squared magnitude avoids sqrt. Max iterations and bounds (x_min, x_max, y_min, y_max) are parameters. Precision: long double for coordinates and iteration math where possible.
- **Coloring:** Palette of 256 (or similar) colors; iteration count (mod palette size) indexes the color; max_iter pixels are black. Palette is a singleton (e.g. sinusoidal RGB).
- **Buffer:** Renderer owns an internal RGBA buffer. It is filled by the recursive tasks (with generation checks before writes). When the pool is idle, the worker copies this buffer into its own canvas and the UI uploads it to a texture. No direct main-thread pixel computation.

---

## 4. Viewport, overscan, and canvas

- **Viewport:** The visible screen region (e.g. main ImGui viewport or full window). Dimensions (viewport_width, viewport_height) drive all “visible” math.
- **Overscan:** Always on. The **canvas** is larger than the viewport: viewport plus margins on all sides (e.g. ~1/6 of viewport size per side). The Mandelbrot is rendered for the full canvas bounds. This gives extra pixels so that during drag the user can move within the margin before a new render is needed.
- **Bounds:** Two coordinate systems. **Viewport bounds** (x_min, x_max, y_min, y_max in complex space) describe the visible rectangle. **Canvas bounds** are the viewport bounds extended by the margins (in complex units). Conversion viewport ↔ canvas is linear (scale by margin in pixels vs viewport size). Texture dimensions are canvas_width × canvas_height (viewport + 2×margin each axis).

---

## 5. Dragging and panning (seamless transitions)

Design follows a **display-offset** model so that during drag the image moves immediately with the mouse, and when a new texture is ready the same complex point stays under the cursor.

- **Display offset:** Two scalars (x, y) in **screen (viewport) pixels**, meaning “shift of the texture from center.” Positive X = drag right = texture shifts left (show content from the left). Positive Y = drag down = texture shifts up (show content from the top). Dragging adds mouse delta to the offset; the draw path uses it to offset the UV window (e.g. `uv_offset = -display_offset / canvas_size`).
- **Coordinate systems:** Screen: origin top-left, X right positive, Y down positive. Mandel/complex: X right positive, Y **up** positive. Texture: row 0 = top = high complex Y. Pan direction must match the draw (e.g. `pan = -display_offset * (viewport_range / viewport_size)` so that drag-right shows content from the left).
- **Two bound sets:** **Displayed texture bounds** = complex bounds of the texture currently on screen. **Render-start bounds** = complex bounds of the texture currently being rendered (for the next swap). When a render is in progress, the front texture is still the old one; do not overwrite displayed bounds with the in-flight render’s bounds.
- **Flow:**  
  1. **Drag:** Only when mouse is on background (e.g. `!WantCaptureMouse`, mouse in viewport, not over any ImGui window). Accumulate offset; draw uses it so the image moves with the drag.  
  2. **Threshold:** When |offset| exceeds a threshold (e.g. fraction of overscan margin), and no render is pending, **handle_pan**: save current bounds as displayed (if not already from a pending render), compute new viewport bounds by applying pan in complex space (using viewport size and range), convert to canvas bounds, set render_start = new canvas bounds, **do not** reset display_offset, start render.  
  3. **Render completes:** When the pool is idle and the current render’s generation matches, merge renderer buffer into worker canvas, then **swap**: convert display_offset from old (displayed) to new (render_start) so the same complex point stays under (viewport center + offset), upload new image to back texture, swap front/back, set displayed = render_start.
- **Conversion on swap:** Input: old bounds, new bounds, current display_offset (viewport pixels). Output: new display_offset (viewport pixels). Method: (1) Viewport point = center + offset; map to canvas pixel (add margin), then to complex using old bounds (canvas row 0 → high complex Y). (2) Map that complex point to canvas pixel in new bounds. (3) New offset = (new canvas pixel − margin − viewport center), in the same units as viewport pixels (in the viewport strip, canvas pixels and viewport pixels are 1:1).
- **One pan per flight:** Do not start another pan while a render is pending; this avoids runaway and keeps displayed vs render_start consistent.

---

## 6. Texture and worker abstraction

- **Double buffering:** Two texture IDs (front/back). The visible image is always “front.” When a new frame is ready, it is uploaded to “back,” then front and back are swapped. Upload and swap happen on the main thread only when the worker has finished the merge.
- **Worker interface:** Abstract worker base: start_render(), try_complete_render() (non-blocking: if pool idle and generation matches, merge into canvas and return true), set_max_iterations(). Concrete MandelWorker: holds a MandelbrotRenderer (created per render), uses the thread pool; try_complete_render() merges the renderer’s buffer into the worker’s canvas and returns true when the pool is idle and the render is for the current generation.
- **Generation/cancellation:** A single atomic generation counter is shared. Starting a new render increments it. Renderer and tasks capture “start” generation and skip work or writes if the current generation no longer matches, so old work does not overwrite new results.

---

## 7. Controls and settings

- **Control window:** ImGui window (e.g. “Controls”) with max iterations slider, bounds or zoom inputs, overscan (if exposed), saved views table, and optional “render in progress” / “dragging” readouts. It does not own state; it reads/writes through a **UI control interface** (get_viewport_bounds, get/set_pending_settings, apply_view_state, get_saved_views, etc.).
- **Pending vs applied:** The UI can keep “applied” (last committed) and “pending” (edited but not yet applied) view state. Controls show pending when present; “apply” means set canvas bounds and max iterations from pending, start a new render, clear pending. This keeps slider/edits from fighting the live view and allows “apply when ready.”
- **Input discipline:** Pan only when the mouse is on the background (e.g. not over any window and not captured by a widget). This prevents the iteration slider or other controls from moving the Mandelbrot view.

---

## 8. Persistence

- **ViewState:** One struct: x_min, x_max, y_min, y_max (complex bounds), max_iterations. Used for viewport bounds, saved views, and config.
- **Saved views:** Named ViewStates (e.g. map name → ViewState). User can save current view under a name, load by name, delete. Double-click or “apply” loads that view (set bounds and max_iter, start render).
- **Config file:** Single file (e.g. `~/.mandel` or similar) stores saved views as JSON. ViewState uses high precision (e.g. long double) so serialization uses string or precise number format for floats. Optional: store “current” view on exit and restore on startup; at minimum, named views are saved and restored.
- **No persistence of:** window position/size (handled by platform or not), open dialogs, or undo history.

---

## 9. Platform and rendering

- **Window/context:** SDL2 for window, events, and OpenGL context; OpenGL 3 core for rendering. V-sync on. Resize updates viewport and canvas dimensions; worker and overscan are recreated for the new size and a new render is started.
- **Textures:** One RGBA texture per buffer (front/back). Create on first upload; update with glTexImage2D. Draw with ImGui’s draw list (e.g. background draw list) so the Mandelbrot is behind all ImGui windows. UVs and draw rect are derived from overscan (viewport region in the canvas texture) and display_offset.
- **ImGui:** Used for all UI (controls, tables, inputs). ImGui backends: SDL2 for input, OpenGL3 for drawing. Main viewport is the full window; the Mandelbrot is drawn into the background; the control window is a normal ImGui window on top.

---

## 10. Summary table

| Area            | Choice / principle                                                |
|-----------------|-------------------------------------------------------------------|
| Responsiveness  | Main thread never blocks on render; poll for completion each frame |
| Parallelism     | Shared thread pool; recursive divide-and-conquer over rectangles  |
| Cancellation    | Atomic generation; tasks and renderer check before doing work     |
| Drag            | Display-offset model; no reset on pan; convert offset on swap    |
| Bounds          | Displayed (on screen) vs render_start (in flight); use displayed when render pending |
| Overscan        | Always on; canvas = viewport + margins; threshold ~ fraction of margin |
| Coordinates     | Screen Y down; complex Y up; texture top = high Y; pan sign matches UV |
| Persistence     | ViewState; saved views in JSON config file                       |
| Controls        | Interface to UI; pending/applied; pan only when mouse on background |

This, together with the **DRAGGING_REDESIGN.md** for exact drag/swap rules and formulas, is enough to reimplement the application from scratch at a broad level.
