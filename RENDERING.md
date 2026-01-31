# Rendering Architecture

## Design Goals

1. **Buttery smooth interaction** - Dragging and zooming feel instant
2. **Progressive refinement** - See results as computed (when idle)
3. **No visual glitches** - No tearing, flickering, or jarring transitions

---

## Components

### MandelbrotRenderer (`mandel.hpp/cpp`)
- Owns CPU pixel buffer (`std::vector<unsigned char> pixels_`)
- Handles Mandelbrot calculation with recursive boundary-tracing + flood fill
- Manages `ThreadPool` for parallel computation
- Tracks complex plane bounds and zoom level
- Uses `long double` for 10^15× zoom precision

### ImGuiRenderer (`mandel_render.hpp/cpp`)
- Owns two GPU textures: `texture_front_` (displayed), `texture_back_` (upload target)
- Handles user input (drag, zoom, double-click)
- Manages visual offset/scale for responsive interaction

---

## Double-Buffered Textures

```
MandelbrotRenderer::pixels_  →  upload to texture_back_
                                      ↓
                              std::swap(front, back)
                                      ↓
                              draw texture_front_
```

Never modify the displayed texture. Upload to back, swap to front.

---

## Panning

### State
```cpp
float display_offset_x_, display_offset_y_;           // Visual offset (screen coords)
float render_start_offset_x_, render_start_offset_y_; // Snapshot when render starts
bool suppress_texture_updates_;                        // Skip uploads during interaction
```

### Flow
1. **Mouse drags** → `display_offset_ += mouse_delta` (instant visual shift)
2. **Offset exceeds threshold** →
   - `render_start_offset_ = display_offset_` (snapshot)
   - `suppress_texture_updates_ = true`
   - Calculate new bounds: `pan = -(display_offset_ - render_start_offset_) * screen_to_complex`
   - Start render
3. **During render** → Old texture shown at `display_offset_` (user can keep dragging)
4. **Render completes** →
   - Upload to back, swap to front
   - `display_offset_ -= render_start_offset_` (preserve extra drag)
   - `render_start_offset_ = 0`
   - `suppress_texture_updates_ = false`

### Coordinate Conversion
Use **texture size** (width_/height_), not viewport_size. The texture is drawn at its actual
pixel dimensions, so 1 pixel of visual offset = (x_range / width_) complex units. Using
viewport_size causes drift due to float-to-int truncation when resizing.
```cpp
FloatType screen_to_x = x_range / static_cast<FloatType>(width_);
FloatType pan_x = -pending_offset * screen_to_x;
```

---

## Zooming

### State
```cpp
float display_scale_;                    // Visual scale (1.0 = normal)
float zoom_center_x_, zoom_center_y_;    // Mouse position when zoom started
```

### Symmetric Zoom Formula
Use exponential for perfect reversibility:
```cpp
zoom_factor = pow(1.0 + zoom_step_, wheel_delta);
// zoom_in then zoom_out: 1.5^1 × 1.5^(-1) = 1.0
```

### Flow
1. **Mouse wheel** →
   - `display_scale_ = zoom_factor` (instant visual scale)
   - `zoom_center_ = mouse_position` (scale centered here)
   - `suppress_texture_updates_ = true`
   - Calculate new bounds centered on mouse
   - Start render
2. **During render** → Old texture shown scaled around `zoom_center_`
3. **Render completes** →
   - Upload to back, swap to front
   - `display_scale_ = 1.0`
   - `suppress_texture_updates_ = false`

### Drawing with Scale
```cpp
float scale_offset_x = zoom_center_x_ * (1.0f - display_scale_);
float scale_offset_y = zoom_center_y_ * (1.0f - display_scale_);
ImVec2 image_min(scale_offset_x + display_offset_x_, scale_offset_y + display_offset_y_);
ImVec2 image_max(scale_offset_x + scaled_width + display_offset_x_, ...);
```

---

## Render Completion Detection

```cpp
bool is_render_in_progress() const {
    // Skip texture_dirty_ when suppressing (it stays true until final upload)
    if (!suppress_texture_updates_ && texture_dirty_) return true;
    if (threading_enabled_ && !pool->is_idle()) return true;
    return false;
}
```

---

## Generation Counter

Invalidates stale work when user interrupts:
```cpp
void generate_mandelbrot_recurse(..., unsigned int generation) {
    if (generation != render_generation_.load()) return;  // Stale, exit
    // ... compute ...
    if (generation == render_generation_.load() && callback) {
        callback->on_pixels_updated(pixels_, width_, height_);
    }
}
```

Increment `render_generation_` before starting new render to cancel in-flight work.

---

## Recursive Boundary Algorithm

1. Draw boundary pixels of rectangle
2. If all boundary pixels same color → flood fill interior
3. Otherwise → subdivide into 4 quadrants, recurse
4. Base case: small regions computed pixel-by-pixel

---

## Frame Loop

```
Each frame:
  ├─ Handle resize → triggers re-render
  ├─ Upload pixels to back, swap to front (skip if suppress_texture_updates_)
  ├─ Draw texture_front_ at (display_offset_, display_scale_)
  ├─ Handle input (drag/zoom/double-click)
  └─ Check render completion → upload final, reset state
```

---

## Constants

```cpp
constexpr float start_threshold = 1.0f;    // Offset to trigger render (idle)
constexpr float restart_threshold = 64.0f; // Offset to restart render (busy)
constexpr FloatType zoom_step_ = 0.5;      // Zoom base = 1.5
```

---

## Summary

| Feature | Mechanism |
|---------|-----------|
| Smooth panning | `display_offset_` shifts texture; old content during render |
| Smooth zooming | `display_scale_` scales texture; old content during render |
| No flickering | Double-buffered textures with swap |
| Atomic updates | Suppress progressive updates during interaction |
| Offset preservation | Subtract `render_start_offset_` on completion |
| Interruptible | Generation counter invalidates stale tasks |
| Fast rendering | Boundary-trace with flood fill, parallel ThreadPool |
