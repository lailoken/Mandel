#include "mandel_ui.hpp"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "config.hpp"
#include "mandel.hpp"
#include "overscan_viewport.hpp"

#ifdef _DEBUG
#define DEBUG_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_PRINTF(...) ((void)0)
#endif

namespace mandel
{

MandelUI::MandelUI(TextureUpdateFunc update_func, TextureDeleteFunc delete_func, ThreadPool* thread_pool)
    : thread_pool_(thread_pool)
    , update_texture_func_(update_func)
    , delete_texture_func_(delete_func)
    , overscan_viewport_(800, 800)
    , texture_front_(0)
    , texture_back_(0)
    , display_offset_x_(0.0f)
    , display_offset_y_(0.0f)
    , render_start_offset_x_(0.0f)
    , render_start_offset_y_(0.0f)
    , render_start_canvas_x_min_(0.0L)
    , render_start_canvas_x_max_(0.0L)
    , render_start_canvas_y_min_(0.0L)
    , render_start_canvas_y_max_(0.0L)
    , displayed_texture_canvas_x_min_(0.0L)
    , displayed_texture_canvas_x_max_(0.0L)
    , displayed_texture_canvas_y_min_(0.0L)
    , displayed_texture_canvas_y_max_(0.0L)
    , is_dragging_(false)
    , suppress_texture_updates_(false)
    , processed_generation_(0)
    , display_scale_(1.0f)
    , zoom_center_x_(0.0f)
    , zoom_center_y_(0.0f)
    , render_generation_(0)
    , canvas_x_min_(MandelbrotRenderer::default_x_min)
    , canvas_x_max_(MandelbrotRenderer::default_x_max)
    , canvas_y_min_(MandelbrotRenderer::default_y_min)
    , canvas_y_max_(MandelbrotRenderer::default_y_max)
    , worker_(nullptr)
    , has_pending_settings_(false)
    , initial_bounds_(MandelbrotRenderer::default_x_min, MandelbrotRenderer::default_x_max,
                      MandelbrotRenderer::default_y_min, MandelbrotRenderer::default_y_max, 512)
    , initial_zoom_(1)
    , control_(this)
    , max_iterations_(512)
{
    new_view_name_buffer_[0] = '\0';

    // Load saved views from file
    LoadedViews loaded = load_views_from_file();
    saved_views_ = loaded.saved_views;
    if (loaded.has_current_view)
    {
        DEBUG_PRINTF("[UI] Constructor: loaded current_view=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), max_iter=%d\n",
               loaded.current_view.x_min, loaded.current_view.x_max, 
               loaded.current_view.y_min, loaded.current_view.y_max, loaded.current_view.max_iterations);
        // Validate loaded bounds before using them
        // Check that bounds are valid (min < max) and not all zeros
        // Check if ranges are valid (min < max)
        bool x_range_valid = loaded.current_view.x_min < loaded.current_view.x_max;
        bool y_range_valid = loaded.current_view.y_min < loaded.current_view.y_max;
        bool valid_ranges = x_range_valid && y_range_valid;
        
        // Check if values are effectively zero (within 1e-3 for safety)
        // Direct check: if all values are within epsilon of zero, reject
        // Use long double literal (L suffix) since FloatType is long double
        const FloatType zero_epsilon = 0.001L;
        FloatType abs_x_min = std::abs(loaded.current_view.x_min);
        FloatType abs_x_max = std::abs(loaded.current_view.x_max);
        FloatType abs_y_min = std::abs(loaded.current_view.y_min);
        FloatType abs_y_max = std::abs(loaded.current_view.y_max);
        bool effectively_zero = (abs_x_min < zero_epsilon && 
                                abs_x_max < zero_epsilon &&
                                abs_y_min < zero_epsilon && 
                                abs_y_max < zero_epsilon);
        
        DEBUG_PRINTF("[UI] Constructor: x_range_valid=%d, y_range_valid=%d, valid_ranges=%d, effectively_zero=%d (abs: %.20Lf, %.20Lf, %.20Lf, %.20Lf, epsilon=%.20Lf)\n",
               x_range_valid ? 1 : 0, y_range_valid ? 1 : 0, valid_ranges ? 1 : 0, effectively_zero ? 1 : 0,
               abs_x_min, abs_x_max, abs_y_min, abs_y_max, zero_epsilon);
        
#ifdef _DEBUG
        // Debug: check each comparison
        bool check1 = abs_x_min < zero_epsilon;
        bool check2 = abs_x_max < zero_epsilon;
        bool check3 = abs_y_min < zero_epsilon;
        bool check4 = abs_y_max < zero_epsilon;
        DEBUG_PRINTF("[UI] Constructor: individual checks: %d, %d, %d, %d\n", check1 ? 1 : 0, check2 ? 1 : 0, check3 ? 1 : 0, check4 ? 1 : 0);
#endif
        
        // Always reject if ranges are invalid OR values are effectively zero
        // The effectively_zero check should catch the (0,0,0,0) case
        // Also force reject if all values are exactly 0.0 (direct comparison with long double)
        bool force_reject_zeros = (loaded.current_view.x_min == 0.0L && loaded.current_view.x_max == 0.0L &&
                                  loaded.current_view.y_min == 0.0L && loaded.current_view.y_max == 0.0L);
        DEBUG_PRINTF("[UI] Constructor: force_reject_zeros=%d\n", force_reject_zeros ? 1 : 0);
        
        if (!valid_ranges || effectively_zero || force_reject_zeros)
        {
            DEBUG_PRINTF("[UI] Constructor: WARNING - loaded bounds are invalid (valid_ranges=%d, effectively_zero=%d), using defaults\n",
                   valid_ranges ? 1 : 0, effectively_zero ? 1 : 0);
            // Don't overwrite initial_bounds_ - it was already set to defaults in member initializer
        }
        else
        {
            // Keep initial_bounds_ as defaults - don't overwrite it
            // The loaded view will be applied via applied_settings_ and canvas bounds in init()
            DEBUG_PRINTF("[UI] Constructor: Loaded bounds are valid, will use for current view (initial_bounds_ stays as defaults)\n");
        }
    }
    else
    {
        DEBUG_PRINTF("[UI] Constructor: No current_view loaded, using defaults\n");
    }

    // Overscan viewport is already initialized in constructor

    // Initialize with initial bounds or loaded bounds
    // Re-load views to get current_view (we already loaded it in constructor, but need it here)
    LoadedViews loaded_again = load_views_from_file();
    if (loaded_again.has_current_view && !(loaded_again.current_view.x_min == 0.0L && loaded_again.current_view.x_max == 0.0L &&
                                      loaded_again.current_view.y_min == 0.0L && loaded_again.current_view.y_max == 0.0L) &&
        loaded_again.current_view.x_min < loaded_again.current_view.x_max && loaded_again.current_view.y_min < loaded_again.current_view.y_max)
    {
        // Use loaded bounds for current view (but keep initial_bounds_ as defaults)
        // Loaded bounds are viewport bounds, need to convert to canvas bounds
        applied_settings_ = loaded_again.current_view;
        max_iterations_ = loaded_again.current_view.max_iterations;
        
        // Convert viewport bounds to canvas bounds (add overscan margins)
        convert_viewport_to_canvas_bounds(loaded_again.current_view.x_min, loaded_again.current_view.x_max,
                                          loaded_again.current_view.y_min, loaded_again.current_view.y_max,
                                          canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
        DEBUG_PRINTF("[UI] init: Using loaded bounds for current view (viewport bounds converted to canvas)\n");
    }
    else
    {
        // Use default initial bounds
        applied_settings_ = initial_bounds_;
        max_iterations_ = initial_bounds_.max_iterations;
        
        // Convert viewport bounds to canvas bounds (add overscan margins)
        convert_viewport_to_canvas_bounds(initial_bounds_.x_min, initial_bounds_.x_max,
                                          initial_bounds_.y_min, initial_bounds_.y_max,
                                          canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
        DEBUG_PRINTF("[UI] init: Using default initial bounds (viewport bounds converted to canvas)\n");
    }
    DEBUG_PRINTF("[UI] init: initial_bounds (defaults)=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), max_iter=%d\n",
           initial_bounds_.x_min, initial_bounds_.x_max, initial_bounds_.y_min, initial_bounds_.y_max, initial_bounds_.max_iterations);
    DEBUG_PRINTF("[UI] init: canvas bounds set to=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
           canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);

    // Create initial worker
    worker_ = std::make_unique<MandelWorker>(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(),
                                             render_generation_,
                                             canvas_x_min_, canvas_x_max_,
                                             canvas_y_min_, canvas_y_max_,
                                             *thread_pool_);
    worker_->init(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
    worker_->set_max_iterations(max_iterations_);

    // Initialize displayed texture bounds to current canvas bounds
    displayed_texture_canvas_x_min_ = canvas_x_min_;
    displayed_texture_canvas_x_max_ = canvas_x_max_;
    displayed_texture_canvas_y_min_ = canvas_y_min_;
    displayed_texture_canvas_y_max_ = canvas_y_max_;

    // Start initial render
    start_render();
}

MandelUI::~MandelUI()
{
    // Save current view before exiting
    ViewState current_viewport = get_viewport_bounds();
    current_viewport.max_iterations = max_iterations_;
    save_views_to_file(saved_views_, &current_viewport);
    
    if (delete_texture_func_)
    {
        if (texture_front_ != 0)
        {
            delete_texture_func_(texture_front_);
        }
        if (texture_back_ != 0)
        {
            delete_texture_func_(texture_back_);
        }
    }
}

// Removed - now handled by OverscanViewport

void MandelUI::convert_viewport_to_canvas_bounds(FloatType viewport_x_min, FloatType viewport_x_max, FloatType viewport_y_min,
                                                  FloatType viewport_y_max, FloatType& canvas_x_min, FloatType& canvas_x_max,
                                                  FloatType& canvas_y_min, FloatType& canvas_y_max) const
{
    // Convert viewport bounds to canvas bounds by extending by margin in complex coords
    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    FloatType pixel_to_x = viewport_x_range / static_cast<FloatType>(overscan_viewport_.viewport_width());
    FloatType pixel_to_y = viewport_y_range / static_cast<FloatType>(overscan_viewport_.viewport_height());

    DEBUG_PRINTF("[UI] convert_viewport_to_canvas_bounds: viewport=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), viewport_size=(%d, %d), margin=(%d, %d), pixel_to=(%.20Lf, %.20Lf)\n",
           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
           overscan_viewport_.viewport_width(), overscan_viewport_.viewport_height(), 
           overscan_viewport_.margin_x(), overscan_viewport_.margin_y(), pixel_to_x, pixel_to_y);

    canvas_x_min = viewport_x_min - pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    canvas_x_max = viewport_x_max + pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    canvas_y_min = viewport_y_min - pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    canvas_y_max = viewport_y_max + pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    
    DEBUG_PRINTF("[UI] convert_viewport_to_canvas_bounds: result canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
           canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max);
}

void MandelUI::convert_canvas_to_viewport_bounds(FloatType canvas_x_min, FloatType canvas_x_max, FloatType canvas_y_min,
                                                 FloatType canvas_y_max, FloatType& viewport_x_min, FloatType& viewport_x_max,
                                                 FloatType& viewport_y_min, FloatType& viewport_y_max) const
{
    // Convert canvas bounds to viewport bounds by shrinking by margin in complex coords
    FloatType canvas_x_range = canvas_x_max - canvas_x_min;
    FloatType canvas_y_range = canvas_y_max - canvas_y_min;
    FloatType pixel_to_x = canvas_x_range / static_cast<FloatType>(overscan_viewport_.canvas_width());
    FloatType pixel_to_y = canvas_y_range / static_cast<FloatType>(overscan_viewport_.canvas_height());

    DEBUG_PRINTF("[UI] convert_canvas_to_viewport_bounds: canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), canvas_size=(%d, %d), margin=(%d, %d), pixel_to=(%.20Lf, %.20Lf)\n",
           canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max,
           overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(), 
           overscan_viewport_.margin_x(), overscan_viewport_.margin_y(), pixel_to_x, pixel_to_y);

    viewport_x_min = canvas_x_min + pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    viewport_x_max = canvas_x_max - pixel_to_x * static_cast<FloatType>(overscan_viewport_.margin_x());
    viewport_y_min = canvas_y_min + pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    viewport_y_max = canvas_y_max - pixel_to_y * static_cast<FloatType>(overscan_viewport_.margin_y());
    
    DEBUG_PRINTF("[UI] convert_canvas_to_viewport_bounds: result viewport=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max);
}

void MandelUI::handle_resize(int viewport_width, int viewport_height)
{
    if (viewport_width == overscan_viewport_.viewport_width() && viewport_height == overscan_viewport_.viewport_height())
    {
        return;  // No change
    }

    DEBUG_PRINTF("[UI] handle_resize: old=(%d, %d), new=(%d, %d), current canvas bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
           overscan_viewport_.viewport_width(), overscan_viewport_.viewport_height(), viewport_width, viewport_height,
           canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);

    // Update overscan viewport
    overscan_viewport_.set_viewport_size(viewport_width, viewport_height);

    // Get current viewport bounds (no lock needed - UI reads freely)
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);

    // Convert back to canvas bounds with new margins
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    // Update canvas bounds (no lock needed - UI updates freely, workers capture snapshot)
    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;

    // Recreate worker with new dimensions
    worker_ = std::make_unique<MandelWorker>(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(),
                                             render_generation_,
                                             canvas_x_min_, canvas_x_max_,
                                             canvas_y_min_, canvas_y_max_,
                                             *thread_pool_);
    worker_->init(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
    worker_->set_max_iterations(max_iterations_);

    // Reset display state
    display_offset_x_ = 0.0f;
    display_offset_y_ = 0.0f;
    display_scale_ = 1.0f;
    render_start_offset_x_ = 0.0f;
    render_start_offset_y_ = 0.0f;

    // Start new render
    start_render();
}

void MandelUI::start_render()
{
    // Note: Don't reset processed_generation_ here - it tracks the last processed generation
    
    // Track bounds at render start - these are what the texture will be rendered for
    render_start_canvas_x_min_ = canvas_x_min_;
    render_start_canvas_x_max_ = canvas_x_max_;
    render_start_canvas_y_min_ = canvas_y_min_;
    render_start_canvas_y_max_ = canvas_y_max_;
    
    // Update displayed texture bounds to current canvas bounds before starting render
    // (the texture that's currently displayed was rendered for these bounds)
    // BUT: Don't overwrite if handle_pan() already saved it (for swap calculation)
    // Check if displayed_texture_canvas is uninitialized (all zeros) or matches current canvas
    // If it matches current canvas, it means this is a fresh render (not after a pan)
    bool is_fresh_render = (displayed_texture_canvas_x_min_ == canvas_x_min_ &&
                           displayed_texture_canvas_x_max_ == canvas_x_max_ &&
                           displayed_texture_canvas_y_min_ == canvas_y_min_ &&
                           displayed_texture_canvas_y_max_ == canvas_y_max_);
    
    if (is_fresh_render || (displayed_texture_canvas_x_min_ == 0.0L && displayed_texture_canvas_x_max_ == 0.0L &&
                            displayed_texture_canvas_y_min_ == 0.0L && displayed_texture_canvas_y_max_ == 0.0L))
    {
        displayed_texture_canvas_x_min_ = canvas_x_min_;
        displayed_texture_canvas_x_max_ = canvas_x_max_;
        displayed_texture_canvas_y_min_ = canvas_y_min_;
        displayed_texture_canvas_y_max_ = canvas_y_max_;
    }
    
    if (thread_pool_)
    {
        thread_pool_->resume();
    }

    worker_->start_render();
}

void MandelUI::handle_zoom(float zoom_factor, float mouse_x, float mouse_y)
{
    // Get current viewport bounds (no lock needed - UI reads freely)
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    DEBUG_PRINTF("[UI] handle_zoom: canvas bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n", 
           canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);

    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    DEBUG_PRINTF("[UI] handle_zoom: viewport bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), range=(%.20Lf, %.20Lf), zoom_factor=%f\n",
           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
           viewport_x_range, viewport_y_range, zoom_factor);

    // Convert mouse position to complex coordinates
    FloatType mouse_x_ratio = mouse_x / static_cast<FloatType>(overscan_viewport_.viewport_width());
    FloatType mouse_y_ratio = mouse_y / static_cast<FloatType>(overscan_viewport_.viewport_height());
    FloatType mouse_x_complex = viewport_x_min + viewport_x_range * mouse_x_ratio;
    FloatType mouse_y_complex = viewport_y_min + viewport_y_range * mouse_y_ratio;

    // Calculate new viewport bounds centered on mouse
    FloatType new_range_x = viewport_x_range / static_cast<FloatType>(zoom_factor);
    FloatType new_range_y = viewport_y_range / static_cast<FloatType>(zoom_factor);

    FloatType new_viewport_x_min = mouse_x_complex - new_range_x * mouse_x_ratio;
    FloatType new_viewport_x_max = mouse_x_complex + new_range_x * (1.0 - mouse_x_ratio);
    FloatType new_viewport_y_min = mouse_y_complex - new_range_y * mouse_y_ratio;
    FloatType new_viewport_y_max = mouse_y_complex + new_range_y * (1.0 - mouse_y_ratio);

    // Convert to canvas bounds
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(new_viewport_x_min, new_viewport_x_max, new_viewport_y_min, new_viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    // Increase generation FIRST (causes renders to bail out)
    render_generation_.fetch_add(1);

    // Wait for worker to complete and get buffer
    std::vector<unsigned char> back_buffer;
    DEBUG_PRINTF("[UI] Calling wait_and_get_buffer...\n");
    if (worker_->wait_and_get_buffer(back_buffer))
    {
        DEBUG_PRINTF("[UI] wait_and_get_buffer returned true, buffer size=%zu\n", back_buffer.size());
        // Upload old image to front texture
        if (update_texture_func_ && !back_buffer.empty())
        {
            DEBUG_PRINTF("[UI] Uploading texture: size=%zu, width=%d, height=%d\n", 
                   back_buffer.size(), overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
            update_texture_func_(&texture_front_, back_buffer.data(), overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
            // Update displayed texture bounds - the texture in texture_front_ was rendered for the canvas bounds
            // that were current when the render started (the OLD canvas bounds, before zoom)
            // We need to save the old bounds before we update canvas_* below
            displayed_texture_canvas_x_min_ = canvas_x_min_;
            displayed_texture_canvas_x_max_ = canvas_x_max_;
            displayed_texture_canvas_y_min_ = canvas_y_min_;
            displayed_texture_canvas_y_max_ = canvas_y_max_;
            DEBUG_PRINTF("[UI] Texture uploaded successfully\n");
        }
        else
        {
            DEBUG_PRINTF("[UI] WARNING: update_texture_func_=%p or back_buffer.empty()=%d\n", 
                   reinterpret_cast<void*>(update_texture_func_), back_buffer.empty());
        }
    }
    else
    {
        DEBUG_PRINTF("[UI] wait_and_get_buffer returned false\n");
    }

    // Update canvas bounds (no lock needed - UI updates freely, workers capture snapshot)
    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;

    // Create new worker for new generation
    DEBUG_PRINTF("[UI] Destroying old worker and creating new one\n");
    worker_ = std::make_unique<MandelWorker>(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(),
                                             render_generation_,
                                             canvas_x_min_, canvas_x_max_,
                                             canvas_y_min_, canvas_y_max_,
                                             *thread_pool_);
    DEBUG_PRINTF("[UI] New worker created\n");
    worker_->init(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
    worker_->set_max_iterations(max_iterations_);

    // Reset display state
    display_offset_x_ = 0.0f;
    display_offset_y_ = 0.0f;
    display_scale_ = 1.0f;
    render_start_offset_x_ = 0.0f;
    render_start_offset_y_ = 0.0f;

    // Update applied settings and save current view
    ViewState new_viewport = get_viewport_bounds();
    new_viewport.max_iterations = max_iterations_;
    applied_settings_ = new_viewport;
    has_pending_settings_ = false;
    save_views_to_file(saved_views_, &new_viewport);

    // Start new render
    start_render();
}

void MandelUI::handle_pan(float delta_x, float delta_y)
{
                printf("[PAN] handle_pan: delta=(%.2f, %.2f), current_display_offset=(%.2f, %.2f)\n", 
                       delta_x, delta_y, display_offset_x_, display_offset_y_);
    
    // Get current viewport bounds (no lock needed - UI reads freely)
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);

                printf("[PAN] handle_pan: current viewport=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max);

    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    FloatType screen_to_x = viewport_x_range / static_cast<FloatType>(overscan_viewport_.viewport_width());
    FloatType screen_to_y = viewport_y_range / static_cast<FloatType>(overscan_viewport_.viewport_height());

    // Convert screen delta to complex plane delta
    // delta_x positive = drag right = pan left (decrease x)
    // delta_y positive = drag down = pan up (increase y, since screen Y is inverted)
    FloatType pan_x = -delta_x * screen_to_x;
    FloatType pan_y = -delta_y * screen_to_y;  // Invert Y: drag down (positive delta_y) should pan up (increase y)

                    printf("[PAN] handle_pan: pan in complex plane=(%.20Lf, %.20Lf)\n", pan_x, pan_y);

    viewport_x_min += pan_x;
    viewport_x_max += pan_x;
    viewport_y_min += pan_y;
    viewport_y_max += pan_y;

                    printf("[PAN] handle_pan: new viewport=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                           viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max);

    // Convert to canvas bounds
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

                    printf("[PAN] handle_pan: new canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                           new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    // Increase generation FIRST (causes renders to bail out)
    render_generation_.fetch_add(1);

    // Don't wait - start render immediately in background to avoid stutter
    // The render will complete asynchronously and we'll swap when ready
    
    // CRITICAL: Before updating canvas bounds, save the current bounds as displayed_texture_canvas
    // This represents the bounds of the texture that's currently being displayed
    // When we swap later, we need to compare this (old displayed texture) with render_start_canvas (new texture)
    displayed_texture_canvas_x_min_ = canvas_x_min_;
    displayed_texture_canvas_x_max_ = canvas_x_max_;
    displayed_texture_canvas_y_min_ = canvas_y_min_;
    displayed_texture_canvas_y_max_ = canvas_y_max_;
    
                    printf("[PAN] handle_pan: saving current canvas as displayed_texture_canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                           displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
                           displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_);
    
    // Update canvas bounds (no lock needed - UI updates freely, workers capture snapshot)
    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;
    
    // Track bounds at render start - these are what the texture will be rendered for
    render_start_canvas_x_min_ = new_canvas_x_min;
    render_start_canvas_x_max_ = new_canvas_x_max;
    render_start_canvas_y_min_ = new_canvas_y_min;
    render_start_canvas_y_max_ = new_canvas_y_max;
    
                    printf("[PAN] handle_pan: render_start_canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                           render_start_canvas_x_min_, render_start_canvas_x_max_, render_start_canvas_y_min_, render_start_canvas_y_max_);
    
    // CRITICAL: display_offset should remain relative to the displayed texture (old texture)
    // The offset_delta was consumed by updating the bounds, so we need to adjust display_offset
    // to account for the fact that the bounds changed but we're still displaying the old texture.
    // We subtract delta because the pan consumed that amount of offset.
    display_offset_x_ -= delta_x;
    display_offset_y_ -= delta_y;
    
                    printf("[PAN] handle_pan: adjusted display_offset to (%.2f, %.2f), starting render\n", display_offset_x_, display_offset_y_);

    // Update applied settings and save current view
    ViewState new_viewport = get_viewport_bounds();
    new_viewport.max_iterations = max_iterations_;
    applied_settings_ = new_viewport;
    has_pending_settings_ = false;
    save_views_to_file(saved_views_, &new_viewport);

    // Update worker bounds (no need to recreate worker for panning)
    worker_->start_render();
}

void MandelUI::start_background_render(float offset_delta_x, float offset_delta_y)
{
    // Start rendering the entire canvas (viewport + overscan) at the new bounds in the background
    // This helps with continuous panning - render the new view before it's needed
    // When dragging far enough, this will render the visible image as well as overscan
    
    // Get current viewport bounds
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);

    FloatType viewport_x_range = viewport_x_max - viewport_x_min;
    FloatType viewport_y_range = viewport_y_max - viewport_y_min;
    FloatType screen_to_x = viewport_x_range / static_cast<FloatType>(overscan_viewport_.viewport_width());
    FloatType screen_to_y = viewport_y_range / static_cast<FloatType>(overscan_viewport_.viewport_height());

    // Convert screen delta to complex plane delta
    FloatType pan_x = -offset_delta_x * screen_to_x;
    FloatType pan_y = -offset_delta_y * screen_to_y;

    // Calculate new viewport bounds (shifted by the drag amount)
    viewport_x_min += pan_x;
    viewport_x_max += pan_x;
    viewport_y_min += pan_y;
    viewport_y_max += pan_y;

    // Convert to canvas bounds
    FloatType new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max;
    convert_viewport_to_canvas_bounds(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max,
                                       new_canvas_x_min, new_canvas_x_max, new_canvas_y_min, new_canvas_y_max);

    // Increase generation FIRST (causes renders to bail out)
    render_generation_.fetch_add(1);

    // Don't wait - start render immediately in the background
    // This allows continuous panning without perceived rendering delay
    
    // Update canvas bounds to new position
    canvas_x_min_ = new_canvas_x_min;
    canvas_x_max_ = new_canvas_x_max;
    canvas_y_min_ = new_canvas_y_min;
    canvas_y_max_ = new_canvas_y_max;

    // Start render for entire canvas at new bounds (viewport + overscan)
    worker_->start_render();
}

void MandelUI::handle_input()
{
    ImGuiIO& io = ImGui::GetIO();

    // Get the main viewport (full window)
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 viewport_pos = viewport->Pos;
    ImVec2 viewport_size = viewport->Size;

    // Check if mouse is over the viewport (not over control window)
    ImVec2 mouse_pos = io.MousePos;
    bool mouse_over_viewport = (mouse_pos.x >= viewport_pos.x && mouse_pos.x < viewport_pos.x + viewport_size.x &&
                                mouse_pos.y >= viewport_pos.y && mouse_pos.y < viewport_pos.y + viewport_size.y);

    // Check if control window is blocking
    bool control_window_hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow);

    // Panning (drag)
    if (io.MouseDown[0] && mouse_over_viewport && !control_window_hovered)
    {
        if (!is_dragging_)
        {
            // Start dragging
            is_dragging_ = true;
            render_start_offset_x_ = display_offset_x_;
            render_start_offset_y_ = display_offset_y_;
        }

        // Update display offset
        float old_display_offset_x = display_offset_x_;
        float old_display_offset_y = display_offset_y_;
        display_offset_x_ += io.MouseDelta.x;
        display_offset_y_ += io.MouseDelta.y;
        
                printf("[DRAG] display_offset: (%.2f, %.2f) -> (%.2f, %.2f), delta=(%.2f, %.2f)\n",
                       old_display_offset_x, old_display_offset_y, display_offset_x_, display_offset_y_,
                       io.MouseDelta.x, io.MouseDelta.y);

        // Check if we need to start a new render (threshold = 80% of margin)
        float start_threshold_x = static_cast<float>(overscan_viewport_.margin_x()) * 0.8f;
        float start_threshold_y = static_cast<float>(overscan_viewport_.margin_y()) * 0.8f;
        float offset_delta_x = display_offset_x_ - render_start_offset_x_;
        float offset_delta_y = display_offset_y_ - render_start_offset_y_;

                printf("[DRAG] offset_delta=(%.2f, %.2f), threshold=(%.2f, %.2f), render_start_offset=(%.2f, %.2f)\n",
                       offset_delta_x, offset_delta_y, start_threshold_x, start_threshold_y,
                       render_start_offset_x_, render_start_offset_y_);

        if (std::abs(offset_delta_x) > start_threshold_x || std::abs(offset_delta_y) > start_threshold_y)
        {
            // Suppress texture updates during interaction
            suppress_texture_updates_ = true;

            // Handle pan - this will start rendering and reset display_offset to 0
            handle_pan(offset_delta_x, offset_delta_y);

            // After handle_pan, display_offset is reset to 0, so update render_start_offset accordingly
            render_start_offset_x_ = 0.0f;
            render_start_offset_y_ = 0.0f;
        }
        else
        {
            // Even if below threshold, start rendering missing overscan areas proactively
            // This helps with continuous panning - render areas that will be needed soon
            // Only do this if not already rendering to avoid unnecessary work
            if (!is_render_in_progress() && (std::abs(io.MouseDelta.x) > 0.1f || std::abs(io.MouseDelta.y) > 0.1f))
            {
                // Start rendering the entire canvas at the new bounds in the background
                start_background_render(offset_delta_x, offset_delta_y);
            }
        }
    }
    else
    {
        if (is_dragging_)
        {
            // End dragging
            is_dragging_ = false;
        }
    }

    // Zooming (mouse wheel) - doubles/halves
    if (io.MouseWheel != 0.0f && mouse_over_viewport && !control_window_hovered)
    {
        // Calculate zoom factor (double or halve)
        float zoom_factor = (io.MouseWheel > 0.0f) ? 2.0f : 0.5f;

        // Set zoom center to mouse position (relative to viewport)
        zoom_center_x_ = mouse_pos.x - viewport_pos.x;
        zoom_center_y_ = mouse_pos.y - viewport_pos.y;

        // Suppress texture updates during interaction
        suppress_texture_updates_ = true;

        // Handle zoom
        handle_zoom(zoom_factor, zoom_center_x_, zoom_center_y_);
    }

    // Double-click to reset zoom
    if (io.MouseDoubleClicked[0] && mouse_over_viewport && !control_window_hovered)
    {
        reset_to_initial();
    }
}

void MandelUI::update_textures()
{
    if (suppress_texture_updates_)
    {
        DEBUG_PRINTF("[UI] update_textures: suppressed\n");
        return;
    }

    const unsigned char* pixels = worker_->get_pixels();
    if (pixels == nullptr)
    {
        DEBUG_PRINTF("[UI] update_textures: pixels is null\n");
        return;
    }

    // Upload to back texture
    if (update_texture_func_)
    {
        DEBUG_PRINTF("[UI] update_textures: Uploading texture, size=%d x %d, texture_back_=%llu\n",
               overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(), static_cast<unsigned long long>(texture_back_));
        update_texture_func_(&texture_back_, pixels, overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
        DEBUG_PRINTF("[UI] update_textures: Texture uploaded\n");
    }
    else
    {
        DEBUG_PRINTF("[UI] update_textures: update_texture_func_ is null\n");
    }

    // Swap front and back
    std::swap(texture_front_, texture_back_);
    
    // Update displayed texture bounds to reflect the texture that's now in texture_front_
    // The texture in texture_back_ (now texture_front_ after swap) was rendered for render_start_canvas_* bounds
    displayed_texture_canvas_x_min_ = render_start_canvas_x_min_;
    displayed_texture_canvas_x_max_ = render_start_canvas_x_max_;
    displayed_texture_canvas_y_min_ = render_start_canvas_y_min_;
    displayed_texture_canvas_y_max_ = render_start_canvas_y_max_;
    
    DEBUG_PRINTF("[UI] update_textures: Swapped textures, texture_front_=%llu\n", static_cast<unsigned long long>(texture_front_));
}

void MandelUI::draw()
{
    ImGuiIO& io = ImGui::GetIO();

    // Handle resize
    if (io.DisplaySize.x != static_cast<float>(overscan_viewport_.viewport_width()) ||
        io.DisplaySize.y != static_cast<float>(overscan_viewport_.viewport_height()))
    {
        handle_resize(static_cast<int>(io.DisplaySize.x), static_cast<int>(io.DisplaySize.y));
    }

    // Handle input (drag, zoom, double-click)
    handle_input();

    // Check render completion - only process once per render
    bool render_in_progress = is_render_in_progress();
    unsigned int current_gen = render_generation_.load();
    
    // CRITICAL: Only process if render is complete AND we haven't processed this generation yet
    // This prevents old renders from being processed after new ones have started
    if (!render_in_progress && processed_generation_ != current_gen)
    {
        // CRITICAL: Check worker's start_generation BEFORE waiting for buffer
        // If the worker's generation doesn't match current, it's a stale render - skip it
        unsigned int worker_start_gen = worker_->get_start_generation();
        if (worker_start_gen != current_gen)
        {
            DEBUG_PRINTF("[UI] draw: Skipping stale render (worker_gen=%u, current_gen=%u)\n", worker_start_gen, current_gen);
            // Mark this generation as processed to prevent retrying
            processed_generation_ = current_gen;
        }
        else
        {
            // Render completed - merge buffer and upload texture (only once)
            // wait_and_get_buffer will also check generation internally
            std::vector<unsigned char> completed_buffer;
            if (worker_->wait_and_get_buffer(completed_buffer))
        {
            DEBUG_PRINTF("[UI] draw: Render completed, buffer merged, size=%zu\n", completed_buffer.size());
            // Buffer is now in canvas, update_textures() will read it
            
            if (suppress_texture_updates_)
            {
                printf("[SWAP] Texture swap: display_offset=(%.2f, %.2f), render_start_canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       display_offset_x_, display_offset_y_,
                       render_start_canvas_x_min_, render_start_canvas_x_max_,
                       render_start_canvas_y_min_, render_start_canvas_y_max_);
                printf("[SWAP] Texture swap: current_canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
                printf("[SWAP] Texture swap: displayed_texture_canvas=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
                       displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_);
                
                // CRITICAL: The new texture was rendered with bounds captured at render start
                // (render_start_canvas_x_min_, etc.). These bounds correspond to display_offset=0.
                // To preserve the point under the mouse, we need to:
                // 1. Calculate the difference between current canvas bounds and render_start bounds
                // 2. Convert this difference to screen pixels (display_offset adjustment)
                // 3. Adjust display_offset to account for this difference
                // 4. Restore bounds to render_start_canvas_* (the bounds the texture was rendered for)
                // This ensures the texture aligns with the bounds it was rendered for, and display_offset
                // correctly represents the visual offset needed to show the current view.
                
                // Calculate viewport bounds for render_start (the new texture being swapped in)
                FloatType render_start_viewport_x_min, render_start_viewport_x_max, render_start_viewport_y_min, render_start_viewport_y_max;
                convert_canvas_to_viewport_bounds(render_start_canvas_x_min_, render_start_canvas_x_max_,
                                                   render_start_canvas_y_min_, render_start_canvas_y_max_,
                                                   render_start_viewport_x_min, render_start_viewport_x_max,
                                                   render_start_viewport_y_min, render_start_viewport_y_max);
                
                // Calculate viewport range for render_start
                FloatType viewport_x_range = render_start_viewport_x_max - render_start_viewport_x_min;
                FloatType viewport_y_range = render_start_viewport_y_max - render_start_viewport_y_min;
                
                // CRITICAL: Calculate viewport_delta using displayed_texture_canvas (what's currently shown)
                // NOT current_canvas (which may have changed further). The delta should be between
                // the displayed texture and the new texture being swapped in.
                FloatType displayed_viewport_x_min, displayed_viewport_x_max, displayed_viewport_y_min, displayed_viewport_y_max;
                
                // Safety check: If displayed_texture_canvas_* is all zeros (not initialized), use render_start_canvas_* instead
                bool use_displayed_texture = (displayed_texture_canvas_x_min_ != 0.0L || displayed_texture_canvas_x_max_ != 0.0L ||
                                              displayed_texture_canvas_y_min_ != 0.0L || displayed_texture_canvas_y_max_ != 0.0L);
                
                if (use_displayed_texture)
                {
                    // Get viewport bounds for the currently displayed texture
                    convert_canvas_to_viewport_bounds(displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
                                                       displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_,
                                                       displayed_viewport_x_min, displayed_viewport_x_max,
                                                       displayed_viewport_y_min, displayed_viewport_y_max);
                    printf("[SWAP] Using displayed_texture_canvas for viewport_delta calculation\n");
                }
                else
                {
                    // Fallback: Use render_start_canvas_* (the texture being swapped in)
                    // This means the currently displayed texture has the same bounds as the new texture
                    displayed_viewport_x_min = render_start_viewport_x_min;
                    displayed_viewport_x_max = render_start_viewport_x_max;
                    displayed_viewport_y_min = render_start_viewport_y_min;
                    displayed_viewport_y_max = render_start_viewport_y_max;
                    printf("[SWAP] WARNING: displayed_texture_canvas is zero, using render_start_canvas for delta\n");
                }
                
                printf("[SWAP] displayed_viewport=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       displayed_viewport_x_min, displayed_viewport_x_max, displayed_viewport_y_min, displayed_viewport_y_max);
                
                // Safety check: avoid division by zero
                if (viewport_x_range <= 0.0L || viewport_y_range <= 0.0L)
                {
                    printf("[SWAP] ERROR: Invalid viewport range (x_range=%.20Lf, y_range=%.20Lf), skipping offset adjustment\n",
                           viewport_x_range, viewport_y_range);
                    // Just restore bounds without adjusting offset
                    canvas_x_min_ = render_start_canvas_x_min_;
                    canvas_x_max_ = render_start_canvas_x_max_;
                    canvas_y_min_ = render_start_canvas_y_min_;
                    canvas_y_max_ = render_start_canvas_y_max_;
                }
                else
                {
                    // Calculate display_offset adjustment to preserve mouse coordinate
                    float adjusted_display_offset_x = display_offset_x_;
                    float adjusted_display_offset_y = display_offset_y_;
                    calculate_swap_display_offset_adjustment(adjusted_display_offset_x, adjusted_display_offset_y);
                    
                    display_offset_x_ = adjusted_display_offset_x;
                    display_offset_y_ = adjusted_display_offset_y;
                    
                    float old_display_offset_x = adjusted_display_offset_x;
                    float old_display_offset_y = adjusted_display_offset_y;
                    printf("[SWAP] display_offset adjustment: (%.2f, %.2f) -> (%.2f, %.2f)\n",
                           old_display_offset_x, old_display_offset_y, display_offset_x_, display_offset_y_);
                    
                    // Restore bounds to what the texture was rendered for
                    canvas_x_min_ = render_start_canvas_x_min_;
                    canvas_x_max_ = render_start_canvas_x_max_;
                    canvas_y_min_ = render_start_canvas_y_min_;
                    canvas_y_max_ = render_start_canvas_y_max_;
                }
                
                printf("[SWAP] Restored canvas to render_start bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
                       canvas_x_min_, canvas_x_max_, canvas_y_min_, canvas_y_max_);
                printf("[SWAP] Adjusted display_offset to (%.2f, %.2f)\n",
                       display_offset_x_, display_offset_y_);
                
                // Upload final texture (this will swap textures)
                // NOTE: update_textures() will update displayed_texture_canvas_* to render_start_canvas_*
                update_textures();

                // Reset zoom scale
                display_scale_ = 1.0f;

                // Re-enable texture updates
                suppress_texture_updates_ = false;
                
                printf("[SWAP] Texture swap complete\n");
            }
            else
            {
                // Just update textures normally
                update_textures();
            }
        }
            else
            {
                DEBUG_PRINTF("[UI] draw: Render completed but wait_and_get_buffer returned false (stale render)\n");
            }
            
            // Mark this generation as processed (even if wait_and_get_buffer returned false)
            // This prevents retrying stale renders
            processed_generation_ = current_gen;
        }
    }

    // Draw the Mandelbrot texture
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();

    if (texture_front_ != 0)
    {
        // Use OverscanViewport to calculate all drawing parameters
        OverscanViewport::DrawInfo draw_info = overscan_viewport_.calculate_draw_info(
            viewport->Pos.x, viewport->Pos.y,
            display_offset_x_, display_offset_y_,
            display_scale_,
            zoom_center_x_, zoom_center_y_);
        
        // Draw grey background for the entire viewport area
        ImVec2 viewport_min(viewport->Pos.x, viewport->Pos.y);
        ImVec2 viewport_max(viewport->Pos.x + static_cast<float>(overscan_viewport_.viewport_width()), 
                           viewport->Pos.y + static_cast<float>(overscan_viewport_.viewport_height()));
        draw_list->AddRectFilled(viewport_min, viewport_max, IM_COL32(64, 64, 64, 255));
        
        // Draw the texture
        float uv_width = draw_info.uv_max_x - draw_info.uv_min_x;
        float uv_height = draw_info.uv_max_y - draw_info.uv_min_y;
        
        if (uv_width > 0.0f && uv_height > 0.0f)
        {
            float texture_draw_x = draw_info.draw_x + draw_info.texture_offset_x;
            float texture_draw_y = draw_info.draw_y + draw_info.texture_offset_y;
            
            ImVec2 texture_min(texture_draw_x, texture_draw_y);
            ImVec2 texture_max(texture_draw_x + draw_info.texture_width, texture_draw_y + draw_info.texture_height);
            
            ImVec2 uv_min(draw_info.uv_min_x, draw_info.uv_min_y);
            ImVec2 uv_max(draw_info.uv_max_x, draw_info.uv_max_y);
            
            draw_list->AddImage(texture_front_, texture_min, texture_max, uv_min, uv_max);
            
            DEBUG_PRINTF("[UI] draw: Drawing texture_front_=%llu at (%.1f, %.1f) to (%.1f, %.1f), size=(%.1f, %.1f), UV=(%.3f,%.3f) to (%.3f,%.3f)\n",
                   static_cast<unsigned long long>(texture_front_), texture_draw_x, texture_draw_y,
                   texture_draw_x + draw_info.texture_width, texture_draw_y + draw_info.texture_height, 
                   draw_info.texture_width, draw_info.texture_height, draw_info.uv_min_x, draw_info.uv_min_y, draw_info.uv_max_x, draw_info.uv_max_y);
        }
        else
        {
            DEBUG_PRINTF("[UI] draw: No valid texture region to draw (UV width=%.3f, height=%.3f)\n", uv_width, uv_height);
        }
    }
    else
    {
        DEBUG_PRINTF("[UI] draw: texture_front_ is 0, not drawing\n");
    }

    // Draw control window
    control_.draw();
}

// MandelUIControlInterface implementation

ViewState MandelUI::get_viewport_bounds() const
{
    FloatType viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max;
    convert_canvas_to_viewport_bounds(canvas_x_min_, canvas_x_max_,
                                       canvas_y_min_, canvas_y_max_,
                                       viewport_x_min, viewport_x_max,
                                       viewport_y_min, viewport_y_max);
    return ViewState(viewport_x_min, viewport_x_max, viewport_y_min, viewport_y_max, max_iterations_);
}

bool MandelUI::is_render_in_progress() const
{
    // Check thread pool status - renderer writes directly to canvas
    if (thread_pool_)
    {
        return !thread_pool_->is_idle();
    }
    return false;
}

int MandelUI::get_max_iterations() const
{
    return max_iterations_;
}

unsigned int MandelUI::get_render_generation() const
{
    return render_generation_.load();
}

bool MandelUI::is_dragging() const
{
    return is_dragging_;
}


bool MandelUI::is_rendering() const
{
    return is_render_in_progress();
}

ViewState MandelUI::get_applied_settings() const
{
    return applied_settings_;
}

ViewState MandelUI::get_pending_settings() const
{
    return pending_settings_;
}

bool MandelUI::has_pending_settings() const
{
    return has_pending_settings_;
}

ViewState MandelUI::get_initial_bounds() const
{
    return initial_bounds_;
}

uint64_t MandelUI::get_initial_zoom() const
{
    return initial_zoom_;
}

void MandelUI::set_pending_settings(const ViewState& settings)
{
    pending_settings_ = settings;
    has_pending_settings_ = true;
}

void MandelUI::apply_view_state(const ViewState& state)
{
    DEBUG_PRINTF("[UI] apply_view_state: state=(%.20Lf, %.20Lf, %.20Lf, %.20Lf), max_iter=%d\n",
           state.x_min, state.x_max, state.y_min, state.y_max, state.max_iterations);
    
    // Convert viewport bounds to canvas bounds
    FloatType canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max;
    convert_viewport_to_canvas_bounds(state.x_min, state.x_max, state.y_min, state.y_max,
                                       canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max);

    DEBUG_PRINTF("[UI] apply_view_state: converted canvas bounds=(%.20Lf, %.20Lf, %.20Lf, %.20Lf)\n",
           canvas_x_min, canvas_x_max, canvas_y_min, canvas_y_max);

    // Update atomic canvas bounds
    canvas_x_min_ = canvas_x_min;
    canvas_x_max_ = canvas_x_max;
    canvas_y_min_ = canvas_y_min;
    canvas_y_max_ = canvas_y_max;

    max_iterations_ = state.max_iterations;

    // Increase generation to cancel any in-progress renders
    render_generation_.fetch_add(1);

    // Recreate worker
    DEBUG_PRINTF("[UI] apply_view_state: Creating new worker\n");
    worker_ = std::make_unique<MandelWorker>(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height(),
                                             render_generation_,
                                             canvas_x_min_, canvas_x_max_,
                                             canvas_y_min_, canvas_y_max_,
                                             *thread_pool_);
    worker_->init(overscan_viewport_.canvas_width(), overscan_viewport_.canvas_height());
    worker_->set_max_iterations(max_iterations_);

    // Reset display state
    display_offset_x_ = 0.0f;
    display_offset_y_ = 0.0f;
    display_scale_ = 1.0f;
    render_start_offset_x_ = 0.0f;
    render_start_offset_y_ = 0.0f;

    // Update applied settings
    applied_settings_ = state;
    has_pending_settings_ = false;

    // Save current view to persist it
    ViewState current_viewport = get_viewport_bounds();
    current_viewport.max_iterations = max_iterations_;
    save_views_to_file(saved_views_, &current_viewport);

    // Start render
    DEBUG_PRINTF("[UI] apply_view_state: Starting render\n");
    start_render();
}

void MandelUI::save_view_state(const std::string& name)
{
    ViewState viewport = get_viewport_bounds();
    saved_views_[name] = viewport;
    save_views_to_file(saved_views_);
}

void MandelUI::extend_bounds_for_overscan(FloatType& x_min, FloatType& x_max, FloatType& y_min, FloatType& y_max) const
{
    // This is already handled internally, but provide the conversion for external use
    convert_viewport_to_canvas_bounds(x_min, x_max, y_min, y_max, x_min, x_max, y_min, y_max);
}

void MandelUI::apply_pending_settings_if_ready()
{
    if (!has_pending_settings_ || is_render_in_progress())
    {
        return;
    }

    apply_view_state(pending_settings_);
}

void MandelUI::reset_to_initial()
{
    apply_view_state(initial_bounds_);
}

std::map<std::string, ViewState>& MandelUI::get_saved_views()
{
    return saved_views_;
}

char* MandelUI::get_new_view_name_buffer()
{
    return new_view_name_buffer_;
}

void MandelUI::calculate_swap_display_offset_adjustment(float& display_offset_x, float& display_offset_y) const
{
    // Get viewport bounds for both textures
    FloatType displayed_viewport_x_min, displayed_viewport_x_max, displayed_viewport_y_min, displayed_viewport_y_max;
    FloatType render_start_viewport_x_min, render_start_viewport_x_max, render_start_viewport_y_min, render_start_viewport_y_max;
    
    // Check if we have a valid displayed texture (not first render)
    bool has_displayed_texture = (displayed_texture_canvas_x_min_ != 0.0L || displayed_texture_canvas_x_max_ != 0.0L ||
                                  displayed_texture_canvas_y_min_ != 0.0L || displayed_texture_canvas_y_max_ != 0.0L);
    
    if (!has_displayed_texture)
    {
        return;  // First render - no adjustment needed
    }
    
    convert_canvas_to_viewport_bounds(displayed_texture_canvas_x_min_, displayed_texture_canvas_x_max_,
                                     displayed_texture_canvas_y_min_, displayed_texture_canvas_y_max_,
                                     displayed_viewport_x_min, displayed_viewport_x_max,
                                     displayed_viewport_y_min, displayed_viewport_y_max);
    
    convert_canvas_to_viewport_bounds(render_start_canvas_x_min_, render_start_canvas_x_max_,
                                       render_start_canvas_y_min_, render_start_canvas_y_max_,
                                       render_start_viewport_x_min, render_start_viewport_x_max,
                                       render_start_viewport_y_min, render_start_viewport_y_max);
    
    // Calculate viewport ranges
    FloatType displayed_viewport_x_range = displayed_viewport_x_max - displayed_viewport_x_min;
    FloatType displayed_viewport_y_range = displayed_viewport_y_max - displayed_viewport_y_min;
    FloatType render_start_viewport_x_range = render_start_viewport_x_max - render_start_viewport_x_min;
    FloatType render_start_viewport_y_range = render_start_viewport_y_max - render_start_viewport_y_min;
    
    int viewport_width = overscan_viewport_.viewport_width();
    int viewport_height = overscan_viewport_.viewport_height();
    
    // display_offset is relative to the displayed texture (old texture) bounds
    // Find mouse complex coordinate using old displayed texture bounds
    FloatType mouse_screen_x = static_cast<FloatType>(viewport_width) / 2.0L + static_cast<FloatType>(display_offset_x);
    FloatType mouse_screen_y = static_cast<FloatType>(viewport_height) / 2.0L + static_cast<FloatType>(display_offset_y);
    
    // Convert screen position to complex coordinate in old texture space
    FloatType mouse_complex_x = displayed_viewport_x_min + (mouse_screen_x / static_cast<FloatType>(viewport_width)) * displayed_viewport_x_range;
    FloatType mouse_complex_y = displayed_viewport_y_min + (mouse_screen_y / static_cast<FloatType>(viewport_height)) * displayed_viewport_y_range;
    
    // Convert complex coordinate to screen position in new texture space
    FloatType new_mouse_screen_x = ((mouse_complex_x - render_start_viewport_x_min) / render_start_viewport_x_range) * static_cast<FloatType>(viewport_width);
    FloatType new_mouse_screen_y = ((mouse_complex_y - render_start_viewport_y_min) / render_start_viewport_y_range) * static_cast<FloatType>(viewport_height);
    
    // Calculate display_offset for new texture (offset from center)
    display_offset_x = static_cast<float>(new_mouse_screen_x) - static_cast<float>(viewport_width) / 2.0f;
    display_offset_y = static_cast<float>(new_mouse_screen_y) - static_cast<float>(viewport_height) / 2.0f;
}

}  // namespace mandel
