# Rendering Architecture

## Design Goals

1. **Buttery smooth interaction** - Dragging and zooming feel instant
2. **Progressive refinement** - Early bail when user interaction while rendering
3. **No visual glitches** - No tearing, flickering, or jarring transitions
4. **Thread Safety** - No race conditions, no dangling pointers, no memory corruption

---

## System Overview

The system renders the Mandelbrot set with smooth, responsive interaction. Users can drag to pan and scroll to zoom. The point under the mouse cursor must remain fixed in Mandelbrot coordinate space throughout all interactions, even when buffers swap.

### Key Behaviors

- **Dragging**: Image follows mouse smoothly, no jumps or stutters
- **Zooming**: Image zooms in/out centered on mouse position
- **Continuous rendering**: Worker always has next frame rendering in background
- **Coordinate preservation**: Mouse coordinate in Mandelbrot space never jumps during buffer swaps

---

## Components

### Renderer
- Calculates Mandelbrot set using recursive boundary-tracing + flood fill
- Uses high-precision floating point for deep zoom support
- Parallelizes computation across multiple threads
- Creates and owns its own pixel buffer
- Can be cancelled mid-render (generation check)
- Provides completed buffer to worker when done

### Worker
- Owns main canvas buffer
- Creates renderer instances for each render
- Waits for renderer completion, then merges renderer's buffer into canvas
- Thread-safe merge operation (only one merge at a time)

### Viewport Manager
- Manages viewport (visible area) and canvas (viewport + overscan)
- Calculates overscan margins (~1/6 viewport on each side)
- Converts between viewport bounds and canvas bounds
- Calculates texture coordinates and drawing parameters
- Handles display offset for smooth dragging
- Detects when dragging past overscan (shows grey areas)

### UI Controller
- Manages double-buffered GPU textures (front/back)
- Handles user input (drag, zoom, double-click)
- Manages display offset for smooth interaction
- Coordinates rendering and buffer swaps
- Ensures mouse coordinate preservation during swaps

---

## Double-Buffered Rendering

```
Renderer completes → Worker merges → Upload to back texture → Swap to front → Display
```

Never modify displayed texture directly. Always update back texture, then swap to front.

---

## Overscan

To enable smooth panning without immediate re-rendering, render a larger area than visible.

- **Viewport**: Visible screen area
- **Overscan Margin**: ~1/6 of viewport on each side
- **Canvas**: Viewport + 2× margin on each side

### Drawing Past Overscan

When dragging past overscan bounds:
- Show grey background in areas without valid texture data
- Clamp texture coordinates to valid range
- Adjust texture size and position to show valid area correctly

---

## Dragging

### Behavior

1. **Accumulate offset**: As user drags, accumulate display offset (mouse movement)
2. **Always render**: Worker continuously renders next frame in background (overscan or viewable area)
3. **Update bounds**: When offset exceeds threshold, update canvas bounds to new position
4. **Preserve coordinate**: When buffer swaps, adjust display offset to keep mouse coordinate fixed

### Requirements

- **No jumps**: Point under mouse must remain same Mandelbrot coordinate throughout dragging
- **Smooth motion**: Image follows mouse without stutter
- **Continuous rendering**: Worker always busy rendering next frame
- **Correct swap**: Buffer swap must account for difference between old and new texture bounds

### How Coordinate Preservation Works

When a render completes and buffers swap:
1. Calculate difference between old displayed texture bounds and new texture bounds
2. Convert this difference to screen pixels
3. Adjust display offset by this amount
4. This ensures the mouse coordinate in Mandelbrot space remains fixed

---

## Synchronization & Thread Safety

### Core Principle

UI thread never blocks on backend operations. All rendering happens asynchronously.

### Bounds Updates

- UI thread is only writer of canvas bounds
- Workers capture bounds snapshot when render starts
- If bounds change mid-render, generation increment cancels stale render

### Pixel Writes

- Each renderer writes to its own buffer (no sharing)
- Worker merges completed buffers into main canvas (thread-safe)
- Worker validates generation before merge (drops stale buffers)
- All pixel writes happen asynchronously

### Generation-Based Cancellation

- Atomic generation counter tracks current render
- UI increments generation to cancel in-flight renders
- Renderers check generation periodically and bail out if mismatch
- Worker drops completed buffers if generation doesn't match

### Thread Safety Guarantees

- **No deadlocks**: Minimal locking, UI never acquires merge locks
- **No segfaults**: Clear buffer ownership, no shared memory
- **No race conditions**: Single writer for bounds, atomic generation, per-renderer buffers

---

## Rendering Algorithm

1. Draw boundary pixels of rectangle
2. If all boundary pixels same color → flood fill interior
3. Otherwise → subdivide into 4×4 quadrants, recurse
4. Base case: small regions computed pixel-by-pixel
5. All work parallelized across thread pool

---

## Frame Loop

```
Each frame:
  ├─ Check render completion → merge buffer, upload to back texture
  ├─ Swap textures if update needed
  ├─ Draw grey background + texture (via viewport manager)
  ├─ Handle input (drag/zoom/double-click)
  └─ Update display offset/scale for smooth interaction
```

---

## Summary

| Requirement | Behavior |
|-------------|----------|
| Smooth panning | Display offset shifts texture during drag |
| Smooth zooming | Display scale scales texture during zoom |
| No flickering | Double-buffered textures with swap |
| Mouse coordinate preservation | Adjust display offset on swap using viewport delta |
| Continuous rendering | Worker always renders next frame (overscan or viewable) |
| Interruptible | Generation counter cancels stale renders |
| Fast rendering | Boundary-trace with flood fill, parallel computation |
| Overscan | Canvas ~1.33x viewport for smooth panning without edges |
| Thread safety | Per-renderer buffers, minimal locking |
