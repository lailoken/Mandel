# Test Plan: Buffer Swap Mouse Coordinate Preservation

## Problem
When dragging the image, visual glitches occur during buffer swaps - the image jumps forward or backward in the direction of dragging. The mouse coordinate in Mandelbrot complex space should remain fixed, but it's jumping.

## Root Cause Analysis

The issue occurs during the buffer swap sequence when a render completes while the user is dragging. The sequence is:

1. **Initial State**: User is viewing texture A with bounds `displayed_texture_canvas_*_`
2. **User Drags**: `display_offset_*_` accumulates as user drags
3. **Threshold Reached**: `handle_pan()` is called:
   - Saves `displayed_texture_canvas_*_` = current `canvas_*_` (texture A bounds)
   - Updates `canvas_*_` to new bounds (pan applied)
   - Sets `render_start_canvas_*_` = new `canvas_*_` (texture B will be rendered for these bounds)
   - Resets `display_offset_*_` = 0
4. **User Continues Dragging**: `display_offset_*_` accumulates again
5. **Render Completes**: Swap happens in `draw()`:
   - Calculates viewport delta between `displayed_texture_canvas_*_` (old texture A) and `render_start_canvas_*_` (new texture B)
   - Converts viewport delta to screen pixels: `screen_delta = viewport_delta * (viewport_size / render_start_viewport_range)`
   - Adjusts `display_offset_*_` += `screen_delta`
   - Restores `canvas_*_` = `render_start_canvas_*_`
   - Updates `displayed_texture_canvas_*_` = `render_start_canvas_*_`

## Test Strategy

### Test Case 1: Simple Drag and Swap
- Start with initial bounds
- User drags (display_offset accumulates)
- Threshold reached, handle_pan() called
- User continues dragging
- Render completes, swap happens
- Verify: Mouse coordinate in complex space unchanged

### Test Case 2: Multiple Pans Before Swap
- Start with initial bounds
- User drags, threshold reached, handle_pan() called (pan 1)
- User drags again, threshold reached, handle_pan() called (pan 2)
- User continues dragging
- Render completes, swap happens
- Verify: Mouse coordinate preserved

### Test Case 3: Real-World Failure Case
- Use actual values from logs that showed the bug
- Verify the test detects the jump

## Test Implementation Requirements

1. **Accurate State Simulation**: The test must simulate the exact sequence of operations from the real code
2. **Coordinate Verification**: Calculate mouse position in complex space before and after swap
3. **Tolerance**: Use small tolerance (0.0001) for floating point comparison
4. **Screen Jump Calculation**: Convert complex coordinate difference to screen pixels for user-visible jump

## Key Functions to Test

- `handle_pan()`: Must save `displayed_texture_canvas_*_` before updating `canvas_*_`
- Swap logic in `draw()`: Must correctly calculate viewport delta and adjust `display_offset_*_`

## Expected Behavior

After swap:
- `canvas_*_` = `render_start_canvas_*_` (bounds texture was rendered for)
- `display_offset_*_` = adjusted to preserve mouse coordinate
- Mouse coordinate in complex space = unchanged (within tolerance)

## Bug Detection

The test should FAIL if:
- Complex coordinate difference > tolerance (0.0001)
- Screen pixel jump > 0.1 pixels (user-visible)

The test should PASS if:
- Complex coordinate difference <= tolerance
- Mouse coordinate is preserved
