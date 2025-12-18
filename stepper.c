/***********************************************************************************************************************
PicoMite MMBasic

stepper.c

<COPYRIGHT HOLDERS>  Geoff Graham, Peter Mather
Copyright (c) 2021, <COPYRIGHT HOLDERS> All rights reserved.
Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
1.	Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
2.	Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
    in the documentation and/or other materials provided with the distribution.
3.	The name MMBasic be used when referring to the interpreter in any documentation and promotional material and the original copyright message be displayed
    on the console at startup (additional copyright messages may be added).
4.	All advertising materials mentioning features or use of this software must display the following acknowledgement: This product includes software developed
    by the <copyright holder>.
5.	Neither the name of the <copyright holder> nor the names of its contributors may be used to endorse or promote products derived from this software
    without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDERS> AS IS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDERS> BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

************************************************************************************************************************/
/**
 * @cond
 * The following section will be excluded from the documentation.
 */

#include "MMBasic_Includes.h"
#include "Hardware_Includes.h"
#include "stepper.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include <stdarg.h>
#include <stdlib.h>
#include <hardware/sync.h>
#ifdef rp2350
// Forward declarations
static bool axis_is_configured(stepper_axis_t *axis);
static bool position_within_limits(stepper_axis_t *axis, float pos_mm);
static void cmd_stepper_home_axes(int argc, unsigned char **argv);

// Cornering (junction) settings
// Minimal implementation uses a fixed junction deviation. Future work can expose this via STEPPER command.
#define DEFAULT_JUNCTION_DEVIATION_MM 0.05f

// Planner position state (separate from ISR-updated current_pos)
// These track where the next planned move will start
static float planner_x = 0.0f;
static float planner_y = 0.0f;
static float planner_z = 0.0f;

// Global stepper system instance (must be defined before helpers that use it)
static stepper_system_t stepper_system = {0};

static inline void stepper_spindle_set(bool on)
{
    if (stepper_system.spindle_pin == 0xFF)
        return;

    // active-high by default; invert flips polarity
    bool level = on ? true : false;
    if (stepper_system.spindle_invert)
        level = !level;
    gpio_put(stepper_system.spindle_pin, level ? 1 : 0);
    stepper_system.spindle_on = on;
}

static inline void stepper_spindle_off(void)
{
    stepper_spindle_set(false);
}

// MMBasic error() does not return; ensure the spindle is made safe first.
static void stepper_error(const char *msg)
{
    stepper_spindle_off();
    error((char *)msg);
}

// Helper: convert step count to mm for an axis, including G92 workspace offset
float get_axis_position_mm(const stepper_axis_t *axis)
{
    if (axis && axis->steps_per_mm > 0.0f)
    {
        float hw_position = (float)axis->current_pos / axis->steps_per_mm;

        // Add G92 workspace offset
        if (axis == &stepper_system.x)
            return hw_position + stepper_system.x_g92_offset;
        else if (axis == &stepper_system.y)
            return hw_position + stepper_system.y_g92_offset;
        else if (axis == &stepper_system.z)
            return hw_position + stepper_system.z_g92_offset;

        return hw_position;
    }
    return 0.0f;
}

// Global G-code buffer instance (must be defined before any helper uses it)
static gcode_buffer_t gcode_buffer = {0};
static void gcode_buffer_release_all_payloads(gcode_buffer_t *buffer);
static void gcode_buffer_allocate(gcode_buffer_t *buffer, uint16_t size)
{
    if (size < 1 || size > GCODE_BUFFER_MAX_SIZE)
        stepper_error("Invalid buffer size");

    if (buffer->blocks != NULL)
    {
        FreeMemorySafe((void **)&buffer->blocks);
    }

    buffer->blocks = (stepper_runtime_block_t *)GetMemory((size_t)size * sizeof(stepper_runtime_block_t));
    buffer->size = size;
}

static void gcode_buffer_free(gcode_buffer_t *buffer)
{
    if (buffer->blocks != NULL)
    {
        gcode_buffer_release_all_payloads(buffer);
        FreeMemorySafe((void **)&buffer->blocks);
    }
    buffer->size = 0;
    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    buffer->buffer_full = false;
    buffer->buffer_empty = true;
}

static inline void stepper_pack_runtime_block(stepper_runtime_block_t *dst, const gcode_block_t *src)
{
    dst->type = src->type;
    dst->is_planned = src->is_planned;
    dst->allow_accumulator_carry = src->allow_accumulator_carry;
    dst->major_axis_mask = src->major_axis_mask;
    dst->x_steps_planned = src->x_steps_planned;
    dst->y_steps_planned = src->y_steps_planned;
    dst->z_steps_planned = src->z_steps_planned;
    dst->x_dir = src->x_dir;
    dst->y_dir = src->y_dir;
    dst->z_dir = src->z_dir;
    dst->major_steps = src->major_steps;
    dst->accel_steps = src->accel_steps;
    dst->cruise_steps = src->cruise_steps;
    dst->decel_steps = src->decel_steps;
    dst->entry_rate = src->entry_rate;
    dst->exit_rate = src->exit_rate;
    dst->cruise_rate = src->cruise_rate;
    dst->accel_increment = src->accel_increment;
    dst->use_scurve = src->use_scurve;
    dst->jerk_increment = src->jerk_increment;
    dst->initial_phase = src->initial_phase;
    dst->initial_accel_rate = src->initial_accel_rate;
    dst->scurve_ju_accel_steps = src->scurve_ju_accel_steps;
    dst->scurve_ca_steps = src->scurve_ca_steps;
    dst->scurve_jd_accel_steps = src->scurve_jd_accel_steps;
    dst->scurve_cruise_steps = src->scurve_cruise_steps;
    dst->scurve_ju_decel_steps = src->scurve_ju_decel_steps;
    dst->scurve_cd_steps = src->scurve_cd_steps;
    dst->scurve_jd_decel_steps = src->scurve_jd_decel_steps;
    dst->unit_x = src->unit_x;
    dst->unit_y = src->unit_y;
    dst->unit_z = src->unit_z;
    dst->virtual_steps_per_mm = src->virtual_steps_per_mm;
    dst->min_accel = src->min_accel;
    dst->distance = src->distance;
    dst->max_velocity = src->max_velocity;
    dst->arc_segment_count = src->arc_segment_count;
    dst->arc_segments = src->arc_segments;
}

static inline void stepper_unpack_runtime_block(gcode_block_t *dst, const stepper_runtime_block_t *src)
{
    *dst = (gcode_block_t){0};
    dst->type = src->type;
    dst->is_planned = src->is_planned;
    dst->allow_accumulator_carry = src->allow_accumulator_carry;
    dst->major_axis_mask = src->major_axis_mask;
    dst->x_steps_planned = src->x_steps_planned;
    dst->y_steps_planned = src->y_steps_planned;
    dst->z_steps_planned = src->z_steps_planned;
    dst->x_dir = src->x_dir;
    dst->y_dir = src->y_dir;
    dst->z_dir = src->z_dir;
    dst->major_steps = src->major_steps;
    dst->accel_steps = src->accel_steps;
    dst->cruise_steps = src->cruise_steps;
    dst->decel_steps = src->decel_steps;
    dst->entry_rate = src->entry_rate;
    dst->exit_rate = src->exit_rate;
    dst->cruise_rate = src->cruise_rate;
    dst->accel_increment = src->accel_increment;
    dst->use_scurve = src->use_scurve;
    dst->jerk_increment = src->jerk_increment;
    dst->initial_phase = src->initial_phase;
    dst->initial_accel_rate = src->initial_accel_rate;
    dst->scurve_ju_accel_steps = src->scurve_ju_accel_steps;
    dst->scurve_ca_steps = src->scurve_ca_steps;
    dst->scurve_jd_accel_steps = src->scurve_jd_accel_steps;
    dst->scurve_cruise_steps = src->scurve_cruise_steps;
    dst->scurve_ju_decel_steps = src->scurve_ju_decel_steps;
    dst->scurve_cd_steps = src->scurve_cd_steps;
    dst->scurve_jd_decel_steps = src->scurve_jd_decel_steps;
    dst->unit_x = src->unit_x;
    dst->unit_y = src->unit_y;
    dst->unit_z = src->unit_z;
    dst->virtual_steps_per_mm = src->virtual_steps_per_mm;
    dst->min_accel = src->min_accel;
    dst->distance = src->distance;
    dst->max_velocity = src->max_velocity;
    dst->arc_segment_count = src->arc_segment_count;
    dst->arc_segments = src->arc_segments;
}

static inline void stepper_release_runtime_block_payload(stepper_runtime_block_t *block)
{
    if (block->arc_segments != NULL)
    {
        FreeMemorySafe((void **)&block->arc_segments);
        block->arc_segment_count = 0;
    }
}

static void gcode_buffer_release_all_payloads(gcode_buffer_t *buffer)
{
    if (buffer->blocks == NULL || buffer->size == 0)
        return;

    uint16_t count = buffer->count;
    uint16_t idx = buffer->tail;
    while (count--)
    {
        stepper_release_runtime_block_payload(&buffer->blocks[idx]);
        idx = (uint16_t)((idx + 1) % buffer->size);
    }
}

// Exported: free slots remaining in G-code buffer (0..buffer_size)
// Read from MMBasic via MM.CODE.
volatile uint16_t stepper_gcode_buffer_space = 0;

// Runtime-configurable jerk limit for future S-curve motion (mm/s^3)
// Set via STEPPER JERK. Clamped via a hard limit derived from configured axes steps/mm.
static float stepper_jerk_limit_mm_s3 = 0.0f;

// Enable jerk-limited S-curve profiles for standalone G0/G1 blocks.
// Arc segments remain on the existing approach initially.
static bool stepper_scurve_enable = false;

// Buffer management functions

// Reset planner position (call when buffer is cleared or system initialized)
static void planner_reset_position(void)
{
    planner_x = 0.0f;
    planner_y = 0.0f;
    planner_z = 0.0f;
}

// Sync planner position to current physical position (derived from ISR-updated step counters)
// Planner works in workspace coordinates (hardware position + G92 offset)
static void planner_sync_to_physical(void)
{
    if (axis_is_configured(&stepper_system.x) && stepper_system.x.steps_per_mm > 0.0f)
        planner_x = (float)stepper_system.x.current_pos / stepper_system.x.steps_per_mm + stepper_system.x_g92_offset;
    else
        planner_x = 0.0f;

    if (axis_is_configured(&stepper_system.y) && stepper_system.y.steps_per_mm > 0.0f)
        planner_y = (float)stepper_system.y.current_pos / stepper_system.y.steps_per_mm + stepper_system.y_g92_offset;
    else
        planner_y = 0.0f;

    if (axis_is_configured(&stepper_system.z) && stepper_system.z.steps_per_mm > 0.0f)
        planner_z = (float)stepper_system.z.current_pos / stepper_system.z.steps_per_mm + stepper_system.z_g92_offset;
    else
        planner_z = 0.0f;
}

// Recompute trapezoid/triangle profile for a linear move block.
// Uses block->distance (mm), block->virtual_steps_per_mm, block->min_accel (mm/s^2),
// block->max_velocity (mm/s), and entry/exit rates.
static void compute_profile_for_block(gcode_block_t *block)
{
    // Default to trapezoid/triangle for this block
    block->use_scurve = false;
    block->jerk_increment = 0;
    block->scurve_ju_accel_steps = 0;
    block->scurve_ca_steps = 0;
    block->scurve_jd_accel_steps = 0;
    block->scurve_cruise_steps = 0;
    block->scurve_ju_decel_steps = 0;
    block->scurve_cd_steps = 0;
    block->scurve_jd_decel_steps = 0;

    if (block->major_steps <= 0)
    {
        block->is_planned = false;
        return;
    }

    if (block->distance < 0.001f)
        block->distance = 0.001f;

    if (block->virtual_steps_per_mm <= 0.0f)
        block->virtual_steps_per_mm = (float)block->major_steps / block->distance;

    if (block->min_accel < 0.001f)
        block->min_accel = 0.001f;

    float rate_to_vel = block->virtual_steps_per_mm * (float)RATE_SCALE;
    float v_entry = (rate_to_vel > 0.0f) ? ((float)block->entry_rate / rate_to_vel) : 0.0f;
    float v_exit = (rate_to_vel > 0.0f) ? ((float)block->exit_rate / rate_to_vel) : 0.0f;

    float v_max = block->max_velocity;
    if (v_max < v_entry)
        v_max = v_entry;
    if (v_max < v_exit)
        v_max = v_exit;

    // Ideal accel/decel distances (mm)
    float accel_dist = (v_max * v_max - v_entry * v_entry) / (2.0f * block->min_accel);
    float decel_dist = (v_max * v_max - v_exit * v_exit) / (2.0f * block->min_accel);
    if (accel_dist < 0.0f)
        accel_dist = 0.0f;
    if (decel_dist < 0.0f)
        decel_dist = 0.0f;

    // Default cruise rate for requested max velocity
    block->cruise_rate = (int32_t)(v_max * rate_to_vel);

    if (accel_dist + decel_dist > block->distance)
    {
        // Triangle profile: compute peak velocity
        float v_peak_sq = block->min_accel * block->distance + 0.5f * (v_entry * v_entry + v_exit * v_exit);
        if (v_peak_sq < 0.0f)
            v_peak_sq = 0.0f;
        float v_peak = sqrtf(v_peak_sq);
        block->cruise_rate = (int32_t)(v_peak * rate_to_vel);

        float accel_dist_peak = (v_peak * v_peak - v_entry * v_entry) / (2.0f * block->min_accel);
        float decel_dist_peak = (v_peak * v_peak - v_exit * v_exit) / (2.0f * block->min_accel);
        if (accel_dist_peak < 0.0f)
            accel_dist_peak = 0.0f;
        if (decel_dist_peak < 0.0f)
            decel_dist_peak = 0.0f;

        block->accel_steps = (int32_t)(accel_dist_peak * block->virtual_steps_per_mm);
        block->decel_steps = (int32_t)(decel_dist_peak * block->virtual_steps_per_mm);
        if (block->accel_steps < 0)
            block->accel_steps = 0;
        if (block->decel_steps < 0)
            block->decel_steps = 0;

        int32_t used = block->accel_steps + block->decel_steps;
        if (used > block->major_steps)
        {
            // Clamp for rounding
            block->accel_steps = block->major_steps / 2;
            block->decel_steps = block->major_steps - block->accel_steps;
        }
        block->cruise_steps = 0;
    }
    else
    {
        // Trapezoid profile
        block->accel_steps = (int32_t)(accel_dist * block->virtual_steps_per_mm);
        block->decel_steps = (int32_t)(decel_dist * block->virtual_steps_per_mm);
        if (block->accel_steps < 0)
            block->accel_steps = 0;
        if (block->decel_steps < 0)
            block->decel_steps = 0;

        int32_t used = block->accel_steps + block->decel_steps;
        if (used > block->major_steps)
        {
            // Clamp for rounding
            block->accel_steps = block->major_steps / 2;
            block->decel_steps = block->major_steps - block->accel_steps;
            block->cruise_steps = 0;
        }
        else
        {
            block->cruise_steps = block->major_steps - used;
        }
    }

    block->is_planned = true;

    // Precompute initial phase for trapezoid profile to optimize ISR
    if (block->accel_steps > 0)
        block->initial_phase = MOVE_PHASE_ACCEL;
    else if (block->cruise_steps > 0)
        block->initial_phase = MOVE_PHASE_CRUISE;
    else
        block->initial_phase = MOVE_PHASE_DECEL;
    block->initial_accel_rate = 0; // Not used for trapezoid, but initialize for consistency
}

// Compute jerk-limited S-curve phase distances (mm) for one side (accelerate from v0 to v1).
// Assumes v1 >= v0. Returns total distance (mm). Outputs per-phase distances.
static double scurve_side_distance(double v0, double v1, double a_max, double j_max,
                                   double *d_ju, double *d_ca, double *d_jd)
{
    *d_ju = 0.0;
    *d_ca = 0.0;
    *d_jd = 0.0;

    if (v1 <= v0)
        return 0.0;

    if (a_max <= 0.0)
        a_max = 0.001;
    if (j_max <= 0.0)
        j_max = 0.001;

    const double dv = v1 - v0;
    const double tj_full = a_max / j_max;
    const double dv_min = (a_max * a_max) / j_max;

    double tj;
    double ta;
    double a_peak;

    if (dv >= dv_min)
    {
        tj = tj_full;
        ta = (dv - dv_min) / a_max;
        a_peak = a_max;
    }
    else
    {
        tj = sqrt(dv / j_max);
        ta = 0.0;
        a_peak = j_max * tj;
    }

    // jerk-up
    *d_ju = v0 * tj + (1.0 / 6.0) * j_max * tj * tj * tj;

    // velocity after jerk-up
    const double v_mid1 = v0 + 0.5 * j_max * tj * tj;

    // constant accel (optional)
    if (ta > 0.0)
        *d_ca = v_mid1 * ta + 0.5 * a_peak * ta * ta;

    // velocity after constant accel
    const double v_mid2 = v_mid1 + a_peak * ta;

    // jerk-down back to zero accel
    *d_jd = v_mid2 * tj + 0.5 * a_peak * tj * tj - (1.0 / 6.0) * j_max * tj * tj * tj;

    return (*d_ju + *d_ca + *d_jd);
}

static double scurve_distance_needed(double v_entry, double v_exit, double v_peak,
                                     double a_max, double j_max)
{
    double d1, d2, d3;
    double d_acc = scurve_side_distance(v_entry, v_peak, a_max, j_max, &d1, &d2, &d3);
    double d_dec = scurve_side_distance(v_exit, v_peak, a_max, j_max, &d1, &d2, &d3);
    return d_acc + d_dec;
}

// Attempt to compute jerk-limited S-curve profile for a linear/rapid block.
// Returns true if S-curve profile was generated; false means caller should fall back to trapezoid.
static bool compute_scurve_profile_for_block(gcode_block_t *block)
{
    // Policy: only apply to standalone G0/G1 blocks.
    if (!(block->type == GCODE_LINEAR_MOVE || block->type == GCODE_RAPID_MOVE))
        return false;
    if (!stepper_scurve_enable)
        return false;
    if (stepper_jerk_limit_mm_s3 <= 0.0f)
        return false;

    if (block->major_steps <= 0)
        return false;
    if (block->distance < 0.001f)
        block->distance = 0.001f;
    if (block->virtual_steps_per_mm <= 0.0f)
        block->virtual_steps_per_mm = (float)block->major_steps / block->distance;

    const double sv = (double)block->virtual_steps_per_mm;
    const double rate_to_vel = sv * (double)RATE_SCALE;
    if (rate_to_vel <= 0.0)
        return false;

    double v_entry = (double)block->entry_rate / rate_to_vel;
    double v_exit = (double)block->exit_rate / rate_to_vel;
    if (v_entry < 0.0)
        v_entry = 0.0;
    if (v_exit < 0.0)
        v_exit = 0.0;

    double v_max = (double)block->max_velocity;
    if (v_max < v_entry)
        v_max = v_entry;
    if (v_max < v_exit)
        v_max = v_exit;

    double a_max = (double)block->min_accel;
    if (a_max < 0.001)
        a_max = 0.001;
    const double j_max = (double)stepper_jerk_limit_mm_s3;
    if (j_max <= 0.0)
        return false;

    // Convert jerk to integer jerk_increment for ISR: j_inc = j * steps/mm * RATE_SCALE / ISR_FREQ^2
    const double isr2 = (double)ISR_FREQ * (double)ISR_FREQ;
    const double j_inc_f = j_max * sv * (double)RATE_SCALE / isr2;
    if (j_inc_f < 1.0)
        return false;
    int32_t j_inc = (int32_t)(j_inc_f + 0.5);
    if (j_inc == 0)
        return false;

    // Ensure accel_increment (rate units/tick) is nonzero
    if (block->accel_increment == 0)
    {
        int32_t a_inc = (int32_t)(a_max * sv * (double)RATE_SCALE / (double)ISR_FREQ);
        if (a_inc == 0)
            a_inc = 1;
        block->accel_increment = a_inc;
    }

    const double dist = (double)block->distance;

    // Determine v_peak (mm/s)
    double v_low = (v_entry > v_exit) ? v_entry : v_exit;
    double v_high = v_max;
    if (v_high < v_low)
        v_high = v_low;

    double v_peak = v_high;
    const double need_at_max = scurve_distance_needed(v_entry, v_exit, v_high, a_max, j_max);
    if (need_at_max > dist)
    {
        // No cruise: binary search for v_peak
        for (int iter = 0; iter < 24; iter++)
        {
            double mid = 0.5 * (v_low + v_high);
            if (scurve_distance_needed(v_entry, v_exit, mid, a_max, j_max) > dist)
                v_high = mid;
            else
                v_low = mid;
        }
        v_peak = v_low;
    }

    // Compute per-phase distances for accel and decel sides
    double d_ju_a, d_ca_a, d_jd_a;
    double d_ju_d, d_cd_d, d_jd_d;
    double d_acc = scurve_side_distance(v_entry, v_peak, a_max, j_max, &d_ju_a, &d_ca_a, &d_jd_a);
    double d_dec = scurve_side_distance(v_exit, v_peak, a_max, j_max, &d_ju_d, &d_cd_d, &d_jd_d);
    double d_cruise = dist - (d_acc + d_dec);
    if (d_cruise < 0.0)
        d_cruise = 0.0;

    // Convert phase distances to step counts (major axis steps)
    int32_t s1 = (int32_t)(d_ju_a * sv + 0.5);
    int32_t s2 = (int32_t)(d_ca_a * sv + 0.5);
    int32_t s3 = (int32_t)(d_jd_a * sv + 0.5);
    int32_t s4 = (int32_t)(d_cruise * sv + 0.5);
    int32_t s5 = (int32_t)(d_ju_d * sv + 0.5);
    int32_t s6 = (int32_t)(d_cd_d * sv + 0.5);
    int32_t s7 = (int32_t)(d_jd_d * sv + 0.5);

    if (s1 < 0)
        s1 = 0;
    if (s2 < 0)
        s2 = 0;
    if (s3 < 0)
        s3 = 0;
    if (s4 < 0)
        s4 = 0;
    if (s5 < 0)
        s5 = 0;
    if (s6 < 0)
        s6 = 0;
    if (s7 < 0)
        s7 = 0;

    int32_t sum = s1 + s2 + s3 + s4 + s5 + s6 + s7;
    int32_t diff = block->major_steps - sum;
    if (diff != 0)
    {
        // Prefer adjusting cruise; otherwise adjust final segment
        if (s4 + diff >= 0)
            s4 += diff;
        else if (s7 + diff >= 0)
            s7 += diff;
        else
            return false; // Cannot fit S-curve in this move
    }

    if (s1 + s2 + s3 + s4 + s5 + s6 + s7 <= 0)
        return false;

    block->use_scurve = true;
    block->jerk_increment = j_inc;
    block->scurve_ju_accel_steps = s1;
    block->scurve_ca_steps = s2;
    block->scurve_jd_accel_steps = s3;
    block->scurve_cruise_steps = s4;
    block->scurve_ju_decel_steps = s5;
    block->scurve_cd_steps = s6;
    block->scurve_jd_decel_steps = s7;

    // For compatibility/diagnostics
    block->accel_steps = s1 + s2 + s3;
    block->cruise_steps = s4;
    block->decel_steps = s5 + s6 + s7;

    // Update cruise_rate to v_peak
    block->cruise_rate = (int32_t)(v_peak * rate_to_vel);
    block->is_planned = true;

    // Precompute initial phase and accel_rate to optimize ISR block loading
    // This eliminates the 7-level if-else chain from the ISR
    if (s1 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_JERK_UP_ACCEL;
        block->initial_accel_rate = 0;
    }
    else if (s2 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_CONST_ACCEL;
        block->initial_accel_rate = (int32_t)block->accel_increment;
    }
    else if (s3 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_JERK_DOWN_ACCEL;
        block->initial_accel_rate = (int32_t)block->accel_increment;
    }
    else if (s4 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_CRUISE;
        block->initial_accel_rate = 0;
    }
    else if (s5 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_JERK_UP_DECEL;
        block->initial_accel_rate = 0;
    }
    else if (s6 > 0)
    {
        block->initial_phase = MOVE_PHASE_S_CONST_DECEL;
        block->initial_accel_rate = -(int32_t)block->accel_increment;
    }
    else
    {
        block->initial_phase = MOVE_PHASE_S_JERK_DOWN_DECEL;
        block->initial_accel_rate = -(int32_t)block->accel_increment;
    }

    // Always initialize accel_rate to 0 for S-curve blocks (ISR expects this)
    return true;
}

// Recompute a block's motion profile after entry/exit changes (e.g. junction blending).
static void recompute_profile_for_block(gcode_block_t *block)
{
    // Clear any previous S-curve fields
    block->use_scurve = false;
    block->jerk_increment = 0;
    block->scurve_ju_accel_steps = 0;
    block->scurve_ca_steps = 0;
    block->scurve_jd_accel_steps = 0;
    block->scurve_cruise_steps = 0;
    block->scurve_ju_decel_steps = 0;
    block->scurve_cd_steps = 0;
    block->scurve_jd_decel_steps = 0;

    if (compute_scurve_profile_for_block(block))
        return;
    compute_profile_for_block(block);
}

// Minimal corner blending: set previous block exit_rate and this block entry_rate
// based on turn angle and accel limit, then recompute both trapezoids.
static inline void stepper_reset_accumulator_carry(void);
static void try_apply_junction_blend(gcode_block_t *next_block)
{
    if (!next_block->is_planned)
        return;
    if (!(next_block->type == GCODE_LINEAR_MOVE || next_block->type == GCODE_RAPID_MOVE))
        return;
    if (next_block->major_steps <= 0 || next_block->virtual_steps_per_mm <= 0.0f)
        return;

    // Atomically read, update, and write back the previous block
    uint32_t save = save_and_disable_interrupts();
    if (gcode_buffer.count < 2)
    {
        restore_interrupts(save);
        return;
    }
    uint16_t prev_idx = (gcode_buffer.size ? (uint16_t)((gcode_buffer.head + gcode_buffer.size - 1) % gcode_buffer.size) : 0);
    stepper_runtime_block_t prev_rt = gcode_buffer.blocks[prev_idx];
    gcode_block_t prev = {0};
    stepper_unpack_runtime_block(&prev, &prev_rt);

    if (!prev.is_planned ||
        !(prev.type == GCODE_LINEAR_MOVE || prev.type == GCODE_RAPID_MOVE) ||
        prev.major_steps <= 0 || prev.virtual_steps_per_mm <= 0.0f ||
        prev.exit_rate != 0 || next_block->entry_rate != 0)
    {
        restore_interrupts(save);
        return;
    }

    // Compute turn angle
    float dot = prev.unit_x * next_block->unit_x + prev.unit_y * next_block->unit_y + prev.unit_z * next_block->unit_z;
    if (dot > 1.0f)
        dot = 1.0f;
    if (dot < -1.0f)
        dot = -1.0f;

    float junction_v = 0.0f;
    if (dot > 0.999f)
    {
        // Almost straight
        junction_v = prev.max_velocity;
        if (next_block->max_velocity < junction_v)
            junction_v = next_block->max_velocity;
    }
    else if (dot < -0.999f)
    {
        // Reversal: come to a stop
        junction_v = 0.0f;
    }
    else
    {
        float sin_theta_d2 = sqrtf(0.5f * (1.0f - dot));
        if (sin_theta_d2 < 1e-6f)
        {
            junction_v = prev.max_velocity;
            if (next_block->max_velocity < junction_v)
                junction_v = next_block->max_velocity;
        }
        else
        {
            float a = prev.min_accel;
            if (next_block->min_accel < a)
                a = next_block->min_accel;
            float denom = (1.0f - sin_theta_d2);
            if (denom < 1e-6f)
                denom = 1e-6f;
            float r = DEFAULT_JUNCTION_DEVIATION_MM * sin_theta_d2 / denom;
            if (r < 0.0f)
                r = 0.0f;
            junction_v = sqrtf(a * r);
            if (junction_v > prev.max_velocity)
                junction_v = prev.max_velocity;
            if (junction_v > next_block->max_velocity)
                junction_v = next_block->max_velocity;
        }
    }

    int32_t prev_exit = (int32_t)(junction_v * prev.virtual_steps_per_mm * (float)RATE_SCALE);
    int32_t next_entry = (int32_t)(junction_v * next_block->virtual_steps_per_mm * (float)RATE_SCALE);
    if (prev_exit > prev.cruise_rate)
        prev_exit = prev.cruise_rate;
    if (next_entry > next_block->cruise_rate)
        next_entry = next_block->cruise_rate;

    prev.exit_rate = prev_exit;
    next_block->entry_rate = next_entry;

    // Recompute both profiles (prev now decels to exit_rate, next now accels from entry_rate)
    recompute_profile_for_block(&prev);
    recompute_profile_for_block(next_block);

    // Write previous block back only if it is still the previous block
    if (gcode_buffer.count >= 2 && gcode_buffer.size && prev_idx == (uint16_t)((gcode_buffer.head + gcode_buffer.size - 1) % gcode_buffer.size))
    {
        stepper_pack_runtime_block(&prev_rt, &prev);
        gcode_buffer.blocks[prev_idx] = prev_rt;
    }
    restore_interrupts(save);
}

void gcode_buffer_init(gcode_buffer_t *buffer)
{
    // Make reset safe even if called while the stepper ISR is running
    uint32_t save = save_and_disable_interrupts();

    gcode_buffer_release_all_payloads(buffer);

    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    buffer->buffer_full = false;
    buffer->buffer_empty = true;
    buffer->blocks_executed = 0;

    // Empty buffer => full space available
    stepper_gcode_buffer_space = buffer->size;

    // Initialize parser state
    buffer->current_motion_mode = GCODE_LINEAR_MOVE;
    buffer->current_feedrate = 0.0f;
    buffer->feedrate_set = false;    // No feedrate set yet
    buffer->absolute_mode = true;    // G90 by default
    buffer->exact_stop_mode = false; // G64 by default

    // Reset planner position
    planner_reset_position();

    stepper_reset_accumulator_carry();

    restore_interrupts(save);
}

// Check if buffer is full
bool gcode_buffer_is_full(gcode_buffer_t *buffer)
{
    return (buffer->size == 0) ? true : (buffer->count >= buffer->size);
}

// Check if buffer is empty
bool gcode_buffer_is_empty(gcode_buffer_t *buffer)
{
    return buffer->count == 0;
}

// Add a block to the buffer (ISR-safe)
// Disables stepper interrupt during modification to prevent race conditions
bool gcode_buffer_add(gcode_buffer_t *buffer, gcode_block_t *block)
{
    stepper_runtime_block_t packed;
    stepper_pack_runtime_block(&packed, block);
    block->arc_segment_count = 0;
    block->arc_segments = NULL;

    // Disable stepper interrupt during buffer modification
    uint32_t save = save_and_disable_interrupts();

    if (gcode_buffer_is_full(buffer))
    {
        stepper_release_runtime_block_payload(&packed);
        restore_interrupts(save);
        return false; // Buffer full
    }

    // Copy block to buffer
    buffer->blocks[buffer->head] = packed;

    // Update head pointer
    buffer->head = (buffer->size ? (uint16_t)((buffer->head + 1) % buffer->size) : 0);
    buffer->count++;
    buffer->buffer_empty = false;
    buffer->buffer_full = (buffer->size == 0) ? true : (buffer->count >= buffer->size);

    // Keep exported free-space indicator in sync (still IRQ-off)
    stepper_gcode_buffer_space = (buffer->size && buffer->count <= buffer->size) ? (uint16_t)(buffer->size - buffer->count) : 0;

    restore_interrupts(save);
    return true;
}

// Get the next block to execute (doesn't remove it)
stepper_runtime_block_t *gcode_buffer_peek(gcode_buffer_t *buffer)
{
    if (gcode_buffer_is_empty(buffer))
    {
        return NULL;
    }

    return &buffer->blocks[buffer->tail];
}

// Remove the current block after execution
bool gcode_buffer_pop(gcode_buffer_t *buffer)
{
    if (gcode_buffer_is_empty(buffer))
    {
        return false;
    }

    stepper_release_runtime_block_payload(&buffer->blocks[buffer->tail]);
    // Update tail pointer
    buffer->tail = (buffer->size ? (uint16_t)((buffer->tail + 1) % buffer->size) : 0);
    buffer->count--;
    buffer->buffer_full = false;
    buffer->buffer_empty = (buffer->count == 0);
    buffer->blocks_executed++;

    // Keep exported free-space indicator in sync (ISR context)
    stepper_gcode_buffer_space = (buffer->size && buffer->count <= buffer->size) ? (uint16_t)(buffer->size - buffer->count) : 0;

    return true;
}

// Get available space in buffer (ISR-safe)
uint16_t gcode_buffer_available(gcode_buffer_t *buffer)
{
    uint32_t save = save_and_disable_interrupts();
    uint16_t available = (buffer->size && buffer->count <= buffer->size) ? (uint16_t)(buffer->size - buffer->count) : 0;
    restore_interrupts(save);
    return available;
}

// Example: Create a G01 linear move command
gcode_block_t create_linear_move(float x, float y, float z, float feedrate)
{
    gcode_block_t block = {0};

    block.type = GCODE_LINEAR_MOVE;
    block.x = x;
    block.y = y;
    block.z = z;
    block.has_x = true;
    block.has_y = true;
    block.has_z = true;
    block.feedrate = feedrate;
    block.is_planned = false;

    return block;
}

// Example: Create a G02 clockwise arc command
gcode_block_t create_cw_arc(float x, float y, float i, float j, float feedrate)
{
    gcode_block_t block = {0};

    block.type = GCODE_CW_ARC;
    block.x = x;
    block.y = y;
    block.has_x = true;
    block.has_y = true;
    block.i = i;
    block.j = j;
    block.use_radius = false;
    block.feedrate = feedrate;
    block.is_planned = false;

    return block;
}

// Stepper timer state
#define STEPPER_PWM_SLICE 10
// End-of-move guard: if decelerating to a full stop (exit_rate==0), integer rate quantization
// can make step_rate drop to ~0 before the final few steps are emitted, causing a very long
// "hang" near the target (or a complete stall if it reaches 0). For the last few steps only,
// clamp to a small minimum step rate so motion deterministically completes.
#define STEPPER_TAIL_FINISH_STEPS 200
#define STEPPER_TAIL_MIN_STEP_RATE 200000 // 200 steps/sec => 5ms per step
// Start-of-move guard: with extremely low acceleration, step_rate can ramp up from 0
// so slowly that the first step may take seconds (or minutes), appearing as "no movement".
// Apply a conservative minimum starting step rate for ACCEL phases only.
// Rate units: step frequency (steps/s) ~= step_rate / 1000 (given current constants).
#define STEPPER_START_MIN_STEP_RATE 20000  // ~20 steps/sec
volatile bool stepper_initialized = false; // Non-static for access from External.c
static volatile uint32_t stepper_tick_count = 0;

// Motion arming gate: when false, the ISR will not dequeue/execute buffered motion.
// This replaces the older TEST-mode mechanism.
static volatile bool stepper_armed = false;

// Per-axis hard step timing guard.
// Minimum allowed time between step pulses is derived from each axis' configured
// max velocity and steps/mm. If any axis is not ready for its next step, then
// all axis steps scheduled for that tick are deferred (maintains coordination).
static volatile uint16_t stepper_axis_min_step_ticks[3] = {1, 1, 1};
static volatile uint32_t stepper_axis_last_step_tick[3] = {0, 0, 0};

typedef struct
{
    bool valid;
    bool allow_carry;
    uint32_t accumulator;
    int32_t exit_rate;
    uint8_t major_axis_mask;
} stepper_accumulator_carry_t;

static volatile stepper_accumulator_carry_t stepper_accumulator_carry = {0};

static inline void stepper_reset_accumulator_carry(void)
{
    stepper_accumulator_carry.valid = false;
    stepper_accumulator_carry.allow_carry = false;
    stepper_accumulator_carry.accumulator = 0;
    stepper_accumulator_carry.exit_rate = 0;
    stepper_accumulator_carry.major_axis_mask = 0;
}

static inline uint16_t stepper_compute_axis_min_step_ticks(const stepper_axis_t *axis)
{
    if (axis == NULL)
        return 1;
    if (!axis_is_configured((stepper_axis_t *)axis))
        return 1;
    if (axis->steps_per_mm <= 0.0f || axis->max_velocity <= 0.0f)
        return 1;

    // max_step_freq = steps/mm * mm/s = steps/s
    double max_step_freq = (double)axis->steps_per_mm * (double)axis->max_velocity;
    if (max_step_freq <= 0.0)
        return 1;

    // min_ticks = ceil(ISR_FREQ / max_step_freq)
    double ticks_d = (double)ISR_FREQ / max_step_freq;
    uint32_t ticks = (uint32_t)ticks_d;
    if (ticks_d > (double)ticks)
        ticks++;
    if (ticks < 1u)
        ticks = 1u;
    if (ticks > 65535u)
        ticks = 65535u;
    return (uint16_t)ticks;
}

static void stepper_recompute_axis_step_tick_limits(void)
{
    // Safe to call in main context; values are consumed by ISR.
    // Caller should wrap in save_and_disable_interrupts() if updating during motion.
    stepper_axis_min_step_ticks[0] = stepper_compute_axis_min_step_ticks(&stepper_system.x);
    stepper_axis_min_step_ticks[1] = stepper_compute_axis_min_step_ticks(&stepper_system.y);
    stepper_axis_min_step_ticks[2] = stepper_compute_axis_min_step_ticks(&stepper_system.z);
}

// Latched hardware-limit trip (set in ISR, reported in main context)
static volatile bool stepper_limit_trip_latched = false;
static volatile uint64_t stepper_limit_trip_mask = 0; // subset of limit_switch_mask that is currently asserted

// Current move state (used by ISR)
static volatile stepper_move_t current_move = {0};

static inline void stepper_apply_direction_pins(void)
{
    if (current_move.x_active && stepper_system.x.step_pin != 0xFF)
    {
        bool dir_level = current_move.x_dir;
        if (stepper_system.x.dir_invert)
            dir_level = !dir_level;
        gpio_put(stepper_system.x.dir_pin, dir_level);
    }
    if (current_move.y_active && stepper_system.y.step_pin != 0xFF)
    {
        bool dir_level = current_move.y_dir;
        if (stepper_system.y.dir_invert)
            dir_level = !dir_level;
        gpio_put(stepper_system.y.dir_pin, dir_level);
    }
    if (current_move.z_active && stepper_system.z.step_pin != 0xFF)
    {
        bool dir_level = current_move.z_dir;
        if (stepper_system.z.dir_invert)
            dir_level = !dir_level;
        gpio_put(stepper_system.z.dir_pin, dir_level);
    }
}

static bool stepper_load_arc_segment(uint16_t index)
{
    if (!current_move.arc_active || current_move.arc_segments == NULL)
        return false;

    uint16_t count = current_move.arc_segment_count;
    while (index < count)
    {
        const volatile arc_segment_runtime_t *seg = &current_move.arc_segments[index];
        if (seg->major_steps <= 0)
        {
            index++;
            continue;
        }

        current_move.arc_segment_index = index;
        current_move.arc_segment_steps_remaining = seg->major_steps;
        current_move.major_steps = seg->major_steps;
        current_move.x_steps = seg->x_steps;
        current_move.y_steps = seg->y_steps;
        current_move.z_steps = seg->z_steps;
        current_move.x_active = (seg->x_steps > 0);
        current_move.y_active = (seg->y_steps > 0);
        current_move.z_active = (seg->z_steps > 0);
        current_move.x_dir = seg->x_dir;
        current_move.y_dir = seg->y_dir;
        current_move.z_dir = seg->z_dir;
        current_move.major_axis_mask = seg->major_axis_mask;
        current_move.x_error = seg->major_steps / 2;
        current_move.y_error = seg->major_steps / 2;
        current_move.z_error = seg->major_steps / 2;
        stepper_apply_direction_pins();
        return true;
    }

    current_move.arc_active = false;
    current_move.arc_segment_steps_remaining = 0;
    return false;
}

static inline void stepper_release_current_arc(void)
{
    if (current_move.arc_segments != NULL)
    {
        FreeMemorySafe((void **)&current_move.arc_segments);
    }
    current_move.arc_active = false;
    current_move.arc_segment_count = 0;
    current_move.arc_segment_index = 0;
    current_move.arc_segment_steps_remaining = 0;
}

// Arc linearization settings
#define DEFAULT_ARC_TOLERANCE_MM 0.5f               // Default maximum arc segment length in mm
#define DEFAULT_ARC_MAX_ANGULAR_DEVIATION_RAD 0.02f // Default angular deviation cap (radians)
#define DEFAULT_ARC_SEGMENT_CAP 128                 // Default hard ceiling on generated segments
#define ARC_SEGMENT_CAP_MAX 512                     // Safety clamp for configurable segment cap
#define MIN_ARC_SEGMENTS 4                          // Minimum segments for any arc

// Runtime-configurable arc policies
static float stepper_arc_tolerance_mm = DEFAULT_ARC_TOLERANCE_MM;
static float stepper_arc_max_angle_rad = DEFAULT_ARC_MAX_ANGULAR_DEVIATION_RAD;
static uint16_t stepper_arc_segment_cap = DEFAULT_ARC_SEGMENT_CAP;

// Force the stepper subsystem back to a known-safe state.
// Safe to call at any time; if STEPPER INIT has never been run then this is a no-op.
void stepper_recover_to_safe_state(void)
{
    if (!stepper_initialized)
        return;

    // Make auxiliary output safe.
    stepper_spindle_off();

    uint32_t save = save_and_disable_interrupts();

    // Stop any executing move immediately.
    current_move.phase = MOVE_PHASE_IDLE;
    current_move.step_rate = 0;
    current_move.cruise_rate = 0;
    current_move.exit_rate = 0;
    current_move.accel_increment = 0;
    current_move.step_accumulator = 0;
    current_move.total_steps_remaining = 0;
    current_move.steps_remaining = 0;
    stepper_reset_accumulator_carry();
    stepper_release_current_arc();

    stepper_system.motion_active = false;
    stepper_system.homing_active = false;

    // Clear buffer and reset modal state.
    gcode_buffer_init(&gcode_buffer);

    // Disarm so nothing resumes without explicit STEPPER RUN.
    stepper_armed = false;

    // Keep planner consistent with the current hardware position.
    planner_sync_to_physical();

    restore_interrupts(save);
}

// Error abort: place the subsystem into a safe, recoverable state without stopping the IRQ
// or discarding axis configuration.
// Intended for automatic use on runtime error termination.
// Resulting state:
// - Spindle off
// - Drivers disabled (if enable pins are configured)
// - G-code buffer cleared
// - Motion execution DISARMED (requires STEPPER RUN)
// - Position marked unknown (requires G28 or STEPPER POSITION)
void stepper_abort_to_safe_state_on_error(void)
{
    if (!stepper_initialized)
        return;

    // Make auxiliary output safe immediately.
    stepper_spindle_off();

    uint32_t save = save_and_disable_interrupts();

    // Stop any executing move immediately.
    current_move.phase = MOVE_PHASE_IDLE;
    current_move.step_rate = 0;
    current_move.cruise_rate = 0;
    current_move.exit_rate = 0;
    current_move.accel_increment = 0;
    current_move.step_accumulator = 0;
    current_move.total_steps_remaining = 0;
    current_move.steps_remaining = 0;
    stepper_reset_accumulator_carry();
    stepper_release_current_arc();

    stepper_system.motion_active = false;
    stepper_system.homing_active = false;

    // Disable drivers (active-low enable pins) to avoid holding position after an error.
    if (stepper_system.x.enable_pin != 0xFF)
        gpio_put(stepper_system.x.enable_pin, 1);
    if (stepper_system.y.enable_pin != 0xFF)
        gpio_put(stepper_system.y.enable_pin, 1);
    if (stepper_system.z.enable_pin != 0xFF)
        gpio_put(stepper_system.z.enable_pin, 1);

    // Clear buffer and reset modal state.
    gcode_buffer_init(&gcode_buffer);

    // Disarm so nothing resumes without explicit STEPPER RUN.
    stepper_armed = false;

    // After an error/abort we cannot assume the machine position is trustworthy.
    stepper_system.position_known = false;

    restore_interrupts(save);
}

// Fully shut down the stepper subsystem (stop timer IRQ and free resources).
// Safe to call at any time; if STEPPER INIT has never been run then this is a no-op.
void stepper_close_subsystem(void)
{
    if (!stepper_initialized)
        return;

    // Ensure spindle is off on shutdown.
    stepper_spindle_off();

    // Stop the ISR source first (and block it from running while we reset state).
    uint32_t save = save_and_disable_interrupts();

    pwm_set_enabled(STEPPER_PWM_SLICE, false);
    pwm_set_irq1_enabled(STEPPER_PWM_SLICE, false);
    irq_set_enabled(PWM_IRQ_WRAP_1, false);

    // Reset runtime state.
    current_move.phase = MOVE_PHASE_IDLE;
    current_move.step_rate = 0;
    current_move.cruise_rate = 0;
    current_move.exit_rate = 0;
    current_move.accel_increment = 0;
    current_move.step_accumulator = 0;
    current_move.total_steps_remaining = 0;
    current_move.steps_remaining = 0;
    stepper_reset_accumulator_carry();
    stepper_release_current_arc();

    stepper_armed = false;
    stepper_system.motion_active = false;
    stepper_system.homing_active = false;

    // Stepper offline => report no usable buffer space.
    stepper_gcode_buffer_space = 0;
    stepper_initialized = false;

    restore_interrupts(save);

    // Free G-code buffer memory after IRQ is disabled.
    gcode_buffer_free(&gcode_buffer);
}

// Arc linearization function
// Breaks G02/G03 arcs into small chords, bundles them as a single runtime payload,
// and queues one block so the entire arc accelerates once (accel, cruise, decel).
// Returns true if successful, false if buffer full
static inline int32_t mm_to_steps_rounded(float hw_mm, float steps_per_mm);
static inline uint8_t compute_major_axis_mask(int32_t major_steps,
                                              int32_t x_steps,
                                              int32_t y_steps,
                                              int32_t z_steps);

static bool plan_arc_move(float start_x, float start_y, float start_z,
                          float target_x, float target_y, float target_z,
                          float offset_i, float offset_j, float offset_k,
                          float radius, bool use_radius, bool is_clockwise,
                          float feedrate)
{
    // Arc is in X-Y plane (most common), Z moves linearly during arc
    // Center of arc
    float center_x, center_y;

    if (use_radius)
    {
        // Calculate center from radius
        float dx = target_x - start_x;
        float dy = target_y - start_y;
        float d = sqrtf(dx * dx + dy * dy);

        // Add small epsilon to account for floating-point rounding errors
        const float epsilon = 0.0001f;
        if (d > fabsf(radius) * 2.0f + epsilon)
            stepper_error("Arc radius too small for move");

        if (d < 0.001f)
            stepper_error("Arc start/end too close");

        // Guard against negative sqrt argument due to floating point rounding
        float h_squared = radius * radius - (d / 2.0f) * (d / 2.0f);
        if (h_squared < 0.0f)
            h_squared = 0.0f; // Clamp to zero - arc is essentially a straight line

        float h = sqrtf(h_squared);
        float mid_x = (start_x + target_x) / 2.0f;
        float mid_y = (start_y + target_y) / 2.0f;
        float perp_x = -dy / d;
        float perp_y = dx / d;

        if ((radius > 0) != is_clockwise)
            h = -h;

        center_x = mid_x + perp_x * h;
        center_y = mid_y + perp_y * h;
    }
    else
    {
        center_x = start_x + offset_i;
        center_y = start_y + offset_j;
    }

    // Calculate radius and angles
    float r_start = sqrtf((start_x - center_x) * (start_x - center_x) +
                          (start_y - center_y) * (start_y - center_y));
    float start_angle = atan2f(start_y - center_y, start_x - center_x);
    float end_angle = atan2f(target_y - center_y, target_x - center_x);
    float angular_travel = end_angle - start_angle;

    // Normalize angular travel
    if (is_clockwise)
    {
        if (angular_travel >= 0.0f)
            angular_travel -= 2.0f * (float)M_PI;
    }
    else
    {
        if (angular_travel <= 0.0f)
            angular_travel += 2.0f * (float)M_PI;
    }

    // Handle full circles
    if (fabsf(start_x - target_x) < 0.001f && fabsf(start_y - target_y) < 0.001f)
        angular_travel = is_clockwise ? -2.0f * (float)M_PI : 2.0f * (float)M_PI;

    // Calculate total arc length (including Z for helical arcs)
    float arc_xy_length = fabsf(angular_travel) * r_start;
    float z_travel = target_z - start_z;

    // Calculate number of segments
    float tol = stepper_arc_tolerance_mm;
    if (tol < 0.001f)
        tol = 0.001f;
    int segments = (int)(arc_xy_length / tol);
    if (segments < MIN_ARC_SEGMENTS)
        segments = MIN_ARC_SEGMENTS;

    float max_angle = stepper_arc_max_angle_rad;
    if (max_angle < 0.001f)
        max_angle = 0.001f;
    int angular_segments = (int)(fabsf(angular_travel) / max_angle);
    if (angular_segments > segments)
        segments = angular_segments;

    if (stepper_arc_segment_cap >= MIN_ARC_SEGMENTS && segments > (int)stepper_arc_segment_cap)
        segments = (int)stepper_arc_segment_cap;

    // Buffer handling policy for arcs:
    // - When armed, arc generation is allowed to block waiting for buffer space.
    //   This prevents large arcs from overflowing the buffer regardless of buffer size.
    // - When disarmed, the ISR will not consume blocks, so blocking would deadlock.
    //   In that case we still require sufficient free slots up-front.
    if (!stepper_armed)
    {
        int free_slots = (int)gcode_buffer_available(&gcode_buffer);
        if (free_slots < segments)
        {
            static char msg[160];
            snprintf(msg, sizeof msg,
                     "Arc needs %d segments; buffer has %d free (size %d). Not armed so it cannot block; increase buffer or arc_tolerance",
                     segments, free_slots, (int)gcode_buffer.size);
            stepper_error(msg);
        }
    }

    typedef struct
    {
        float x, y, z;    // endpoint of segment i
        float dx, dy, dz; // delta from previous point
        float ds;         // chord length (mm)
        float vmax;       // max tangential speed on this segment (mm/s)
        float amax;       // max tangential accel usable on this segment (mm/s^2)
    } arc_seg_plan_t;

    arc_seg_plan_t *segs = (arc_seg_plan_t *)GetMemory((size_t)segments * sizeof(arc_seg_plan_t));

    // Base limits derived from configuration (conservative).
    // Only consider configured axes - start with a large value and reduce.
    float min_accel = 1e9f;
    if (axis_is_configured(&stepper_system.x) && stepper_system.x.max_accel < min_accel)
        min_accel = stepper_system.x.max_accel;
    if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_accel < min_accel)
        min_accel = stepper_system.y.max_accel;
    if (axis_is_configured(&stepper_system.z) && fabsf(z_travel) > 0.001f && stepper_system.z.max_accel < min_accel)
        min_accel = stepper_system.z.max_accel;
    // If no axes configured or all have huge values, use a safe default
    if (min_accel > 1e8f)
        min_accel = 100.0f;
    if (min_accel < 0.001f)
        min_accel = 0.001f;

    // Get minimum configured axis velocity for limiting
    // Only consider configured axes - start with a large value and reduce.
    float min_axis_velocity = 1e9f;
    if (axis_is_configured(&stepper_system.x) && stepper_system.x.max_velocity < min_axis_velocity)
        min_axis_velocity = stepper_system.x.max_velocity;
    if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_velocity < min_axis_velocity)
        min_axis_velocity = stepper_system.y.max_velocity;
    if (axis_is_configured(&stepper_system.z) && fabsf(z_travel) > 0.001f && stepper_system.z.max_velocity < min_axis_velocity)
        min_axis_velocity = stepper_system.z.max_velocity;
    if (min_axis_velocity <= 0.0f || min_axis_velocity > 1e8f)
        min_axis_velocity = feedrate;

    float r_xy = r_start;
    if (r_xy < 0.001f)
        r_xy = 0.001f;

    // For arcs, the limiting factor is that individual axes must handle both:
    // 1. Centripetal acceleration: a_c = v^2/r (always present during curve)
    // 2. Tangential acceleration: a_t (for accel/decel phases)
    //
    // At any point on the arc, one axis bears most of the centripetal load.
    // To be safe, limit tangential velocity so the worst-case axis acceleration
    // (which occurs when that axis is aligned with the radius) stays within limits.
    //
    // Conservative limit: v_max = sqrt(accel * r) ensures centripetal accel alone
    // doesn't exceed axis limits. We use 70% of this to leave room for tangential accel.
    float v_curve_cap = 0.7f * sqrtf(min_accel * r_xy);

    // Also limit by configured axis velocity - on a circle, tangential velocity
    // projects onto each axis as v_axis = v_tangential * sin(angle), so at worst
    // case (axis aligned with tangent), v_tangential = v_axis_max.
    // But we also need margin for the varying step density, so use 70%.
    float v_axis_cap = 0.7f * min_axis_velocity;

    // Speed cap: minimum of requested feed, curvature limit, and axis velocity limit
    float v_cap = feedrate;
    if (v_cap > v_curve_cap)
        v_cap = v_curve_cap;
    if (v_cap > v_axis_cap)
        v_cap = v_axis_cap;

    // Tangential acceleration budget: reserve some accel capacity for centripetal needs.
    // At v_cap on radius r, centripetal accel is v_cap^2/r.
    // Remaining budget for tangential: sqrt(min_accel^2 - a_c^2)
    float a_c = (v_cap * v_cap) / r_xy;
    float arc_a_t = min_accel;
    {
        float rem = min_accel * min_accel - a_c * a_c;
        if (rem <= 0.0f)
            arc_a_t = min_accel * 0.1f; // Minimal tangential budget if centripetal dominates
        else
            arc_a_t = sqrtf(rem);
    }
    if (arc_a_t < 0.001f)
        arc_a_t = 0.001f;

    // Generate segment endpoints and per-segment limits.
    float angle_per_segment = angular_travel / (float)segments;
    float z_per_segment = z_travel / (float)segments;

    float px = start_x;
    float py = start_y;
    float pz = start_z;

    for (int i = 1; i <= segments; i++)
    {
        float seg_x, seg_y, seg_z;
        if (i == segments)
        {
            seg_x = target_x;
            seg_y = target_y;
            seg_z = target_z;
        }
        else
        {
            float ang = start_angle + angle_per_segment * (float)i;
            seg_x = center_x + r_start * cosf(ang);
            seg_y = center_y + r_start * sinf(ang);
            seg_z = start_z + z_per_segment * (float)i;
        }

        arc_seg_plan_t *s = &segs[i - 1];
        s->x = seg_x;
        s->y = seg_y;
        s->z = seg_z;
        s->dx = seg_x - px;
        s->dy = seg_y - py;
        s->dz = seg_z - pz;
        s->ds = sqrtf(s->dx * s->dx + s->dy * s->dy + s->dz * s->dz);
        if (s->ds < 0.001f)
            s->ds = 0.001f;

        // Max speed limited by requested feed/curvature and per-axis max velocity components.
        float vmax = v_cap;
        if (axis_is_configured(&stepper_system.x) && fabsf(s->dx) > 0.001f)
        {
            float vax = stepper_system.x.max_velocity * s->ds / fabsf(s->dx);
            if (vax < vmax)
                vmax = vax;
        }
        if (axis_is_configured(&stepper_system.y) && fabsf(s->dy) > 0.001f)
        {
            float vay = stepper_system.y.max_velocity * s->ds / fabsf(s->dy);
            if (vay < vmax)
                vmax = vay;
        }
        if (axis_is_configured(&stepper_system.z) && fabsf(s->dz) > 0.001f)
        {
            float vaz = stepper_system.z.max_velocity * s->ds / fabsf(s->dz);
            if (vaz < vmax)
                vmax = vaz;
        }
        if (vmax < 0.0f)
            vmax = 0.0f;
        s->vmax = vmax;

        // Max tangential accel: use the minimum configured axis accel directly.
        // The per-axis projection formula (accel * ds / dx) gives the tangential accel
        // that would cause axis X to hit its limit. But when dx is tiny, this gives
        // a huge value which is wrong. Instead, cap to the direct axis limit.
        float amax = arc_a_t;
        if (axis_is_configured(&stepper_system.x) && stepper_system.x.max_accel < amax)
            amax = stepper_system.x.max_accel;
        if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_accel < amax)
            amax = stepper_system.y.max_accel;
        if (axis_is_configured(&stepper_system.z) && fabsf(s->dz) > 0.001f && stepper_system.z.max_accel < amax)
            amax = stepper_system.z.max_accel;
        if (amax < 0.001f)
            amax = 0.001f;
        s->amax = amax;

        px = seg_x;
        py = seg_y;
        pz = seg_z;
    }

    // Aggregate constraints for the bundled arc block.
    float executed_distance = 0.0f;
    float block_velocity_cap = v_cap;
    float block_min_accel = arc_a_t;
    float max_segment_step_density = 0.0f; // Track maximum steps/mm for any segment

    arc_segment_runtime_t *runtime_segments = (arc_segment_runtime_t *)GetMemory((size_t)segments * sizeof(arc_segment_runtime_t));
    uint16_t runtime_count = 0;
    int32_t total_major_steps = 0;
    int32_t total_x_steps = 0;
    int32_t total_y_steps = 0;
    int32_t total_z_steps = 0;

    const bool have_x = axis_is_configured(&stepper_system.x);
    const bool have_y = axis_is_configured(&stepper_system.y);
    const bool have_z = axis_is_configured(&stepper_system.z);

    int32_t prev_steps_x = 0;
    int32_t prev_steps_y = 0;
    int32_t prev_steps_z = 0;

    if (have_x)
        prev_steps_x = mm_to_steps_rounded(start_x - stepper_system.x_g92_offset, stepper_system.x.steps_per_mm);
    if (have_y)
        prev_steps_y = mm_to_steps_rounded(start_y - stepper_system.y_g92_offset, stepper_system.y.steps_per_mm);
    if (have_z)
        prev_steps_z = mm_to_steps_rounded(start_z - stepper_system.z_g92_offset, stepper_system.z.steps_per_mm);

    for (int i = 0; i < segments; i++)
    {
        if (!position_within_limits(&stepper_system.x, segs[i].x))
            stepper_error("Arc exceeds X soft limits");
        if (!position_within_limits(&stepper_system.y, segs[i].y))
            stepper_error("Arc exceeds Y soft limits");
        if (!position_within_limits(&stepper_system.z, segs[i].z))
            stepper_error("Arc exceeds Z soft limits");

        int32_t x_steps = 0;
        int32_t y_steps = 0;
        int32_t z_steps = 0;
        bool x_dir = true;
        bool y_dir = true;
        bool z_dir = true;

        if (have_x)
        {
            int32_t next_steps = mm_to_steps_rounded(segs[i].x - stepper_system.x_g92_offset, stepper_system.x.steps_per_mm);
            int32_t delta = next_steps - prev_steps_x;
            prev_steps_x = next_steps;
            if (delta < 0)
            {
                x_dir = false;
                x_steps = -delta;
            }
            else
            {
                x_dir = true;
                x_steps = delta;
            }
        }
        if (have_y)
        {
            int32_t next_steps = mm_to_steps_rounded(segs[i].y - stepper_system.y_g92_offset, stepper_system.y.steps_per_mm);
            int32_t delta = next_steps - prev_steps_y;
            prev_steps_y = next_steps;
            if (delta < 0)
            {
                y_dir = false;
                y_steps = -delta;
            }
            else
            {
                y_dir = true;
                y_steps = delta;
            }
        }
        if (have_z)
        {
            int32_t next_steps = mm_to_steps_rounded(segs[i].z - stepper_system.z_g92_offset, stepper_system.z.steps_per_mm);
            int32_t delta = next_steps - prev_steps_z;
            prev_steps_z = next_steps;
            if (delta < 0)
            {
                z_dir = false;
                z_steps = -delta;
            }
            else
            {
                z_dir = true;
                z_steps = delta;
            }
        }

        int32_t major = x_steps;
        if (y_steps > major)
            major = y_steps;
        if (z_steps > major)
            major = z_steps;

        if (major <= 0)
            continue;

        arc_segment_runtime_t *seg_runtime = &runtime_segments[runtime_count++];
        seg_runtime->x_steps = x_steps;
        seg_runtime->y_steps = y_steps;
        seg_runtime->z_steps = z_steps;
        seg_runtime->x_dir = x_dir;
        seg_runtime->y_dir = y_dir;
        seg_runtime->z_dir = z_dir;
        seg_runtime->major_steps = major;
        seg_runtime->major_axis_mask = compute_major_axis_mask(major, x_steps, y_steps, z_steps);

        total_major_steps += major;
        total_x_steps += x_steps;
        total_y_steps += y_steps;
        total_z_steps += z_steps;

        executed_distance += segs[i].ds;
        if (segs[i].vmax < block_velocity_cap)
            block_velocity_cap = segs[i].vmax;
        if (segs[i].amax < block_min_accel)
            block_min_accel = segs[i].amax;

        // Track maximum step density to ensure step rate is sufficient for all segments
        if (segs[i].ds > 0.001f)
        {
            float segment_density = (float)major / segs[i].ds;
            if (segment_density > max_segment_step_density)
                max_segment_step_density = segment_density;
        }
    }

    FreeMemorySafe((void **)&segs);

    if (block_velocity_cap < 0.001f)
        block_velocity_cap = 0.001f;
    if (block_min_accel < 0.001f)
        block_min_accel = 0.001f;

    if (runtime_count == 0 || total_major_steps <= 0)
    {
        FreeMemorySafe((void **)&runtime_segments);
        planner_x = target_x;
        planner_y = target_y;
        planner_z = target_z;
        return true;
    }

    if (executed_distance < 0.001f)
        executed_distance = 0.001f;

    // Use maximum segment step density (not average) to ensure step rate is sufficient
    // for all segments. Using average causes velocity spikes in low-density segments.
    float virtual_steps_per_mm = max_segment_step_density;
    if (virtual_steps_per_mm <= 0.0f)
        virtual_steps_per_mm = (float)total_major_steps / executed_distance;

    gcode_block_t block = (gcode_block_t){0};
    block.type = is_clockwise ? GCODE_CW_ARC : GCODE_CCW_ARC;
    block.x = target_x;
    block.y = target_y;
    block.z = target_z;
    block.has_x = (total_x_steps > 0);
    block.has_y = (total_y_steps > 0);
    block.has_z = (total_z_steps > 0);
    block.feedrate = feedrate;
    block.x_steps_planned = total_x_steps;
    block.y_steps_planned = total_y_steps;
    block.z_steps_planned = total_z_steps;
    block.x_dir = runtime_segments[0].x_dir;
    block.y_dir = runtime_segments[0].y_dir;
    block.z_dir = runtime_segments[0].z_dir;
    block.major_steps = total_major_steps;
    block.major_axis_mask = 0;
    block.allow_accumulator_carry = false;
    block.distance = executed_distance;
    block.virtual_steps_per_mm = virtual_steps_per_mm;
    block.unit_x = 0.0f;
    block.unit_y = 0.0f;
    block.unit_z = 0.0f;
    block.max_velocity = block_velocity_cap;
    block.min_accel = block_min_accel;

    // Compute accel_increment for ISR - CRITICAL: without this, arcs never accelerate!
    block.accel_increment = (int32_t)(block.min_accel * block.virtual_steps_per_mm * (float)RATE_SCALE / (float)ISR_FREQ);
    if (block.accel_increment == 0)
        block.accel_increment = 1;

    block.entry_rate = 0;
    block.exit_rate = 0;
    block.arc_segment_count = runtime_count;
    block.arc_segments = runtime_segments;

    recompute_profile_for_block(&block);

    planner_x = target_x;
    planner_y = target_y;
    planner_z = target_z;

    if (gcode_buffer.size == 0)
    {
        FreeMemorySafe((void **)&runtime_segments);
        stepper_error("G-code buffer not initialized");
    }

    while (gcode_buffer_is_full(&gcode_buffer))
    {
        if (!stepper_armed)
        {
            FreeMemorySafe((void **)&runtime_segments);
            stepper_error("Arc buffer full");
        }

        CheckAbort();
        if (MMAbort)
        {
            FreeMemorySafe((void **)&runtime_segments);
            stepper_error("Aborted");
        }
        tight_loop_contents();
    }

    if (!gcode_buffer_add(&gcode_buffer, &block))
    {
        FreeMemorySafe((void **)&runtime_segments);
        stepper_error("Failed to queue arc block");
    }

    return true;
}

// Round a hardware position (mm) to an integer step count.
// We avoid libm rounding calls to keep dependencies minimal.
static inline int32_t mm_to_steps_rounded(float hw_mm, float steps_per_mm)
{
    float s = hw_mm * steps_per_mm;
    return (s >= 0.0f) ? (int32_t)(s + 0.5f) : (int32_t)(s - 0.5f);
}

static inline uint8_t compute_major_axis_mask(int32_t major_steps,
                                              int32_t x_steps,
                                              int32_t y_steps,
                                              int32_t z_steps)
{
    uint8_t mask = 0;
    if (major_steps <= 0)
        return 0;
    if (x_steps > 0 && x_steps == major_steps)
        mask |= 0x1;
    if (y_steps > 0 && y_steps == major_steps)
        mask |= 0x2;
    if (z_steps > 0 && z_steps == major_steps)
        mask |= 0x4;
    return mask;
}

// Plan and add a single linear move (G0/G1) to the buffer
// This is the original function for standalone linear moves
// Currently unused - inline code in cmd_stepper handles G0/G1
// Kept for potential future refactoring
__attribute__((unused)) static bool plan_and_add_linear_move(float target_x, float target_y, float target_z,
                                                             float feedrate, bool is_rapid)
{
    gcode_block_t block = {0};

    block.type = is_rapid ? GCODE_RAPID_MOVE : GCODE_LINEAR_MOVE;
    block.x = target_x;
    block.y = target_y;
    block.z = target_z;
    block.has_x = axis_is_configured(&stepper_system.x);
    block.has_y = axis_is_configured(&stepper_system.y);
    block.has_z = axis_is_configured(&stepper_system.z);
    block.feedrate = feedrate;

    // Calculate motion for each axis
    float dx_mm = 0.0f, dy_mm = 0.0f, dz_mm = 0.0f;
    block.x_steps_planned = 0;
    block.y_steps_planned = 0;
    block.z_steps_planned = 0;
    block.x_dir = true;
    block.y_dir = true;
    block.z_dir = true;

    if (block.has_x)
    {
        dx_mm = target_x - planner_x;
        block.x_dir = (dx_mm >= 0.0f);
        block.x_steps_planned = (int32_t)(fabsf(dx_mm) * stepper_system.x.steps_per_mm);
    }
    if (block.has_y)
    {
        dy_mm = target_y - planner_y;
        block.y_dir = (dy_mm >= 0.0f);
        block.y_steps_planned = (int32_t)(fabsf(dy_mm) * stepper_system.y.steps_per_mm);
    }
    if (block.has_z)
    {
        dz_mm = target_z - planner_z;
        block.z_dir = (dz_mm >= 0.0f);
        block.z_steps_planned = (int32_t)(fabsf(dz_mm) * stepper_system.z.steps_per_mm);
    }

    // Find major axis
    block.major_steps = block.x_steps_planned;
    if (block.y_steps_planned > block.major_steps)
        block.major_steps = block.y_steps_planned;
    if (block.z_steps_planned > block.major_steps)
        block.major_steps = block.z_steps_planned;

    if (block.major_steps > 0)
    {
        float total_dist_mm = sqrtf(dx_mm * dx_mm + dy_mm * dy_mm + dz_mm * dz_mm);
        if (total_dist_mm < 0.001f)
            total_dist_mm = 0.001f;

        float virtual_steps_per_mm = (float)block.major_steps / total_dist_mm;

        float target_velocity = feedrate;
        if (is_rapid)
        {
            target_velocity = 1e9f;
            if (block.x_steps_planned > 0 && fabsf(dx_mm) > 0.001f)
            {
                float axis_limit = stepper_system.x.max_velocity * total_dist_mm / fabsf(dx_mm);
                if (axis_limit < target_velocity)
                    target_velocity = axis_limit;
            }
            if (block.y_steps_planned > 0 && fabsf(dy_mm) > 0.001f)
            {
                float axis_limit = stepper_system.y.max_velocity * total_dist_mm / fabsf(dy_mm);
                if (axis_limit < target_velocity)
                    target_velocity = axis_limit;
            }
            if (block.z_steps_planned > 0 && fabsf(dz_mm) > 0.001f)
            {
                float axis_limit = stepper_system.z.max_velocity * total_dist_mm / fabsf(dz_mm);
                if (axis_limit < target_velocity)
                    target_velocity = axis_limit;
            }
        }

        block.distance = total_dist_mm;
        block.virtual_steps_per_mm = virtual_steps_per_mm;
        block.max_velocity = target_velocity;

        block.unit_x = dx_mm / total_dist_mm;
        block.unit_y = dy_mm / total_dist_mm;
        block.unit_z = dz_mm / total_dist_mm;

        // Calculate acceleration (use minimum of active axes)
        float min_accel = 1e9f;
        if (block.x_steps_planned > 0 && fabsf(dx_mm) > 0.001f)
        {
            float axis_accel = stepper_system.x.max_accel * total_dist_mm / fabsf(dx_mm);
            if (axis_accel < min_accel)
                min_accel = axis_accel;
        }
        if (block.y_steps_planned > 0 && fabsf(dy_mm) > 0.001f)
        {
            float axis_accel = stepper_system.y.max_accel * total_dist_mm / fabsf(dy_mm);
            if (axis_accel < min_accel)
                min_accel = axis_accel;
        }
        if (block.z_steps_planned > 0 && fabsf(dz_mm) > 0.001f)
        {
            float axis_accel = stepper_system.z.max_accel * total_dist_mm / fabsf(dz_mm);
            if (axis_accel < min_accel)
                min_accel = axis_accel;
        }

        block.min_accel = min_accel;
        block.accel_increment = (int32_t)(min_accel * virtual_steps_per_mm * (float)RATE_SCALE / (float)ISR_FREQ);
        if (block.accel_increment == 0)
            block.accel_increment = 1;

        // Standalone moves start/stop by default
        block.entry_rate = 0;
        block.exit_rate = 0;
        recompute_profile_for_block(&block);
    }
    else
    {
        block.is_planned = false;
    }

    // Update planner position
    planner_x = target_x;
    planner_y = target_y;
    planner_z = target_z;

    return gcode_buffer_add(&gcode_buffer, &block);
}

// Stepper timer interrupt handler - runs at 100KHz
// Uses only integer math - all planning done in main context
// Implements Bresenham algorithm for coordinated multi-axis motion
void __not_in_flash_func(stepper_timer_isr)(void)
{
    // Clear the interrupt flag for PWM slice 10
    pwm_clear_irq(STEPPER_PWM_SLICE);

    // Increment tick counter
    stepper_tick_count++;

    // If not armed, don't execute motion.
    if (!stepper_armed)
        return;

    // Check hardware limit switches (active-low, triggers on 0)
    if (stepper_system.limits_enabled && !stepper_system.homing_active)
    {
        uint64_t gpio_state = gpio_get_all64();
        // Limit switches are active-low: if any limit pin reads 0, it's triggered
        if ((~gpio_state & stepper_system.limit_switch_mask) != 0)
        {
            // Latch which limits were active at the time of the trip.
            stepper_limit_trip_latched = true;
            stepper_limit_trip_mask = (~gpio_state & stepper_system.limit_switch_mask);

            // Limit switch triggered during normal motion - emergency stop
            current_move.phase = MOVE_PHASE_IDLE;
            stepper_system.motion_active = false;
            stepper_armed = false; // Disarm to prevent motion resume

            // Disable drivers immediately (active-low enable pins)
            if (stepper_system.x.enable_pin != 0xFF)
                gpio_put(stepper_system.x.enable_pin, 1);
            if (stepper_system.y.enable_pin != 0xFF)
                gpio_put(stepper_system.y.enable_pin, 1);
            if (stepper_system.z.enable_pin != 0xFF)
                gpio_put(stepper_system.z.enable_pin, 1);

            // Disable spindle immediately
            stepper_spindle_off();

            // Clear buffer to prevent further motion
            gcode_buffer_release_all_payloads(&gcode_buffer);
            gcode_buffer.head = 0;
            gcode_buffer.tail = 0;
            gcode_buffer.count = 0;
            gcode_buffer.buffer_full = false;
            gcode_buffer.buffer_empty = true;
            stepper_gcode_buffer_space = gcode_buffer.size;

            // A hard-limit trip invalidates our assumed machine position.
            // Require the user to re-establish position with G28 or STEPPER POSITION.
            stepper_system.position_known = false;
            stepper_system.x_g92_offset = 0.0f;
            stepper_system.y_g92_offset = 0.0f;
            stepper_system.z_g92_offset = 0.0f;

            stepper_reset_accumulator_carry();
            stepper_release_current_arc();

            // Note: main code will now refuse RUN until position is re-established.
            return;
        }
    }
    // Note: During homing (homing_active=true), limits are checked by homing logic, not here

    // If idle, check for new work
    if (current_move.phase == MOVE_PHASE_IDLE)
    {
        // Try to dequeue a new block
        stepper_runtime_block_t *block = gcode_buffer_peek(&gcode_buffer);
        if (block == NULL)
            return;

        // Skip unplanned blocks or blocks with no steps
        if (!block->is_planned || block->major_steps == 0)
        {
            gcode_buffer_pop(&gcode_buffer);
            return;
        }

        // Copy pre-computed values to current_move (all integers)
        current_move.x_active = (block->x_steps_planned > 0);
        current_move.y_active = (block->y_steps_planned > 0);
        current_move.z_active = (block->z_steps_planned > 0);
        current_move.x_dir = block->x_dir;
        current_move.y_dir = block->y_dir;
        current_move.z_dir = block->z_dir;
        current_move.major_steps = block->major_steps;
        current_move.x_steps = block->x_steps_planned;
        current_move.y_steps = block->y_steps_planned;
        current_move.z_steps = block->z_steps_planned;
        current_move.cruise_rate = block->cruise_rate;
        current_move.exit_rate = block->exit_rate;
        current_move.accel_increment = block->accel_increment;
        current_move.major_axis_mask = block->major_axis_mask;
        current_move.allow_accumulator_carry = block->allow_accumulator_carry;

        if (block->arc_segment_count > 0 && block->arc_segments != NULL)
        {
            current_move.arc_active = true;
            current_move.arc_segments = block->arc_segments;
            current_move.arc_segment_count = block->arc_segment_count;
            current_move.arc_segment_index = 0;
            current_move.arc_segment_steps_remaining = 0;
            block->arc_segments = NULL;
            block->arc_segment_count = 0;
        }
        else
        {
            current_move.arc_active = false;
            current_move.arc_segments = NULL;
            current_move.arc_segment_count = 0;
            current_move.arc_segment_index = 0;
            current_move.arc_segment_steps_remaining = 0;
        }

        // Optional jerk-limited S-curve fields
        current_move.jerk_increment = block->jerk_increment;
        current_move.accel_rate = 0;
        current_move.scurve_ju_accel_steps = block->scurve_ju_accel_steps;
        current_move.scurve_ca_steps = block->scurve_ca_steps;
        current_move.scurve_jd_accel_steps = block->scurve_jd_accel_steps;
        current_move.scurve_cruise_steps = block->scurve_cruise_steps;
        current_move.scurve_ju_decel_steps = block->scurve_ju_decel_steps;
        current_move.scurve_cd_steps = block->scurve_cd_steps;
        current_move.scurve_jd_decel_steps = block->scurve_jd_decel_steps;
        current_move.accel_steps = block->accel_steps;
        current_move.cruise_steps = block->cruise_steps;
        current_move.decel_steps = block->decel_steps;
        current_move.total_steps_remaining = block->major_steps;
        current_move.steps_remaining = block->accel_steps;
        uint32_t initial_accum = 0u;
        if (stepper_accumulator_carry.valid &&
            stepper_accumulator_carry.allow_carry &&
            block->allow_accumulator_carry &&
            block->major_axis_mask != 0 &&
            stepper_accumulator_carry.major_axis_mask == block->major_axis_mask)
        {
            int32_t entry_rate = block->entry_rate;
            if (entry_rate < 0)
                entry_rate = -entry_rate;
            int32_t prev_rate = stepper_accumulator_carry.exit_rate;
            if (prev_rate < 0)
                prev_rate = -prev_rate;
            int32_t diff = entry_rate - prev_rate;
            if (diff < 0)
                diff = -diff;

            int32_t tol = (int32_t)block->accel_increment * 4;
            if (tol < (entry_rate / 50))
                tol = entry_rate / 50;
            if (tol < 1000)
                tol = 1000;

            if (diff <= tol)
                initial_accum = stepper_accumulator_carry.accumulator;
        }
        current_move.step_accumulator = initial_accum;
        stepper_accumulator_carry.valid = false;
        stepper_accumulator_carry.allow_carry = false;
        current_move.step_rate = block->entry_rate; // Entry rate (used for junction blending and arc segments)

        // Initialize Bresenham error terms (use half major_steps for better rounding)
        current_move.x_error = block->major_steps / 2;
        current_move.y_error = block->major_steps / 2;
        current_move.z_error = block->major_steps / 2;

        // Use precomputed initial phase (computed in planner to reduce ISR overhead)
        current_move.phase = (move_phase_t)block->initial_phase;
        current_move.accel_rate = block->initial_accel_rate;

        // Set initial steps_remaining based on phase
        switch (current_move.phase)
        {
        case MOVE_PHASE_S_JERK_UP_ACCEL:
            current_move.steps_remaining = block->scurve_ju_accel_steps;
            break;
        case MOVE_PHASE_S_CONST_ACCEL:
            current_move.steps_remaining = block->scurve_ca_steps;
            break;
        case MOVE_PHASE_S_JERK_DOWN_ACCEL:
            current_move.steps_remaining = block->scurve_jd_accel_steps;
            break;
        case MOVE_PHASE_S_CRUISE:
            current_move.steps_remaining = block->scurve_cruise_steps;
            break;
        case MOVE_PHASE_S_JERK_UP_DECEL:
            current_move.steps_remaining = block->scurve_ju_decel_steps;
            break;
        case MOVE_PHASE_S_CONST_DECEL:
            current_move.steps_remaining = block->scurve_cd_steps;
            break;
        case MOVE_PHASE_S_JERK_DOWN_DECEL:
            current_move.steps_remaining = block->scurve_jd_decel_steps;
            break;
        case MOVE_PHASE_ACCEL:
            current_move.steps_remaining = current_move.accel_steps;
            break;
        case MOVE_PHASE_CRUISE:
            current_move.steps_remaining = current_move.cruise_steps;
            break;
        case MOVE_PHASE_DECEL:
            current_move.steps_remaining = current_move.decel_steps;
            break;
        default:
            current_move.steps_remaining = 0;
            break;
        }

        // If the planner chose a non-ACCEL starting phase (e.g. pure CRUISE at low feedrates),
        // entry_rate may be 0 and the ISR would never generate steps.
        // Ensure we start stepping at cruise_rate for those cases.
        if (current_move.step_rate == 0 && current_move.cruise_rate > 0)
        {
            switch (current_move.phase)
            {
            case MOVE_PHASE_CRUISE:
            case MOVE_PHASE_DECEL:
            case MOVE_PHASE_S_CRUISE:
            case MOVE_PHASE_S_JERK_UP_DECEL:
            case MOVE_PHASE_S_CONST_DECEL:
            case MOVE_PHASE_S_JERK_DOWN_DECEL:
                current_move.step_rate = current_move.cruise_rate;
                break;
            default:
                break;
            }
        }

        // Ensure the first step arrives in a reasonable time even with tiny accel_increment.
        if (current_move.step_rate < STEPPER_START_MIN_STEP_RATE && current_move.cruise_rate > 0)
        {
            switch (current_move.phase)
            {
            case MOVE_PHASE_ACCEL:
            case MOVE_PHASE_S_JERK_UP_ACCEL:
            case MOVE_PHASE_S_CONST_ACCEL:
            case MOVE_PHASE_S_JERK_DOWN_ACCEL:
                current_move.step_rate = STEPPER_START_MIN_STEP_RATE;
                if (current_move.step_rate > current_move.cruise_rate)
                    current_move.step_rate = current_move.cruise_rate;
                break;
            default:
                break;
            }
        }

        bool skip_block = false;
        if (current_move.arc_active)
        {
            if (!stepper_load_arc_segment(0))
            {
                skip_block = true;
                stepper_release_current_arc();
            }
        }
        else
        {
            stepper_apply_direction_pins();
        }

        // Remove block from buffer
        gcode_buffer_pop(&gcode_buffer);

        if (skip_block)
        {
            stepper_system.motion_active = false;
            return;
        }

        stepper_system.motion_active = true;
        return; // Start motion on next tick
    }

    // Execute current move - integer math only

    // Update velocity based on phase
    switch (current_move.phase)
    {
    case MOVE_PHASE_ACCEL:
        current_move.step_rate += current_move.accel_increment;
        if (current_move.step_rate > current_move.cruise_rate)
            current_move.step_rate = current_move.cruise_rate;
        break;

    case MOVE_PHASE_DECEL:
        // Decelerate toward exit_rate (0 for stop, non-zero for junction blending)
        if (current_move.step_rate > current_move.exit_rate)
        {
            if (current_move.step_rate > current_move.exit_rate + current_move.accel_increment)
                current_move.step_rate -= current_move.accel_increment;
            else
                current_move.step_rate = current_move.exit_rate;
        }
        break;

    // S-curve phases (jerk-limited)
    case MOVE_PHASE_S_JERK_UP_ACCEL:
        current_move.accel_rate += (int32_t)current_move.jerk_increment;
        if (current_move.accel_rate > (int32_t)current_move.accel_increment)
            current_move.accel_rate = (int32_t)current_move.accel_increment;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < 0)
                sr = 0;
            if (sr > (int64_t)current_move.cruise_rate)
                sr = current_move.cruise_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    case MOVE_PHASE_S_CONST_ACCEL:
        current_move.accel_rate = (int32_t)current_move.accel_increment;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < 0)
                sr = 0;
            if (sr > (int64_t)current_move.cruise_rate)
                sr = current_move.cruise_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    case MOVE_PHASE_S_JERK_DOWN_ACCEL:
        current_move.accel_rate -= (int32_t)current_move.jerk_increment;
        if (current_move.accel_rate < 0)
            current_move.accel_rate = 0;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < 0)
                sr = 0;
            if (sr > (int64_t)current_move.cruise_rate)
                sr = current_move.cruise_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    case MOVE_PHASE_S_CRUISE:
        current_move.accel_rate = 0;
        break;

    case MOVE_PHASE_S_JERK_UP_DECEL:
        current_move.accel_rate -= (int32_t)current_move.jerk_increment;
        if (current_move.accel_rate < -(int32_t)current_move.accel_increment)
            current_move.accel_rate = -(int32_t)current_move.accel_increment;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < (int64_t)current_move.exit_rate)
                sr = current_move.exit_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    case MOVE_PHASE_S_CONST_DECEL:
        current_move.accel_rate = -(int32_t)current_move.accel_increment;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < (int64_t)current_move.exit_rate)
                sr = current_move.exit_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    case MOVE_PHASE_S_JERK_DOWN_DECEL:
        current_move.accel_rate += (int32_t)current_move.jerk_increment;
        if (current_move.accel_rate > 0)
            current_move.accel_rate = 0;
        {
            int64_t sr = (int64_t)current_move.step_rate + (int64_t)current_move.accel_rate;
            if (sr < (int64_t)current_move.exit_rate)
                sr = current_move.exit_rate;
            current_move.step_rate = (int32_t)sr;
        }
        break;

    default:
        break;
    }

    // Tail completion guard (stop-to-zero only)
    // Purpose: avoid step_rate quantization driving step_rate to 0 before the final steps are emitted.
    // To reduce visible "crawl", clamp to a dynamic minimum derived from accel_increment (plus a small floor)
    // rather than forcing a fixed low speed for the entire tail window.
#if 1
    if (current_move.exit_rate == 0 && current_move.total_steps_remaining > 0 &&
        current_move.total_steps_remaining <= STEPPER_TAIL_FINISH_STEPS)
    {
        if (current_move.phase == MOVE_PHASE_DECEL ||
            current_move.phase == MOVE_PHASE_S_JERK_UP_DECEL ||
            current_move.phase == MOVE_PHASE_S_CONST_DECEL ||
            current_move.phase == MOVE_PHASE_S_JERK_DOWN_DECEL)
        {
            uint32_t min_rate = (current_move.accel_increment > 0) ? (uint32_t)current_move.accel_increment : 1u;
            if (min_rate < (uint32_t)STEPPER_TAIL_MIN_STEP_RATE)
                min_rate = (uint32_t)STEPPER_TAIL_MIN_STEP_RATE;

            if ((uint32_t)current_move.step_rate < min_rate)
            {
                current_move.step_rate = (int32_t)min_rate;
                if (current_move.cruise_rate > 0 && current_move.step_rate > current_move.cruise_rate)
                    current_move.step_rate = current_move.cruise_rate;
            }
        }
    }
#endif

    // Accumulator-based step timing. When a major-axis step is due, we also enforce
    // per-axis minimum inter-step intervals derived from each axis' max velocity.
    // If any axis isn't ready, we defer ALL steps for this tick.
    uint64_t accum = (uint64_t)current_move.step_accumulator + (uint64_t)current_move.step_rate;
    if (accum < STEP_ACCUMULATOR_MAX)
    {
        current_move.step_accumulator = (uint32_t)accum;
        return;
    }

    // Bresenham algorithm (peek): determine which axes would step on this major step.
    // We compute on locals first so we can defer without mutating state.
    bool step_x = false, step_y = false, step_z = false;
    int32_t x_error = current_move.x_error;
    int32_t y_error = current_move.y_error;
    int32_t z_error = current_move.z_error;

    if (current_move.x_active)
    {
        x_error -= current_move.x_steps;
        if (x_error < 0)
        {
            x_error += current_move.major_steps;
            step_x = true;
        }
    }
    if (current_move.y_active)
    {
        y_error -= current_move.y_steps;
        if (y_error < 0)
        {
            y_error += current_move.major_steps;
            step_y = true;
        }
    }
    if (current_move.z_active)
    {
        z_error -= current_move.z_steps;
        if (z_error < 0)
        {
            z_error += current_move.major_steps;
            step_z = true;
        }
    }

    // Per-axis minimum tick-gap gate: defer step if any axis hasn't met its minimum interval.
    {
        const uint32_t now = stepper_tick_count;
        if ((step_x && (now - stepper_axis_last_step_tick[0]) < (uint32_t)stepper_axis_min_step_ticks[0]) ||
            (step_y && (now - stepper_axis_last_step_tick[1]) < (uint32_t)stepper_axis_min_step_ticks[1]) ||
            (step_z && (now - stepper_axis_last_step_tick[2]) < (uint32_t)stepper_axis_min_step_ticks[2]))
        {
            current_move.step_accumulator = STEP_ACCUMULATOR_MAX - 1u;
            return;
        }
    }

    // Commit accumulator and Bresenham state now that we will emit pulses.
    accum -= STEP_ACCUMULATOR_MAX;
    current_move.step_accumulator = (uint32_t)accum;
    current_move.x_error = x_error;
    current_move.y_error = y_error;
    current_move.z_error = z_error;

    // Generate step pulses (multi-axis) with a configurable minimum pulse width.
    uint64_t step_mask = 0;
    if (step_x)
        step_mask |= (1ULL << stepper_system.x.step_pin);
    if (step_y)
        step_mask |= (1ULL << stepper_system.y.step_pin);
    if (step_z)
        step_mask |= (1ULL << stepper_system.z.step_pin);

    if (step_mask)
    {
        gpio_set_mask64(step_mask);
        if (STEPPER_STEP_PULSE_US > 0)
            busy_wait_us_32((uint32_t)STEPPER_STEP_PULSE_US);
        gpio_clr_mask64(step_mask);
    }

    // Update per-axis last-step timing and positions in single pass.
    {
        const uint32_t now = stepper_tick_count;
        if (step_x)
        {
            stepper_axis_last_step_tick[0] = now;
            stepper_system.x.current_pos += current_move.x_dir ? 1 : -1;
        }
        if (step_y)
        {
            stepper_axis_last_step_tick[1] = now;
            stepper_system.y.current_pos += current_move.y_dir ? 1 : -1;
        }
        if (step_z)
        {
            stepper_axis_last_step_tick[2] = now;
            stepper_system.z.current_pos += current_move.z_dir ? 1 : -1;
        }
    }

    // ...existing code...

    current_move.total_steps_remaining--;
    current_move.steps_remaining--;

    if (current_move.arc_active)
    {
        if (current_move.arc_segment_steps_remaining > 0)
            current_move.arc_segment_steps_remaining--;
        if (current_move.arc_segment_steps_remaining <= 0)
            stepper_load_arc_segment((uint16_t)(current_move.arc_segment_index + 1));
    }

    // If we've executed the final step for this block, complete immediately.
    // Without this, profiles that transition into a 0-step terminal phase
    // (e.g. CRUISE -> DECEL with decel_steps==0) can leave motion_active stuck true.
    if (current_move.total_steps_remaining <= 0)
    {
        current_move.phase = MOVE_PHASE_IDLE;
        if (current_move.allow_accumulator_carry &&
            gcode_buffer.count != 0 &&
            current_move.step_rate > 0 &&
            current_move.major_axis_mask != 0)
        {
            stepper_accumulator_carry.valid = true;
            stepper_accumulator_carry.allow_carry = true;
            stepper_accumulator_carry.accumulator = current_move.step_accumulator;
            stepper_accumulator_carry.exit_rate = current_move.step_rate;
            stepper_accumulator_carry.major_axis_mask = current_move.major_axis_mask;
        }
        else
        {
            stepper_reset_accumulator_carry();
        }
        stepper_release_current_arc();
        stepper_system.motion_active = false;
        return;
    }

    // Check for phase transition
    if (current_move.steps_remaining <= 0)
    {
        switch (current_move.phase)
        {
        case MOVE_PHASE_ACCEL:
            if (current_move.cruise_steps > 0)
            {
                current_move.phase = MOVE_PHASE_CRUISE;
                current_move.steps_remaining = current_move.cruise_steps;
            }
            else
            {
                current_move.phase = MOVE_PHASE_DECEL;
                current_move.steps_remaining = current_move.decel_steps;
            }
            break;
        case MOVE_PHASE_CRUISE:
            current_move.phase = MOVE_PHASE_DECEL;
            current_move.steps_remaining = current_move.decel_steps;
            break;
        case MOVE_PHASE_DECEL:
            current_move.phase = MOVE_PHASE_IDLE;
            stepper_system.motion_active = false;
            break;

        // S-curve transitions
        case MOVE_PHASE_S_JERK_UP_ACCEL:
            if (current_move.scurve_ca_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CONST_ACCEL;
                current_move.steps_remaining = current_move.scurve_ca_steps;
                current_move.accel_rate = (int32_t)current_move.accel_increment;
            }
            else if (current_move.scurve_jd_accel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_ACCEL;
                current_move.steps_remaining = current_move.scurve_jd_accel_steps;
                current_move.accel_rate = (int32_t)current_move.accel_increment;
            }
            else if (current_move.scurve_cruise_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CRUISE;
                current_move.steps_remaining = current_move.scurve_cruise_steps;
                current_move.accel_rate = 0;
            }
            else
            {
                current_move.phase = MOVE_PHASE_S_JERK_UP_DECEL;
                current_move.steps_remaining = current_move.scurve_ju_decel_steps;
                current_move.accel_rate = 0;
            }
            break;
        case MOVE_PHASE_S_CONST_ACCEL:
            if (current_move.scurve_jd_accel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_ACCEL;
                current_move.steps_remaining = current_move.scurve_jd_accel_steps;
                current_move.accel_rate = (int32_t)current_move.accel_increment;
            }
            else if (current_move.scurve_cruise_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CRUISE;
                current_move.steps_remaining = current_move.scurve_cruise_steps;
                current_move.accel_rate = 0;
            }
            else
            {
                current_move.phase = MOVE_PHASE_S_JERK_UP_DECEL;
                current_move.steps_remaining = current_move.scurve_ju_decel_steps;
                current_move.accel_rate = 0;
            }
            break;
        case MOVE_PHASE_S_JERK_DOWN_ACCEL:
            current_move.accel_rate = 0;
            if (current_move.scurve_cruise_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CRUISE;
                current_move.steps_remaining = current_move.scurve_cruise_steps;
            }
            else if (current_move.scurve_ju_decel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_UP_DECEL;
                current_move.steps_remaining = current_move.scurve_ju_decel_steps;
            }
            else if (current_move.scurve_cd_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CONST_DECEL;
                current_move.steps_remaining = current_move.scurve_cd_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            else
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_DECEL;
                current_move.steps_remaining = current_move.scurve_jd_decel_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            break;
        case MOVE_PHASE_S_CRUISE:
            current_move.accel_rate = 0;
            if (current_move.scurve_ju_decel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_UP_DECEL;
                current_move.steps_remaining = current_move.scurve_ju_decel_steps;
            }
            else if (current_move.scurve_cd_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CONST_DECEL;
                current_move.steps_remaining = current_move.scurve_cd_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            else
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_DECEL;
                current_move.steps_remaining = current_move.scurve_jd_decel_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            break;
        case MOVE_PHASE_S_JERK_UP_DECEL:
            if (current_move.scurve_cd_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_CONST_DECEL;
                current_move.steps_remaining = current_move.scurve_cd_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            else if (current_move.scurve_jd_decel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_DECEL;
                current_move.steps_remaining = current_move.scurve_jd_decel_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            else
            {
                current_move.phase = MOVE_PHASE_IDLE;
                stepper_system.motion_active = false;
            }
            break;
        case MOVE_PHASE_S_CONST_DECEL:
            if (current_move.scurve_jd_decel_steps > 0)
            {
                current_move.phase = MOVE_PHASE_S_JERK_DOWN_DECEL;
                current_move.steps_remaining = current_move.scurve_jd_decel_steps;
                current_move.accel_rate = -(int32_t)current_move.accel_increment;
            }
            else
            {
                current_move.phase = MOVE_PHASE_IDLE;
                stepper_system.motion_active = false;
            }
            break;
        case MOVE_PHASE_S_JERK_DOWN_DECEL:
            current_move.phase = MOVE_PHASE_IDLE;
            stepper_system.motion_active = false;
            break;
        default:
            break;
        }

        // Safety check: prevent hanging if exit_rate==0 and step_rate reaches 0 in a decel-family phase
        if (current_move.exit_rate == 0 &&
            current_move.step_rate == 0 &&
            (current_move.phase == MOVE_PHASE_DECEL ||
             current_move.phase == MOVE_PHASE_S_JERK_UP_DECEL ||
             current_move.phase == MOVE_PHASE_S_CONST_DECEL ||
             current_move.phase == MOVE_PHASE_S_JERK_DOWN_DECEL))
        {
            current_move.phase = MOVE_PHASE_IDLE;
            stepper_system.motion_active = false;
        }
    }
}

// Helper function to initialize an axis
static void stepper_axis_init(stepper_axis_t *axis)
{
    axis->step_pin = 0xFF;
    axis->dir_pin = 0xFF;
    axis->enable_pin = 0xFF;
    axis->steps_per_mm = 200.0f;
    axis->max_velocity = 100.0f;
    axis->max_accel = 500.0f;
    axis->current_pos = 0;
    axis->target_pos = 0;
    axis->current_vel = 0.0f;
    axis->accel_distance = 0.0f;
    axis->decel_distance = 0.0f;
    axis->step_delay_us = 0;
    axis->dir_invert = false;
    axis->min_limit = INT32_MIN;
    axis->max_limit = INT32_MAX;
    axis->homing_dir = false;
}

// Helper function to check if an axis is configured
static bool axis_is_configured(stepper_axis_t *axis)
{
    return (axis->step_pin != 0xFF);
}

// Helper function to check if soft limits are configured for an axis
static bool axis_limits_configured(stepper_axis_t *axis)
{
    return (axis->min_limit != INT32_MIN || axis->max_limit != INT32_MAX);
}

// Helper function to check if a position is within soft limits (in mm)
// Returns true if within limits or limits not configured
static bool position_within_limits(stepper_axis_t *axis, float pos_mm)
{
    if (!axis_is_configured(axis))
        return true; // Unconfigured axis, no limit check
    if (!axis_limits_configured(axis))
        return true; // No limits set, allow any position

    // Convert workspace position to hardware position by subtracting G92 offset
    float hw_pos_mm = pos_mm;
    if (axis == &stepper_system.x)
        hw_pos_mm -= stepper_system.x_g92_offset;
    else if (axis == &stepper_system.y)
        hw_pos_mm -= stepper_system.y_g92_offset;
    else if (axis == &stepper_system.z)
        hw_pos_mm -= stepper_system.z_g92_offset;

    int32_t pos_steps = (int32_t)(hw_pos_mm * axis->steps_per_mm);
    return (pos_steps >= axis->min_limit && pos_steps <= axis->max_limit);
}

// Helper function to get axis pointer from axis character
static stepper_axis_t *get_axis_ptr(char axis_char)
{
    switch (toupper(axis_char))
    {
    case 'X':
        return &stepper_system.x;
    case 'Y':
        return &stepper_system.y;
    case 'Z':
        return &stepper_system.z;
    default:
        return NULL;
    }
}

// Helper: match a parameter against one of N expected strings.
// First pass tests the raw token (allows unquoted literals).
// Second pass tests getCstring() (allows quoted strings or variables).
// Returns 1..n on match, or 0 if no match.
static int checkparam(char *p, int n, char *test1, ...)
{
    if (p == NULL || n <= 0)
        return 0;

    if (n > 32)
        error("Internal fault (checkparam)");

    const char *tests[32];
    tests[0] = test1;

    va_list ap;
    va_start(ap, test1);
    for (int i = 1; i < n; i++)
        tests[i] = va_arg(ap, const char *);
    va_end(ap);

    for (int pass = 0; pass < 2; pass++)
    {
        char *s = p;
        if (pass == 1)
            s = (char *)getCstring((unsigned char *)p);
        skipspace(s);

        for (int i = 0; i < n; i++)
        {
            const char *t = tests[i];
            if (t == NULL)
                continue;

            if (t[0] && t[1] == 0)
            {
                if (toupper((unsigned char)s[0]) == toupper((unsigned char)t[0]) && s[1] == 0)
                    return i + 1;
            }
            else
            {
                if (checkstring((unsigned char *)s, (unsigned char *)t) != NULL)
                    return i + 1;
            }
        }
    }

    return 0;
}

// Helper function to parse a pin number (accepts physical pin or GPxx format)
static int parse_pin(unsigned char *arg)
{
    unsigned char code;
    if (!(code = codecheck(arg)))
    {
        arg += 2; // Skip "GP" prefix
    }
    int pin = getinteger(arg);
    if (!code)
    {
        pin = codemap(pin); // Convert GPIO number to physical pin
    }
    return pin;
}

// Helper function to automatically calculate and set a reasonable default jerk
// based on configured axes. Called after axis configuration changes.
// Uses conservative default: 10× max acceleration, clamped to valid ISR range.
static void calculate_default_jerk(void)
{
    // Find highest configured acceleration and steps/mm
    float max_accel = 0.0f;
    float max_steps_per_mm = 0.0f;

    if (axis_is_configured(&stepper_system.x))
    {
        if (stepper_system.x.max_accel > max_accel)
            max_accel = stepper_system.x.max_accel;
        if (stepper_system.x.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.x.steps_per_mm;
    }
    if (axis_is_configured(&stepper_system.y))
    {
        if (stepper_system.y.max_accel > max_accel)
            max_accel = stepper_system.y.max_accel;
        if (stepper_system.y.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.y.steps_per_mm;
    }
    if (axis_is_configured(&stepper_system.z))
    {
        if (stepper_system.z.max_accel > max_accel)
            max_accel = stepper_system.z.max_accel;
        if (stepper_system.z.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.z.steps_per_mm;
    }

    if (max_accel <= 0.0f || max_steps_per_mm <= 0.0f)
        return; // No axes configured yet

    // Calculate conservative default: 10× max acceleration
    float candidate_jerk = 10.0f * max_accel;

    // Validate against ISR integer limits
    // jerk_increment must be between 1 and 1000 for integer arithmetic
    const double denom = (double)max_steps_per_mm * (double)RATE_SCALE;
    const double isr2 = (double)ISR_FREQ * (double)ISR_FREQ;
    const double jerk_min = isr2 / denom;            // j_inc == 1
    const double jerk_max = (1000.0 * isr2) / denom; // j_inc == 1000

    // Clamp to valid range
    if ((double)candidate_jerk < jerk_min)
        candidate_jerk = (float)jerk_min;
    if ((double)candidate_jerk > jerk_max)
        candidate_jerk = (float)jerk_max;

    // Only auto-set if not already manually configured
    // (allows user override with STEPPER JERK command)
    if (stepper_jerk_limit_mm_s3 <= 0.0f)
        stepper_jerk_limit_mm_s3 = candidate_jerk;
}

// G28 homing implementation - homes one axis at a time in negative direction
// Two-pass approach: fast approach at 50% speed, then slow precision at 5% speed
static void cmd_stepper_home_axes(int argc, unsigned char **argv)
{
    if (!stepper_initialized)
        stepper_error("Stepper not initialized");

    // Homing generates motion directly (foreground bit-bang stepping).
    // Require no active motion.
    if (stepper_system.motion_active || current_move.phase != MOVE_PHASE_IDLE)
        stepper_error("Cannot home while motion active");

    // Parse which axes to home (X, Y, Z parameters with non-zero values)
    bool home_x = false, home_y = false, home_z = false;

    for (int i = 2; i < argc; i += 4)
    {
        if (i + 2 >= argc)
            break;

        int axis_idx = checkparam((char *)argv[i], 3, "X", "Y", "Z");
        float value = getnumber(argv[i + 2]);
        if (value != 0.0f)
        {
            if (axis_idx == 1 && axis_is_configured(&stepper_system.x))
                home_x = true;
            else if (axis_idx == 2 && axis_is_configured(&stepper_system.y))
                home_y = true;
            else if (axis_idx == 3 && axis_is_configured(&stepper_system.z))
                home_z = true;
        }
    }

    if (!home_x && !home_y && !home_z)
        stepper_error("G28 requires at least one axis specified (X, Y, or Z with non-zero value)");

    // Validate required minimum limit switches for the requested axes only.
    if (!stepper_system.limits_enabled)
        stepper_error("G28 requires minimum limit switches configured (use STEPPER HWLIMITS)");
    if (home_x && stepper_system.x_min_limit_pin == 0xFF)
        stepper_error("G28 requires X minimum limit switch configured");
    if (home_y && stepper_system.y_min_limit_pin == 0xFF)
        stepper_error("G28 requires Y minimum limit switch configured");
    if (home_z && stepper_system.z_min_limit_pin == 0xFF)
        stepper_error("G28 requires Z minimum limit switch configured");

    // Homing redefines machine zero; any queued motion becomes invalid.
    gcode_buffer_init(&gcode_buffer);

    // Enter homing mode
    stepper_system.homing_active = true;
    MMPrintString("Homing axes...\r\n");

    // Home each axis sequentially
    stepper_axis_t *axes[] = {&stepper_system.x, &stepper_system.y, &stepper_system.z};
    bool home_flags[] = {home_x, home_y, home_z};
    uint8_t limit_pins[] = {stepper_system.x_min_limit_pin,
                            stepper_system.y_min_limit_pin,
                            stepper_system.z_min_limit_pin};
    const char *axis_names[] = {"X", "Y", "Z"};

    for (int axis_idx = 0; axis_idx < 3; axis_idx++)
    {
        if (!home_flags[axis_idx])
            continue;

        stepper_axis_t *axis = axes[axis_idx];
        uint8_t limit_pin = limit_pins[axis_idx];
        uint64_t limit_mask = (uint64_t)1 << limit_pin;

        // Fast approach at 50% max speed
        float fast_speed = axis->max_velocity * stepper_system.homing_fast_rate;

        if (fast_speed <= 0.0f || axis->steps_per_mm <= 0.0f)
            stepper_error("Invalid axis configuration for homing");

        // Move in negative direction until limit hit
        bool limit_hit = false;

        // Calculate large negative move distance (will stop at limit)
        float large_distance = -1000.0f; // Move up to 1000mm negative
        int32_t target_steps = axis->current_pos + (int32_t)(large_distance * axis->steps_per_mm);

        // Simplified move: directly control axis at fixed speed
        // This bypasses the G-code buffer for direct control during homing
        uint32_t delay_us = (uint32_t)(1000000.0f / (fast_speed * axis->steps_per_mm));

        while (axis->current_pos > target_steps && !limit_hit)
        {
            // Check limit switch
            uint64_t gpio_state = gpio_get_all64();
            if ((~gpio_state & limit_mask) != 0)
            {
                limit_hit = true;
                break;
            }

            // Take one step in negative direction
            bool dir_level = false; // Negative direction
            if (axis->dir_invert)
                dir_level = !dir_level;
            gpio_put(axis->dir_pin, dir_level);
            busy_wait_us(2);

            gpio_put(axis->step_pin, 1);
            busy_wait_us(2);
            gpio_put(axis->step_pin, 0);

            axis->current_pos--;
            busy_wait_us(delay_us);
        }

        if (!limit_hit)
            stepper_error("Homing failed - limit not reached");

        // Back off until limit clears
        bool dir_level = true; // Positive direction
        if (axis->dir_invert)
            dir_level = !dir_level;
        gpio_put(axis->dir_pin, dir_level);
        busy_wait_us(10);

        for (int i = 0; i < 100; i++) // Max 100 steps backoff
        {
            uint64_t gpio_state = gpio_get_all64();
            if ((gpio_state & limit_mask) != 0)
                break; // Limit cleared

            gpio_put(axis->step_pin, 1);
            busy_wait_us(2);
            gpio_put(axis->step_pin, 0);
            axis->current_pos++;
            busy_wait_us(delay_us * 2); // Slower backoff
        }

        // Slow precision approach at 5% max speed
        float slow_speed = axis->max_velocity * stepper_system.homing_slow_rate;

        if (slow_speed <= 0.0f)
            stepper_error("Invalid axis configuration for homing");
        delay_us = (uint32_t)(1000000.0f / (slow_speed * axis->steps_per_mm));

        // Set direction back to negative
        dir_level = false;
        if (axis->dir_invert)
            dir_level = !dir_level;
        gpio_put(axis->dir_pin, dir_level);
        busy_wait_us(10);

        limit_hit = false;
        for (int i = 0; i < 100; i++) // Max 100 steps for precision
        {
            uint64_t gpio_state = gpio_get_all64();
            if ((~gpio_state & limit_mask) != 0)
            {
                limit_hit = true;
                break;
            }

            gpio_put(axis->step_pin, 1);
            busy_wait_us(2);
            gpio_put(axis->step_pin, 0);
            axis->current_pos--;
            busy_wait_us(delay_us);
        }

        if (!limit_hit)
            stepper_error("Precision homing failed");

        // Final backoff
        dir_level = true;
        if (axis->dir_invert)
            dir_level = !dir_level;
        gpio_put(axis->dir_pin, dir_level);
        busy_wait_us(10);

        for (int i = 0; i < 10; i++)
        {
            uint64_t gpio_state = gpio_get_all64();
            if ((gpio_state & limit_mask) != 0)
                break;

            gpio_put(axis->step_pin, 1);
            busy_wait_us(2);
            gpio_put(axis->step_pin, 0);
            axis->current_pos++;
            busy_wait_us(delay_us * 2);
        }

        // Zero the position
        axis->current_pos = 0;
        axis->target_pos = 0;

        char buf[50];
        sprintf(buf, "%s axis homed and zeroed\r\n", axis_names[axis_idx]);
        MMPrintString(buf);
    }

    // Clear G92 workspace offsets (homing establishes machine zero)
    stepper_system.x_g92_offset = 0.0f;
    stepper_system.y_g92_offset = 0.0f;
    stepper_system.z_g92_offset = 0.0f;

    // Sync planner position to zeroed hardware position (and cleared offsets)
    planner_sync_to_physical();

    // Exit homing mode and mark position as known
    stepper_system.homing_active = false;
    stepper_system.position_known = true;
    MMPrintString("Homing complete\r\n");
}

void cmd_stepper(void)
{
    unsigned char *tp;

    // Block stepper command if I2S audio is configured (shares PWM resources)
    if (Option.audio_i2s_bclk)
        stepper_error("Stepper incompatible with I2S audio");

    // Report any latched hardware limit trip (set by ISR) as soon as we re-enter command context.
    // This avoids "silent" stops where the buffer is cleared and mode forced to TEST.
    if (stepper_limit_trip_latched)
    {
        uint64_t mask;
        uint32_t save = save_and_disable_interrupts();
        mask = stepper_limit_trip_mask;
        stepper_limit_trip_mask = 0;
        stepper_limit_trip_latched = false;
        restore_interrupts(save);

        MMPrintString("Hardware limit switch trip - emergency stop\r\n");

        // Best-effort decode of which configured limits were asserted.
        // (Pins are active-low; mask bits are GP numbers.)
        char which[120];
        which[0] = 0;
        bool first = true;
        struct
        {
            const char *name;
            uint8_t pin;
        } lims[] = {
            {"X_MIN", stepper_system.x_min_limit_pin},
            {"X_MAX", stepper_system.x_max_limit_pin},
            {"Y_MIN", stepper_system.y_min_limit_pin},
            {"Y_MAX", stepper_system.y_max_limit_pin},
            {"Z_MIN", stepper_system.z_min_limit_pin},
            {"Z_MAX", stepper_system.z_max_limit_pin},
        };

        for (unsigned i = 0; i < sizeof(lims) / sizeof(lims[0]); i++)
        {
            if (lims[i].pin == 0xFF)
                continue;
            if ((mask & (1ULL << lims[i].pin)) == 0)
                continue;

            if (!first)
                strncat(which, ", ", sizeof(which) - strlen(which) - 1);
            strncat(which, lims[i].name, sizeof(which) - strlen(which) - 1);
            first = false;
        }

        if (!first)
        {
            MMPrintString("Asserted: ");
            MMPrintString(which);
            MMPrintString("\r\n");
        }

        MMPrintString("Mode forced to TEST, buffer cleared, position unknown\r\n");
        MMPrintString("Clear the switch condition and re-home (G28), then STEPPER RUN\r\n");
    }

    // STEPPER INIT [arc_tolerance] [,buffer_size] - Initialize stepper system, G-code buffer, and 100KHz timer interrupt
    if ((tp = checkstring(cmdline, (unsigned char *)"INIT")) != NULL)
    {
        if (stepper_initialized)
            stepper_error("Stepper already initialized");

        float tol = DEFAULT_ARC_TOLERANCE_MM;
        bool tol_set = false;
        int buf_size = GCODE_BUFFER_DEFAULT_SIZE;

        // Parse optional parameters. Accepts either order.
        // Disambiguation rule:
        //   - 0 < value < 1.0 => arc tolerance (mm)
        //   - integer value >=16 => buffer size
        // Examples:
        //   STEPPER INIT
        //   STEPPER INIT 0.25
        //   STEPPER INIT 64
        //   STEPPER INIT 0.25,64
        //   STEPPER INIT 64,0.25
        getcsargs(&tp, 3);
        if (!(argc == 0 || argc == 1 || argc == 3))
            stepper_error("Syntax: STEPPER INIT [arc_tolerance] [,buffer_size]");

        bool have_tol = false;
        bool have_buf = false;
        for (int i = 0; i < argc; i += 2)
        {
            double v = (double)getnumber(argv[i]);
            if (v > 0.0 && v < 1.0)
            {
                if (have_tol)
                    stepper_error("Syntax: STEPPER INIT [arc_tolerance] [,buffer_size]");
                have_tol = true;
                tol = (float)v;
                tol_set = true;
                continue;
            }

            if (v >= 16.0 && v <= (double)GCODE_BUFFER_MAX_SIZE)
            {
                long iv = (long)v;
                if ((double)iv != v)
                    stepper_error("Syntax: STEPPER INIT [arc_tolerance] [,buffer_size]");
                if (have_buf)
                    stepper_error("Syntax: STEPPER INIT [arc_tolerance] [,buffer_size]");
                have_buf = true;
                buf_size = (int)iv;
                continue;
            }

            stepper_error("Syntax: STEPPER INIT [arc_tolerance] [,buffer_size]");
        }

        if (tol_set && tol <= 0.0f)
            stepper_error("Arc tolerance must be > 0");
        stepper_arc_tolerance_mm = tol;
        stepper_arc_max_angle_rad = DEFAULT_ARC_MAX_ANGULAR_DEVIATION_RAD;
        stepper_arc_segment_cap = DEFAULT_ARC_SEGMENT_CAP;

        // Check for conflict with EXT_FAST_TIMER (uses same IRQ)
        if (ExtCurrentConfig[FAST_TIMER_PIN] == EXT_FAST_TIMER)
            stepper_error("Stepper incompatible with FAST TIMER");

#ifdef rp2350
        // Prevent STEPPER INIT if PWM slice 10 is already in use (audio/backlight/camera or user PWM10).
        if (Option.AUDIO_SLICE == STEPPER_PWM_SLICE || BacklightSlice == STEPPER_PWM_SLICE || CameraSlice == STEPPER_PWM_SLICE)
            stepper_error("Stepper timer slice in use");
        for (int i = 1; i <= NBRPINS; i++)
        {
            if (ExtCurrentConfig[i] == EXT_PWM10A || ExtCurrentConfig[i] == EXT_PWM10B)
                stepper_error("Stepper timer slice in use");
        }
#endif

        // Initialize all axes to defaults
        stepper_axis_init(&stepper_system.x);
        stepper_axis_init(&stepper_system.y);
        stepper_axis_init(&stepper_system.z);

        // Derive per-axis minimum step tick spacing from configured limits.
        stepper_recompute_axis_step_tick_limits();
        stepper_system.motion_active = false;
        stepper_system.step_interval = 10; // 10 microseconds base interval

        // Initialize limit switches to disabled
        stepper_system.x_min_limit_pin = 0xFF;
        stepper_system.x_max_limit_pin = 0xFF;
        stepper_system.y_min_limit_pin = 0xFF;
        stepper_system.y_max_limit_pin = 0xFF;
        stepper_system.z_min_limit_pin = 0xFF;
        stepper_system.z_max_limit_pin = 0xFF;
        stepper_system.limit_switch_mask = 0;
        stepper_system.limits_enabled = false;

        // Initialize homing parameters
        stepper_system.homing_active = false;
        stepper_system.homing_fast_rate = 0.5f;  // 50% of max speed
        stepper_system.homing_slow_rate = 0.05f; // 5% of max speed

        // Initialize G92 workspace offsets
        stepper_system.x_g92_offset = 0.0f;
        stepper_system.y_g92_offset = 0.0f;
        stepper_system.z_g92_offset = 0.0f;
        stepper_system.position_known = false;

        // Initialize spindle control
        stepper_system.spindle_pin = 0xFF;
        stepper_system.spindle_invert = false;
        stepper_system.spindle_on = false;

        stepper_reset_accumulator_carry();

        // Allocate and initialize the G-code circular buffer
        gcode_buffer_allocate(&gcode_buffer, (uint16_t)buf_size);
        gcode_buffer_init(&gcode_buffer);

        // Set up PWM slice 10 for 100KHz interrupt (10 microsecond period)
        // System clock is typically 150MHz on RP2350
        // For 100KHz: 150MHz / 100KHz = 1500 counts
        // Using divider of 1, wrap value of 1499 gives 100KHz
        uint32_t sys_clk = clock_get_hz(clk_sys);
        uint32_t target_freq = 100000; // 100KHz
        uint16_t wrap_value = (sys_clk / target_freq) - 1;

        // Configure PWM slice 10 for 100KHz
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 1.0f); // No clock division
        pwm_config_set_wrap(&cfg, wrap_value);
        pwm_init(STEPPER_PWM_SLICE, &cfg, false); // Don't start yet

        // Start disarmed; user must explicitly arm with STEPPER RUN.
        stepper_armed = false;

        // Clear any pending interrupt
        pwm_clear_irq(STEPPER_PWM_SLICE);

        // Set up interrupt handler on PWM_IRQ_WRAP_1
        irq_set_exclusive_handler(PWM_IRQ_WRAP_1, stepper_timer_isr);
        irq_set_enabled(PWM_IRQ_WRAP_1, true);
        irq_set_priority(PWM_IRQ_WRAP_1, 0); // Highest priority for timing critical

        // Enable interrupt for PWM slice 10 on IRQ1
        pwm_set_irq1_enabled(STEPPER_PWM_SLICE, true);

        // Start the PWM timer
        pwm_set_enabled(STEPPER_PWM_SLICE, true);

        stepper_initialized = true;
        stepper_tick_count = 0;

        // Reset per-axis timing history.
        stepper_axis_last_step_tick[0] = 0;
        stepper_axis_last_step_tick[1] = 0;
        stepper_axis_last_step_tick[2] = 0;

        // Warn if soft limits not configured
        MMPrintString("Stepper initialized - 100KHz timer active\r\n");
        MMPrintString("Warning: Soft limits not configured. Use STEPPER LIMITS to set working area.\r\n");

        return;
    }

    // STEPPER CLOSE - Shutdown stepper system and disable timer interrupt
    if (checkstring(cmdline, (unsigned char *)"CLOSE"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        stepper_close_subsystem();

        return;
    }

    // STEPPER ESTOP - Emergency stop: halt motion immediately, clear buffer, disable drivers
    if (checkstring(cmdline, (unsigned char *)"ESTOP"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        uint32_t save = save_and_disable_interrupts();

        // Stop any executing move immediately
        current_move.phase = MOVE_PHASE_IDLE;
        current_move.step_rate = 0;
        current_move.cruise_rate = 0;
        current_move.exit_rate = 0;
        current_move.accel_increment = 0;
        current_move.step_accumulator = 0;
        current_move.total_steps_remaining = 0;
        current_move.steps_remaining = 0;
        stepper_reset_accumulator_carry();

        stepper_system.motion_active = false;

        // Clear buffer and reset modal state
        gcode_buffer_release_all_payloads(&gcode_buffer);
        gcode_buffer.head = 0;
        gcode_buffer.tail = 0;
        gcode_buffer.count = 0;
        gcode_buffer.buffer_full = false;
        gcode_buffer.buffer_empty = true;

        // Empty buffer => full space available
        stepper_gcode_buffer_space = gcode_buffer.size;
        gcode_buffer.current_motion_mode = GCODE_LINEAR_MOVE;
        gcode_buffer.current_feedrate = 0.0f;
        gcode_buffer.feedrate_set = false;
        gcode_buffer.absolute_mode = true; // Default to G90

        // Disarm to prevent any auto-execution until explicit STEPPER RUN
        stepper_armed = false;

        // Disable drivers (active low enable pins)
        if (stepper_system.x.enable_pin != 0xFF)
            gpio_put(stepper_system.x.enable_pin, 1);
        if (stepper_system.y.enable_pin != 0xFF)
            gpio_put(stepper_system.y.enable_pin, 1);
        if (stepper_system.z.enable_pin != 0xFF)
            gpio_put(stepper_system.z.enable_pin, 1);

        // Disable spindle
        stepper_spindle_off();

        // Keep planner consistent with the physical position at time of stop
        planner_sync_to_physical();

        restore_interrupts(save);

        MMPrintString("Emergency stop - motion halted, buffer cleared, drivers disabled\r\n");
        MMPrintString("Use STEPPER RUN to arm\r\n");
        return;
    }

    // STEPPER RECOVER - Recover from an abnormal state (eg. after a runtime error)
    if (checkstring(cmdline, (unsigned char *)"RECOVER"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        stepper_recover_to_safe_state();
        MMPrintString("Stepper recovered - disarmed, buffer cleared\r\n");
        return;
    }

    // STEPPER AXIS X|Y|Z, step_pin, dir_pin [, enable_pin] [, dir_invert] [, steps_per_mm] [, max_velocity] [, max_accel]
    if ((tp = checkstring(cmdline, (unsigned char *)"AXIS")) != NULL)
    {
        getcsargs(&tp, 15);
        if (argc < 5)
            stepper_error("Syntax");

        int axis_idx = checkparam((char *)argv[0], 3, "X", "Y", "Z");
        if (axis_idx == 0)
            stepper_error("Axis must be X, Y, or Z");

        const char axis_chars[] = "XYZ";
        stepper_axis_t *axis = get_axis_ptr(axis_chars[axis_idx - 1]);
        if (axis == NULL)
            stepper_error("Axis must be X, Y, or Z");

        // Initialize axis to defaults first
        stepper_axis_init(axis);

        // Get step pin (required) - accepts physical pin or GPxx format
        int step_pin = parse_pin(argv[2]);
        if (IsInvalidPin(step_pin))
            stepper_error("Invalid step pin");
        CheckPin(step_pin, CP_IGNORE_INUSE);
        axis->step_pin = PinDef[step_pin].GPno;

        // Get direction pin (required) - accepts physical pin or GPxx format
        int dir_pin = parse_pin(argv[4]);
        if (IsInvalidPin(dir_pin))
            stepper_error("Invalid dir pin");
        CheckPin(dir_pin, CP_IGNORE_INUSE);
        axis->dir_pin = PinDef[dir_pin].GPno;

        // Configure step and dir pins as outputs
        gpio_init(axis->step_pin);
        gpio_set_dir(axis->step_pin, GPIO_OUT);
        gpio_put(axis->step_pin, 0);
        ExtCfg(step_pin, EXT_DIG_OUT, 0);
        ExtCfg(step_pin, EXT_COM_RESERVED, 0);

        gpio_init(axis->dir_pin);
        gpio_set_dir(axis->dir_pin, GPIO_OUT);
        gpio_put(axis->dir_pin, 0);
        ExtCfg(dir_pin, EXT_DIG_OUT, 0);
        ExtCfg(dir_pin, EXT_COM_RESERVED, 0);

        // Optional: enable pin (0 means not used)
        if (argc >= 7 && *argv[6])
        {
            int enable_pin = parse_pin(argv[6]);
            if (enable_pin != 0)
            {
                if (IsInvalidPin(enable_pin))
                    stepper_error("Invalid enable pin");
                CheckPin(enable_pin, CP_IGNORE_INUSE);
                axis->enable_pin = PinDef[enable_pin].GPno;
                gpio_init(axis->enable_pin);
                gpio_set_dir(axis->enable_pin, GPIO_OUT);
                gpio_put(axis->enable_pin, 1); // Typically active low, so disable by default
                ExtCfg(enable_pin, EXT_DIG_OUT, 0);
                ExtCfg(enable_pin, EXT_COM_RESERVED, 0);
            }
        }

        // Optional: direction invert (0 or 1)
        if (argc >= 9 && *argv[8])
        {
            axis->dir_invert = getint(argv[8], 0, 1);
        }

        // Optional: steps per mm
        if (argc >= 11 && *argv[10])
        {
            axis->steps_per_mm = getnumber(argv[10]);
            if (axis->steps_per_mm <= 0)
                stepper_error("Steps per mm must be > 0");
        }

        // Optional: max velocity (mm/min)
        // Internally we use mm/s, so convert.
        if (argc >= 13 && *argv[12])
        {
            float max_vel_mm_min = getnumber(argv[12]);
            if (max_vel_mm_min <= 0.0f)
                stepper_error("Max velocity must be > 0");
            axis->max_velocity = max_vel_mm_min / 60.0f;
        }

        // Optional: max acceleration (mm/s²)
        if (argc >= 15 && *argv[14])
        {
            axis->max_accel = getnumber(argv[14]);
            if (axis->max_accel <= 0)
                stepper_error("Max acceleration must be > 0");
        }

        // Update ISR hard-timing limits for this axis configuration.
        {
            uint32_t save = save_and_disable_interrupts();
            stepper_recompute_axis_step_tick_limits();
            restore_interrupts(save);
        }

        // Auto-calculate reasonable default jerk based on configured axes
        // User can still override with explicit STEPPER JERK command
        calculate_default_jerk();

        return;
    }

    // STEPPER JERK jerk_mm_s^3
    // Manually sets the jerk limit (mm/s^3) used by future S-curve planning.
    // Optional: a default jerk is auto-calculated when axes are configured.
    // Use this command to override the default for fine-tuning.
    if ((tp = checkstring(cmdline, (unsigned char *)"JERK")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        getcsargs(&tp, 1);
        if (argc != 1)
            stepper_error("Syntax");

        float jerk = getnumber(argv[0]);
        if (jerk <= 0.0f)
            stepper_error("Jerk must be > 0");

        // Gate: ensure at least one axis is configured
        // (default jerk is auto-calculated, but allow manual override)
        float max_steps_per_mm = 0.0f;
        if (axis_is_configured(&stepper_system.x) && stepper_system.x.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.x.steps_per_mm;
        if (axis_is_configured(&stepper_system.y) && stepper_system.y.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.y.steps_per_mm;
        if (axis_is_configured(&stepper_system.z) && stepper_system.z.steps_per_mm > max_steps_per_mm)
            max_steps_per_mm = stepper_system.z.steps_per_mm;

        if (max_steps_per_mm <= 0.0f)
            stepper_error("No axes configured");

        // Hard limits derived from integer jerk increment resolution at 100kHz.
        // Future jerk increment: j_inc = j * steps/mm * RATE_SCALE / ISR_FREQ^2
        // Enforce 1 <= j_inc <= JINC_MAX to keep values representable and bounded.
        const double denom = (double)max_steps_per_mm * (double)RATE_SCALE;
        const double isr2 = (double)ISR_FREQ * (double)ISR_FREQ;
        const double jerk_min = isr2 / denom;            // j_inc == 1
        const double jerk_max = (1000.0 * isr2) / denom; // j_inc == 1000

        if ((double)jerk < jerk_min)
            stepper_error("Jerk too low for configured steps/mm");
        if ((double)jerk > jerk_max)
            stepper_error("Jerk too high for configured steps/mm");

        stepper_jerk_limit_mm_s3 = jerk;
        return;
    }

    // STEPPER SCURVE 0|1
    // Enable/disable jerk-limited S-curve planning for standalone G0/G1 blocks.
    // Arc segments remain on the existing approach initially.
    if ((tp = checkstring(cmdline, (unsigned char *)"SCURVE")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        getcsargs(&tp, 1);
        if (argc != 1)
            stepper_error("Syntax");

        stepper_scurve_enable = (getint(argv[0], 0, 1) != 0);
        return;
    }

    if ((tp = checkstring(cmdline, (unsigned char *)"ARC")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        char *s = (char *)tp;
        skipspace(s);
        if (*s == 0)
        {
            char buf[160];
            float angle_deg = stepper_arc_max_angle_rad * (180.0f / (float)M_PI);
            snprintf(buf, sizeof buf,
                     "Arc tolerance %.3f mm, angular step %.2f deg, max segments %u\r\n",
                     (double)stepper_arc_tolerance_mm,
                     (double)angle_deg,
                     (unsigned int)stepper_arc_segment_cap);
            MMPrintString(buf);
            return;
        }

        getcsargs(&tp, 5);
        if (!(argc == 1 || argc == 3 || argc == 5))
            stepper_error("Syntax: STEPPER ARC tolerance[,angle_deg][,max_segments]");

        float tol = getnumber(argv[0]);
        if (tol <= 0.0f)
            stepper_error("Arc tolerance must be > 0");

        float angle_deg = stepper_arc_max_angle_rad * (180.0f / (float)M_PI);
        int cap = (int)stepper_arc_segment_cap;

        if (argc >= 3 && *argv[2])
            angle_deg = getnumber(argv[2]);
        if (argc >= 5 && *argv[4])
            cap = getint(argv[4], MIN_ARC_SEGMENTS, ARC_SEGMENT_CAP_MAX);

        if (angle_deg <= 0.0f)
            stepper_error("Angle must be > 0");
        if (angle_deg > 180.0f)
            angle_deg = 180.0f;

        if (cap < MIN_ARC_SEGMENTS || cap > ARC_SEGMENT_CAP_MAX)
            stepper_error("Max segments out of range");

        stepper_arc_tolerance_mm = tol;
        stepper_arc_max_angle_rad = angle_deg * ((float)M_PI / 180.0f);
        stepper_arc_segment_cap = (uint16_t)cap;

        char buf[160];
        snprintf(buf, sizeof buf,
                 "Arc settings: %.3f mm tol, %.2f deg max step, %u seg cap\r\n",
                 (double)stepper_arc_tolerance_mm,
                 (double)angle_deg,
                 (unsigned int)stepper_arc_segment_cap);
        MMPrintString(buf);
        return;
    }

    // STEPPER RESET - Reset all axes to default state
    if (checkstring(cmdline, (unsigned char *)"RESET"))
    {
        // Ensure spindle is off when resetting configuration
        stepper_spindle_off();
        stepper_axis_init(&stepper_system.x);
        stepper_axis_init(&stepper_system.y);
        stepper_axis_init(&stepper_system.z);

        {
            uint32_t save = save_and_disable_interrupts();
            stepper_recompute_axis_step_tick_limits();
            stepper_axis_last_step_tick[0] = 0;
            stepper_axis_last_step_tick[1] = 0;
            stepper_axis_last_step_tick[2] = 0;
            restore_interrupts(save);
        }
        stepper_system.motion_active = false;
        stepper_system.step_interval = 0;

        // Clear spindle configuration
        stepper_system.spindle_pin = 0xFF;
        stepper_system.spindle_invert = false;
        stepper_system.spindle_on = false;
        return;
    }

    // STEPPER SPINDLE pin [,invert] - Configure spindle enable output
    if ((tp = checkstring(cmdline, (unsigned char *)"SPINDLE")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        getcsargs(&tp, 3);
        if (!(argc == 1 || argc == 3))
            stepper_error("Syntax: STEPPER SPINDLE pin [,invert]");

        int spindle_pin = parse_pin(argv[0]);
        if (spindle_pin == 0)
        {
            // Disable spindle control
            stepper_spindle_off();
            stepper_system.spindle_pin = 0xFF;
            stepper_system.spindle_invert = false;
            stepper_system.spindle_on = false;
            return;
        }

        if (IsInvalidPin(spindle_pin))
            stepper_error("Invalid spindle pin");
        CheckPin(spindle_pin, CP_IGNORE_INUSE);

        stepper_system.spindle_pin = PinDef[spindle_pin].GPno;
        stepper_system.spindle_invert = (argc == 3) ? (getint(argv[2], 0, 1) != 0) : false;
        stepper_system.spindle_on = false;

        gpio_init(stepper_system.spindle_pin);
        gpio_set_dir(stepper_system.spindle_pin, GPIO_OUT);
        stepper_spindle_off();
        ExtCfg(spindle_pin, EXT_DIG_OUT, 0);
        return;
    }

    // STEPPER HWLIMITS X_MIN, Y_MIN, Z_MIN [,X_MAX] [,Y_MAX] [,Z_MAX]
    // Configure hardware limit switch pins (3 to 6 pins)
    // Pins are active-low (triggered when grounded)
    if ((tp = checkstring(cmdline, (unsigned char *)"HWLIMITS")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        getcsargs(&tp, 11);
        if (argc < 5)
            stepper_error("Syntax: STEPPER HWLIMITS x_min, y_min, z_min [,x_max] [,y_max] [,z_max]");

        // Read minimum limit pins (required)
        int x_min_pin = parse_pin(argv[0]);
        int y_min_pin = parse_pin(argv[2]);
        int z_min_pin = parse_pin(argv[4]);

        if (IsInvalidPin(x_min_pin) || IsInvalidPin(y_min_pin) || IsInvalidPin(z_min_pin))
            stepper_error("Invalid limit pin");

        CheckPin(x_min_pin, CP_IGNORE_INUSE);
        CheckPin(y_min_pin, CP_IGNORE_INUSE);
        CheckPin(z_min_pin, CP_IGNORE_INUSE);

        stepper_system.x_min_limit_pin = PinDef[x_min_pin].GPno;
        stepper_system.y_min_limit_pin = PinDef[y_min_pin].GPno;
        stepper_system.z_min_limit_pin = PinDef[z_min_pin].GPno;

        // Configure as inputs with pull-ups (active-low switches)
        gpio_init(stepper_system.x_min_limit_pin);
        gpio_set_dir(stepper_system.x_min_limit_pin, GPIO_IN);
        gpio_pull_up(stepper_system.x_min_limit_pin);
        ExtCfg(x_min_pin, EXT_DIG_IN, 0);

        gpio_init(stepper_system.y_min_limit_pin);
        gpio_set_dir(stepper_system.y_min_limit_pin, GPIO_IN);
        gpio_pull_up(stepper_system.y_min_limit_pin);
        ExtCfg(y_min_pin, EXT_DIG_IN, 0);

        gpio_init(stepper_system.z_min_limit_pin);
        gpio_set_dir(stepper_system.z_min_limit_pin, GPIO_IN);
        gpio_pull_up(stepper_system.z_min_limit_pin);
        ExtCfg(z_min_pin, EXT_DIG_IN, 0);

        // Build initial mask with minimum limits
        uint64_t mask = ((uint64_t)1 << stepper_system.x_min_limit_pin) |
                        ((uint64_t)1 << stepper_system.y_min_limit_pin) |
                        ((uint64_t)1 << stepper_system.z_min_limit_pin);

        // Read optional maximum limit pins
        stepper_system.x_max_limit_pin = 0xFF;
        stepper_system.y_max_limit_pin = 0xFF;
        stepper_system.z_max_limit_pin = 0xFF;

        if (argc >= 7 && *argv[6])
        {
            int x_max_pin = parse_pin(argv[6]);
            if (!IsInvalidPin(x_max_pin) && x_max_pin != 0)
            {
                CheckPin(x_max_pin, CP_IGNORE_INUSE);
                stepper_system.x_max_limit_pin = PinDef[x_max_pin].GPno;
                gpio_init(stepper_system.x_max_limit_pin);
                gpio_set_dir(stepper_system.x_max_limit_pin, GPIO_IN);
                gpio_pull_up(stepper_system.x_max_limit_pin);
                ExtCfg(x_max_pin, EXT_DIG_IN, 0);
                mask |= ((uint64_t)1 << stepper_system.x_max_limit_pin);
            }
        }

        if (argc >= 9 && *argv[8])
        {
            int y_max_pin = parse_pin(argv[8]);
            if (!IsInvalidPin(y_max_pin) && y_max_pin != 0)
            {
                CheckPin(y_max_pin, CP_IGNORE_INUSE);
                stepper_system.y_max_limit_pin = PinDef[y_max_pin].GPno;
                gpio_init(stepper_system.y_max_limit_pin);
                gpio_set_dir(stepper_system.y_max_limit_pin, GPIO_IN);
                gpio_pull_up(stepper_system.y_max_limit_pin);
                ExtCfg(y_max_pin, EXT_DIG_IN, 0);
                mask |= ((uint64_t)1 << stepper_system.y_max_limit_pin);
            }
        }

        if (argc >= 11 && *argv[10])
        {
            int z_max_pin = parse_pin(argv[10]);
            if (!IsInvalidPin(z_max_pin) && z_max_pin != 0)
            {
                CheckPin(z_max_pin, CP_IGNORE_INUSE);
                stepper_system.z_max_limit_pin = PinDef[z_max_pin].GPno;
                gpio_init(stepper_system.z_max_limit_pin);
                gpio_set_dir(stepper_system.z_max_limit_pin, GPIO_IN);
                gpio_pull_up(stepper_system.z_max_limit_pin);
                ExtCfg(z_max_pin, EXT_DIG_IN, 0);
                mask |= ((uint64_t)1 << stepper_system.z_max_limit_pin);
            }
        }

        // Store mask and enable limit checking
        stepper_system.limit_switch_mask = mask;
        stepper_system.limits_enabled = true;

        MMPrintString("Hardware limit switches configured\r\n");
        return;
    }

    // STEPPER LIMITS X|Y|Z, min_mm, max_mm - Set soft limits for an axis
    // Limits are in mm (or user units). Motion commands that would exceed limits generate an error.
    if ((tp = checkstring(cmdline, (unsigned char *)"LIMITS")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        getcsargs(&tp, 5);
        if (argc < 5)
            stepper_error("Syntax: STEPPER LIMITS axis, min_mm, max_mm");

        int axis_idx = checkparam((char *)argv[0], 3, "X", "Y", "Z");
        if (axis_idx == 0)
            stepper_error("Axis must be X, Y, or Z");

        const char axis_chars[] = "XYZ";
        stepper_axis_t *axis = get_axis_ptr(axis_chars[axis_idx - 1]);
        if (axis == NULL)
            stepper_error("Axis must be X, Y, or Z");

        if (!axis_is_configured(axis))
            stepper_error("Axis not configured");

        // Get min and max in mm
        float min_mm = getnumber(argv[2]);
        float max_mm = getnumber(argv[4]);

        if (min_mm >= max_mm)
            stepper_error("Min must be less than max");

        // Convert to steps and store
        axis->min_limit = (int32_t)(min_mm * axis->steps_per_mm);
        axis->max_limit = (int32_t)(max_mm * axis->steps_per_mm);

        char buf[80];
        sprintf(buf, "%c axis limits: %.3f to %.3f mm (%ld to %ld steps)\r\n",
                axis_chars[axis_idx - 1], (double)min_mm, (double)max_mm,
                (long)axis->min_limit, (long)axis->max_limit);
        MMPrintString(buf);

        return;
    }

    // STEPPER ENABLE X|Y|Z|ALL [, 0|1]
    if ((tp = checkstring(cmdline, (unsigned char *)"ENABLE")) != NULL)
    {
        getcsargs(&tp, 3);
        if (argc < 1)
            stepper_error("Syntax");

        bool enable = true;
        if (argc >= 3)
            enable = getint(argv[2], 0, 1);

        int axis_sel = checkparam((char *)argv[0], 4, "ALL", "X", "Y", "Z");
        if (axis_sel == 0)
            stepper_error("Axis must be X, Y, Z, or ALL");

        if (axis_sel == 1)
        {
            // Enable/disable all axes
            if (stepper_system.x.enable_pin != 0xFF)
                gpio_put(stepper_system.x.enable_pin, enable ? 0 : 1);
            if (stepper_system.y.enable_pin != 0xFF)
                gpio_put(stepper_system.y.enable_pin, enable ? 0 : 1);
            if (stepper_system.z.enable_pin != 0xFF)
                gpio_put(stepper_system.z.enable_pin, enable ? 0 : 1);
        }
        else
        {
            const char axis_chars[] = "XYZ";
            stepper_axis_t *axis = get_axis_ptr(axis_chars[axis_sel - 2]);
            if (axis == NULL)
                stepper_error("Axis must be X, Y, Z, or ALL");
            if (axis->enable_pin == 0xFF)
                stepper_error("Enable pin not configured for this axis");
            gpio_put(axis->enable_pin, enable ? 0 : 1); // Active low
        }
        return;
    }

    // STEPPER INVERT X|Y|Z, 0|1 - Invert direction for an axis
    if ((tp = checkstring(cmdline, (unsigned char *)"INVERT")) != NULL)
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            stepper_error("Syntax");

        int axis_idx = checkparam((char *)argv[0], 3, "X", "Y", "Z");
        if (axis_idx == 0)
            stepper_error("Axis must be X, Y, or Z");

        const char axis_chars[] = "XYZ";
        stepper_axis_t *axis = get_axis_ptr(axis_chars[axis_idx - 1]);
        if (axis == NULL)
            stepper_error("Axis must be X, Y, or Z");

        axis->dir_invert = getint(argv[2], 0, 1);
        return;
    }

    // STEPPER POSITION HOME - Set all axes position to 0 (and clear G92 offsets)
    // STEPPER POSITION X|Y|Z, position - Set current position for an axis
    if ((tp = checkstring(cmdline, (unsigned char *)"POSITION")) != NULL)
    {
        // Fast path: STEPPER POSITION HOME
        char *s = (char *)tp;
        skipspace(s);
        unsigned char *t = checkstring((unsigned char *)s, (unsigned char *)"HOME");
        if (t != NULL)
        {
            skipspace((char *)t);
            if (*t)
                stepper_error("Syntax");

            stepper_system.x.current_pos = 0;
            stepper_system.x.target_pos = 0;
            stepper_system.y.current_pos = 0;
            stepper_system.y.target_pos = 0;
            stepper_system.z.current_pos = 0;
            stepper_system.z.target_pos = 0;

            // Clear workspace offsets so reported/planned coordinates are also at 0.
            stepper_system.x_g92_offset = 0.0f;
            stepper_system.y_g92_offset = 0.0f;
            stepper_system.z_g92_offset = 0.0f;

            stepper_system.position_known = true;
            planner_sync_to_physical();
            return;
        }

        getcsargs(&tp, 3);
        if (argc != 3)
            stepper_error("Syntax");

        int axis_idx = checkparam((char *)argv[0], 3, "X", "Y", "Z");
        if (axis_idx == 0)
            stepper_error("Axis must be X, Y, or Z");

        const char axis_chars[] = "XYZ";
        stepper_axis_t *axis = get_axis_ptr(axis_chars[axis_idx - 1]);
        if (axis == NULL)
            stepper_error("Axis must be X, Y, or Z");

        float pos_mm = getnumber(argv[2]);
        axis->current_pos = (int32_t)(pos_mm * axis->steps_per_mm);
        axis->target_pos = axis->current_pos;
        stepper_system.position_known = true; // Machine position now known

        // Keep planner position consistent with new physical position.
        planner_sync_to_physical();
        return;
    }

    // STEPPER GC <gcode> [words...] - Normal G-code word format (space-separated)
    // Examples:
    //   STEPPER GC G1 X0 Y0 F300
    //   STEPPER GC G0 X10
    //   STEPPER GC G2 X10 Y0 I5 J0 F600
    //   STEPPER GC G28 X Y
    //   STEPPER GC G92 X10 Y20
    // Notes:
    // - This is in addition to STEPPER GCODE (comma-separated); STEPPER GCODE is unchanged.
    // - Words may be supplied as X10 or as X 10 (commas are also accepted as separators).
    if ((tp = checkstring(cmdline, (unsigned char *)"GC")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        // MMBasic may tokenise operators like '-' and '+' inside the command line.
        // Accept these token values as numeric signs so words like Z-2 parse correctly.
        const unsigned char tok_minus = GetTokenValue((unsigned char *)"-");
        const unsigned char tok_plus = GetTokenValue((unsigned char *)"+");

        gcode_block_t block = {0};
        block.is_planned = false;

        // Standalone parser: scan tp directly (standard G-code word format).
        // Supports: G0/G1/G2/G3, G28, G90/G91, G61/G64, G92, M3/M5 and words X/Y/Z/F/I/J/K/R.
        char *s = (char *)tp;

        int primary_g = -1;           // 0/1/2/3/28/92
        bool primary_is_home = false; // G28
        bool primary_is_g92 = false;  // G92
        int spindle_m = -1;           // 3/5

        bool word_x = false, word_y = false, word_z = false;
        bool word_x_has = false, word_y_has = false, word_z_has = false;
        double word_x_val = 0.0, word_y_val = 0.0, word_z_val = 0.0;

        bool word_f = false, word_i = false, word_j = false, word_k = false, word_r = false;
        double word_f_val = 0.0, word_i_val = 0.0, word_j_val = 0.0, word_k_val = 0.0, word_r_val = 0.0;

        while (1)
        {
            // Skip separators
            while (*s == ' ' || *s == '\t' || *s == ',')
                s++;

            // End-of-line or comment
            if (*s == 0 || *s == '\r' || *s == '\n' || *s == ';')
                break;

            // Parentheses comment
            if (*s == '(')
            {
                while (*s && *s != ')')
                    s++;
                if (*s == ')')
                    s++;
                continue;
            }

            char letter = (char)toupper((unsigned char)*s++);
            while (*s == ' ' || *s == '\t')
                s++;

            if (letter == 'G' || letter == 'M')
            {
                char *endp = s;
                long code = strtol(s, &endp, 10);
                if (endp == s)
                    stepper_error("Syntax");
                s = endp;

                if (letter == 'M')
                {
                    if (code == 3)
                        spindle_m = 3;
                    else if (code == 5)
                        spindle_m = 5;
                    else
                        stepper_error("Unsupported M-code");
                    continue;
                }

                // G-code
                if (code == 0 || code == 1 || code == 2 || code == 3)
                {
                    primary_g = (int)code;
                    primary_is_home = false;
                    primary_is_g92 = false;
                }
                else if (code == 28)
                {
                    primary_g = 28;
                    primary_is_home = true;
                    primary_is_g92 = false;
                }
                else if (code == 92)
                {
                    primary_g = 92;
                    primary_is_home = false;
                    primary_is_g92 = true;
                }
                else if (code == 90)
                {
                    gcode_buffer.absolute_mode = true;
                }
                else if (code == 91)
                {
                    gcode_buffer.absolute_mode = false;
                }
                else if (code == 61)
                {
                    gcode_buffer.exact_stop_mode = true;
                }
                else if (code == 64)
                {
                    gcode_buffer.exact_stop_mode = false;
                }
                else
                {
                    stepper_error("Unsupported G-code");
                }

                continue;
            }

            // Parse optional numeric value for this word.
            bool has_value = false;
            double value = 0.0;
            {
                unsigned char c0 = (unsigned char)*s;
                if (c0 == tok_minus || c0 == tok_plus)
                {
                    bool neg = (c0 == tok_minus);
                    s++; // skip tokenised sign
                    while (*s == ' ' || *s == '\t')
                        s++;
                    char *endp = s;
                    double mag = strtod(s, &endp);
                    if (endp == s)
                        stepper_error("Syntax");
                    s = endp;
                    value = neg ? -mag : mag;
                    has_value = true;
                }
                else if (*s == '+' || *s == '-' || *s == '.' || (*s >= '0' && *s <= '9'))
                {
                    char *endp = s;
                    value = strtod(s, &endp);
                    if (endp == s)
                        stepper_error("Syntax");
                    s = endp;
                    has_value = true;
                }
            }

            switch (letter)
            {
            case 'X':
                word_x = true;
                word_x_has = has_value;
                word_x_val = value;
                break;
            case 'Y':
                word_y = true;
                word_y_has = has_value;
                word_y_val = value;
                break;
            case 'Z':
                word_z = true;
                word_z_has = has_value;
                word_z_val = value;
                break;
            case 'F':
                if (!has_value)
                    stepper_error("Missing value");
                word_f = true;
                word_f_val = value;
                break;
            case 'I':
                if (!has_value)
                    stepper_error("Missing value");
                word_i = true;
                word_i_val = value;
                break;
            case 'J':
                if (!has_value)
                    stepper_error("Missing value");
                word_j = true;
                word_j_val = value;
                break;
            case 'K':
                if (!has_value)
                    stepper_error("Missing value");
                word_k = true;
                word_k_val = value;
                break;
            case 'R':
                if (!has_value)
                    stepper_error("Missing value");
                word_r = true;
                word_r_val = value;
                break;
            default:
                stepper_error("Unknown parameter");
            }
        }

        // Spindle commands (M3/M5) are handled as standalone commands in GC mode.
        if (spindle_m >= 0)
        {
            if (primary_g >= 0 || word_x || word_y || word_z || word_f || word_i || word_j || word_k || word_r)
                stepper_error("M-code must be alone");
            if (stepper_system.spindle_pin == 0xFF)
                stepper_error("Spindle pin not configured (use STEPPER SPINDLE)");
            stepper_spindle_set(spindle_m == 3);
            return;
        }

        if (primary_g < 0)
            stepper_error("Syntax");

        // G28 - Home specified axes
        if (primary_is_home)
        {
            bool home_x = word_x && (!word_x_has || (word_x_val != 0.0));
            bool home_y = word_y && (!word_y_has || (word_y_val != 0.0));
            bool home_z = word_z && (!word_z_has || (word_z_val != 0.0));

            if (!home_x && !home_y && !home_z)
                stepper_error("G28 requires at least one axis specified (X, Y, or Z)");

            unsigned char *hargv[16];
            int hargc = 0;
            hargv[hargc++] = (unsigned char *)"G28";
            if (home_x)
            {
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"X";
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"1";
            }
            if (home_y)
            {
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"Y";
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"1";
            }
            if (home_z)
            {
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"Z";
                hargv[hargc++] = (unsigned char *)",";
                hargv[hargc++] = (unsigned char *)"1";
            }

            cmd_stepper_home_axes(hargc, hargv);
            return;
        }

        // G92 - Set workspace offset (no motion)
        if (primary_is_g92)
        {
            if (!stepper_system.position_known)
                stepper_error("Machine position unknown - use STEPPER POSITION or G28 homing first");

            if (word_x)
            {
                if (!word_x_has)
                    stepper_error("G92 missing value");
                if (!axis_is_configured(&stepper_system.x))
                    stepper_error("X axis not configured");
                stepper_system.x_g92_offset =
                    ((float)stepper_system.x.current_pos / stepper_system.x.steps_per_mm) - (float)word_x_val;
            }
            if (word_y)
            {
                if (!word_y_has)
                    stepper_error("G92 missing value");
                if (!axis_is_configured(&stepper_system.y))
                    stepper_error("Y axis not configured");
                stepper_system.y_g92_offset =
                    ((float)stepper_system.y.current_pos / stepper_system.y.steps_per_mm) - (float)word_y_val;
            }
            if (word_z)
            {
                if (!word_z_has)
                    stepper_error("G92 missing value");
                if (!axis_is_configured(&stepper_system.z))
                    stepper_error("Z axis not configured");
                stepper_system.z_g92_offset =
                    ((float)stepper_system.z.current_pos / stepper_system.z.steps_per_mm) - (float)word_z_val;
            }

            planner_sync_to_physical();
            return;
        }

        // Motion commands: G0/G1/G2/G3
        if (primary_g == 0)
        {
            block.type = GCODE_RAPID_MOVE;
            gcode_buffer.current_motion_mode = GCODE_RAPID_MOVE;
        }
        else if (primary_g == 1)
        {
            block.type = GCODE_LINEAR_MOVE;
            gcode_buffer.current_motion_mode = GCODE_LINEAR_MOVE;
        }
        else if (primary_g == 2)
        {
            block.type = GCODE_CW_ARC;
            gcode_buffer.current_motion_mode = GCODE_CW_ARC;
        }
        else if (primary_g == 3)
        {
            block.type = GCODE_CCW_ARC;
            gcode_buffer.current_motion_mode = GCODE_CCW_ARC;
        }
        else
        {
            stepper_error("Unsupported G-code");
        }

        // Safety: prevent queuing any motion until the machine position is established.
        if (!stepper_system.position_known)
            stepper_error("Machine position unknown - use STEPPER POSITION or G28 homing first");

        // Initialize with last known planner position (for missing coordinates)
        block.x = planner_x;
        block.y = planner_y;
        block.z = planner_z;
        block.feedrate = gcode_buffer.current_feedrate;
        block.has_x = false;
        block.has_y = false;
        block.has_z = false;
        block.use_radius = false;
        block.i = 0;
        block.j = 0;
        block.k = 0;
        block.r = 0;

        if (word_x)
        {
            if (!word_x_has)
                stepper_error("Missing value");
            if (!axis_is_configured(&stepper_system.x))
                stepper_error("X axis not configured");
            block.x = gcode_buffer.absolute_mode ? (float)word_x_val : (planner_x + (float)word_x_val);
            block.has_x = true;
        }
        if (word_y)
        {
            if (!word_y_has)
                stepper_error("Missing value");
            if (!axis_is_configured(&stepper_system.y))
                stepper_error("Y axis not configured");
            block.y = gcode_buffer.absolute_mode ? (float)word_y_val : (planner_y + (float)word_y_val);
            block.has_y = true;
        }
        if (word_z)
        {
            if (!word_z_has)
                stepper_error("Missing value");
            if (!axis_is_configured(&stepper_system.z))
                stepper_error("Z axis not configured");
            block.z = gcode_buffer.absolute_mode ? (float)word_z_val : (planner_z + (float)word_z_val);
            block.has_z = true;
        }
        if (word_f)
        {
            block.feedrate = (float)word_f_val / 60.0f;
            gcode_buffer.current_feedrate = block.feedrate;
            gcode_buffer.feedrate_set = true;
        }
        if (word_i)
            block.i = (float)word_i_val;
        if (word_j)
            block.j = (float)word_j_val;
        if (word_k)
            block.k = (float)word_k_val;
        if (word_r)
        {
            block.r = (float)word_r_val;
            block.use_radius = true;
        }

        // Check soft limits for target positions
        if (block.has_x && !position_within_limits(&stepper_system.x, block.x))
            stepper_error("X position exceeds soft limits");
        if (block.has_y && !position_within_limits(&stepper_system.y, block.y))
            stepper_error("Y position exceeds soft limits");
        if (block.has_z && !position_within_limits(&stepper_system.z, block.z))
            stepper_error("Z position exceeds soft limits");

        // Validate feedrate is set for G1, G2, G3 commands
        if (block.type == GCODE_LINEAR_MOVE ||
            block.type == GCODE_CW_ARC ||
            block.type == GCODE_CCW_ARC)
        {
            if (!gcode_buffer.feedrate_set)
                stepper_error("Feedrate not set");
        }

        // Validate arc commands have required parameters
        if (block.type == GCODE_CW_ARC || block.type == GCODE_CCW_ARC)
        {
            if (!block.use_radius && (block.i == 0 && block.j == 0))
                stepper_error("Arc requires I,J or R parameter");

            float start_x = planner_x;
            float start_y = planner_y;
            float start_z = planner_z;
            float target_x = block.has_x ? block.x : start_x;
            float target_y = block.has_y ? block.y : start_y;
            float target_z = block.has_z ? block.z : start_z;
            bool is_clockwise = (block.type == GCODE_CW_ARC);

            float max_feedrate = block.feedrate;
            if (axis_is_configured(&stepper_system.x) && stepper_system.x.max_velocity < max_feedrate)
                max_feedrate = stepper_system.x.max_velocity;
            if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_velocity < max_feedrate)
                max_feedrate = stepper_system.y.max_velocity;

            if (!plan_arc_move(start_x, start_y, start_z,
                               target_x, target_y, target_z,
                               block.i, block.j, block.k,
                               block.r, block.use_radius, is_clockwise,
                               max_feedrate))
            {
                stepper_error("Arc planning failed - buffer full or exceeds soft limits");
            }
            return;
        }

        // Limit feedrate based on max velocity of referenced axes
        if (block.type == GCODE_RAPID_MOVE || block.type == GCODE_LINEAR_MOVE)
        {
            float dx = block.has_x ? (block.x - planner_x) : 0.0f;
            float dy = block.has_y ? (block.y - planner_y) : 0.0f;
            float dz = block.has_z ? (block.z - planner_z) : 0.0f;
            float total_dist = sqrtf(dx * dx + dy * dy + dz * dz);

            if (total_dist > 0.0f)
            {
                float max_feedrate = block.feedrate;
                if (block.has_x && fabsf(dx) > 0.001f)
                {
                    float axis_max = stepper_system.x.max_velocity * total_dist / fabsf(dx);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }
                if (block.has_y && fabsf(dy) > 0.001f)
                {
                    float axis_max = stepper_system.y.max_velocity * total_dist / fabsf(dy);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }
                if (block.has_z && fabsf(dz) > 0.001f)
                {
                    float axis_max = stepper_system.z.max_velocity * total_dist / fabsf(dz);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }
                block.feedrate = max_feedrate;
            }
        }

        // Pre-compute motion profile for ISR (multi-axis Bresenham)
        float start_x = planner_x;
        float start_y = planner_y;
        float start_z = planner_z;
        float target_x = block.has_x ? block.x : start_x;
        float target_y = block.has_y ? block.y : start_y;
        float target_z = block.has_z ? block.z : start_z;
        block.x = target_x;
        block.y = target_y;
        block.z = target_z;

        float dx_mm = 0.0f, dy_mm = 0.0f, dz_mm = 0.0f;
        block.x_steps_planned = 0;
        block.y_steps_planned = 0;
        block.z_steps_planned = 0;
        block.x_dir = true;
        block.y_dir = true;
        block.z_dir = true;

        if (axis_is_configured(&stepper_system.x))
        {
            dx_mm = target_x - start_x;
            block.x_dir = (dx_mm >= 0.0f);
            block.x_steps_planned = (int32_t)(fabsf(dx_mm) * stepper_system.x.steps_per_mm);
        }
        if (axis_is_configured(&stepper_system.y))
        {
            dy_mm = target_y - start_y;
            block.y_dir = (dy_mm >= 0.0f);
            block.y_steps_planned = (int32_t)(fabsf(dy_mm) * stepper_system.y.steps_per_mm);
        }
        if (axis_is_configured(&stepper_system.z))
        {
            dz_mm = target_z - start_z;
            block.z_dir = (dz_mm >= 0.0f);
            block.z_steps_planned = (int32_t)(fabsf(dz_mm) * stepper_system.z.steps_per_mm);
        }

        block.major_steps = block.x_steps_planned;
        if (block.y_steps_planned > block.major_steps)
            block.major_steps = block.y_steps_planned;
        if (block.z_steps_planned > block.major_steps)
            block.major_steps = block.z_steps_planned;

        block.major_axis_mask = compute_major_axis_mask(block.major_steps,
                                                        block.x_steps_planned,
                                                        block.y_steps_planned,
                                                        block.z_steps_planned);

        block.major_axis_mask = compute_major_axis_mask(block.major_steps,
                                                        block.x_steps_planned,
                                                        block.y_steps_planned,
                                                        block.z_steps_planned);

        block.major_axis_mask = compute_major_axis_mask(block.major_steps,
                                                        block.x_steps_planned,
                                                        block.y_steps_planned,
                                                        block.z_steps_planned);

        if (block.major_steps > 0)
        {
            float total_dist_mm = sqrtf(dx_mm * dx_mm + dy_mm * dy_mm + dz_mm * dz_mm);
            if (total_dist_mm < 0.001f)
                total_dist_mm = 0.001f;

            float virtual_steps_per_mm = (float)block.major_steps / total_dist_mm;
            block.distance = total_dist_mm;
            block.virtual_steps_per_mm = virtual_steps_per_mm;
            block.unit_x = dx_mm / total_dist_mm;
            block.unit_y = dy_mm / total_dist_mm;
            block.unit_z = dz_mm / total_dist_mm;

            float target_velocity = block.feedrate;
            if (block.type == GCODE_RAPID_MOVE)
            {
                target_velocity = 1e9f;
                if (block.x_steps_planned > 0 && fabsf(dx_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.x.max_velocity * total_dist_mm / fabsf(dx_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
                if (block.y_steps_planned > 0 && fabsf(dy_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.y.max_velocity * total_dist_mm / fabsf(dy_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
                if (block.z_steps_planned > 0 && fabsf(dz_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.z.max_velocity * total_dist_mm / fabsf(dz_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
            }
            block.max_velocity = target_velocity;

            float min_accel = 1e9f;
            if (block.x_steps_planned > 0)
            {
                float axis_accel = stepper_system.x.max_accel * total_dist_mm / fabsf(dx_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }
            if (block.y_steps_planned > 0)
            {
                float axis_accel = stepper_system.y.max_accel * total_dist_mm / fabsf(dy_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }
            if (block.z_steps_planned > 0)
            {
                float axis_accel = stepper_system.z.max_accel * total_dist_mm / fabsf(dz_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }

            block.min_accel = min_accel;
            block.accel_increment = (int32_t)(min_accel * virtual_steps_per_mm * (float)RATE_SCALE / (float)ISR_FREQ);
            if (block.accel_increment == 0)
                block.accel_increment = 1;

            block.entry_rate = 0;
            block.exit_rate = 0;
            recompute_profile_for_block(&block);

            if (!gcode_buffer.exact_stop_mode)
                try_apply_junction_blend(&block);
        }
        else
        {
            block.is_planned = false;
        }

        if (gcode_buffer_is_full(&gcode_buffer))
            stepper_error("G-code buffer full");

        if (!gcode_buffer_add(&gcode_buffer, &block))
            stepper_error("Failed to add to buffer");

        planner_x = target_x;
        planner_y = target_y;
        planner_z = target_z;

        return;
    }

    // STEPPER GCODE G0|G1|G2|G3 [, X x] [, Y y] [, Z z] [, F feedrate] [, I i] [, J j] [, R r]
    // Add a G-code motion command to the circular buffer
    if ((tp = checkstring(cmdline, (unsigned char *)"GCODE")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        gcode_block_t block = {0};
        block.is_planned = false;

        // Parse arguments: command, then optional coordinate pairs.
        // Max tokens needed for: G1, X, x, Y, y, Z, z, F, f, I, i, J, j, K, k, R, r
        // (1 command + up to 8 parameters + comma tokens) => 17+ entries.
        getcsargs(&tp, 35);
        if (argc < 1)
            stepper_error("Syntax");

        // First argument is the G-code or M-code command.
        int gcode = -1;
        int mcode = -1;

        int cmd_idx = checkparam((char *)argv[0], 18,
                                 "G0", "G00", "G1", "G01", "G2", "G02", "G3", "G03",
                                 "G28", "G90", "G91", "G61", "G64",
                                 "M3", "M03", "M5", "M05",
                                 "G92");

        switch (cmd_idx)
        {
        case 1:
        case 2:
            gcode = 0;
            break;
        case 3:
        case 4:
            gcode = 1;
            break;
        case 5:
        case 6:
            gcode = 2;
            break;
        case 7:
        case 8:
            gcode = 3;
            break;
        case 9:
            gcode = 28;
            break;
        case 10:
            gcode = 90;
            break;
        case 11:
            gcode = 91;
            break;
        case 12:
            gcode = 61;
            break;
        case 13:
            gcode = 64;
            break;
        case 14:
        case 15:
            mcode = 3;
            break;
        case 16:
        case 17:
            mcode = 5;
            break;
        case 18:
            gcode = 92;
            break;
        default:
            break;
        }

        if (gcode < 0 && mcode < 0)
        {
            // Fall back to numeric parse for quoted strings, variables, or uncommon G/M codes.
            char *cmd_str = (char *)getCstring(argv[0]);
            skipspace(cmd_str);
            if (cmd_str[0] == 'G' || cmd_str[0] == 'g')
                gcode = atoi(&cmd_str[1]);
            else if (cmd_str[0] == 'M' || cmd_str[0] == 'm')
                mcode = atoi(&cmd_str[1]);
        }

        // Process the G-code
        if (mcode >= 0)
        {
            switch (mcode)
            {
            case 3:
                if (stepper_system.spindle_pin == 0xFF)
                    stepper_error("Spindle pin not configured (use STEPPER SPINDLE)");
                stepper_spindle_set(true);
                return;
            case 5:
                if (stepper_system.spindle_pin == 0xFF)
                    stepper_error("Spindle pin not configured (use STEPPER SPINDLE)");
                stepper_spindle_set(false);
                return;
            default:
                stepper_error("Unsupported M-code");
            }
        }
        else if (gcode >= 0)
        {
            switch (gcode)
            {
            case 0:
                block.type = GCODE_RAPID_MOVE;
                gcode_buffer.current_motion_mode = GCODE_RAPID_MOVE;
                break;
            case 1:
                block.type = GCODE_LINEAR_MOVE;
                gcode_buffer.current_motion_mode = GCODE_LINEAR_MOVE;
                break;
            case 2:
                block.type = GCODE_CW_ARC;
                gcode_buffer.current_motion_mode = GCODE_CW_ARC;
                break;
            case 3:
                block.type = GCODE_CCW_ARC;
                gcode_buffer.current_motion_mode = GCODE_CCW_ARC;
                break;
            case 28:
                // G28 - Home specified axes (call homing function and return)
                cmd_stepper_home_axes(argc, argv);
                return;
            case 90:
                gcode_buffer.absolute_mode = true;
                return; // Mode change only, no motion
            case 91:
                gcode_buffer.absolute_mode = false;
                return; // Mode change only, no motion
            case 92:
                // G92 - Set workspace coordinate system offset (without moving)
                // G92 X10 means "current position is now X=10 in workspace coordinates"
                // Offset = hardware_position - specified_workspace_position
                if (!stepper_system.position_known)
                    stepper_error("Machine position unknown - use STEPPER POSITION or G28 homing first");

                for (int i = 2; i < argc; i += 4)
                {
                    if (i + 2 >= argc)
                        break;

                    float value = getnumber(argv[i + 2]);

                    int axis_idx = checkparam((char *)argv[i], 3, "X", "Y", "Z");
                    switch (axis_idx)
                    {
                    case 1:
                        if (!axis_is_configured(&stepper_system.x))
                            stepper_error("X axis not configured");
                        // Calculate offset: hardware_pos - workspace_pos
                        stepper_system.x_g92_offset =
                            ((float)stepper_system.x.current_pos / stepper_system.x.steps_per_mm) - value;
                        break;
                    case 2:
                        if (!axis_is_configured(&stepper_system.y))
                            stepper_error("Y axis not configured");
                        stepper_system.y_g92_offset =
                            ((float)stepper_system.y.current_pos / stepper_system.y.steps_per_mm) - value;
                        break;
                    case 3:
                        if (!axis_is_configured(&stepper_system.z))
                            stepper_error("Z axis not configured");
                        stepper_system.z_g92_offset =
                            ((float)stepper_system.z.current_pos / stepper_system.z.steps_per_mm) - value;
                        break;
                    default:
                        stepper_error("G92 unknown parameter");
                    }
                }

                // Planner coordinates are in workspace space; resync after changing offsets.
                planner_sync_to_physical();
                return; // G92 only changes offset, no motion
            case 61:
                gcode_buffer.exact_stop_mode = true;
                return; // Mode change only, no motion
            case 64:
                gcode_buffer.exact_stop_mode = false;
                return; // Mode change only, no motion
            default:
                stepper_error("Unsupported G-code");
            }
        }
        else
        {
            // Use current modal motion mode (no G command specified)
            block.type = gcode_buffer.current_motion_mode;
        }

        // Safety: prevent queuing any motion until the machine position is established.
        // (G28 and STEPPER POSITION establish the physical machine position.)
        if (!stepper_system.position_known &&
            (block.type == GCODE_RAPID_MOVE ||
             block.type == GCODE_LINEAR_MOVE ||
             block.type == GCODE_CW_ARC ||
             block.type == GCODE_CCW_ARC))
        {
            stepper_error("Machine position unknown - use STEPPER POSITION or G28 homing first");
        }

        // Initialize with last known planner position (for missing coordinates)
        block.x = planner_x;
        block.y = planner_y;
        block.z = planner_z;
        block.feedrate = gcode_buffer.current_feedrate;
        block.has_x = false;
        block.has_y = false;
        block.has_z = false;
        block.use_radius = false;
        block.i = 0;
        block.j = 0;
        block.k = 0;
        block.r = 0;

        // Parse remaining parameters (X, Y, Z, F, I, J, K, R)
        // argv indices: 0=G1, 1=empty, 2=param, 3=empty, 4=value, ...
        // Use checkstring first for unquoted literals, fall back to getCstring for variables
        for (int i = 2; i < argc; i += 4)
        {
            if (i + 2 >= argc)
                break; // Need parameter and value

            float value = getnumber(argv[i + 2]);

            int param_idx = checkparam((char *)argv[i], 8, "X", "Y", "Z", "F", "I", "J", "K", "R");
            switch (param_idx)
            {
            case 1:
                if (!axis_is_configured(&stepper_system.x))
                    stepper_error("X axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.x = value;
                else
                    block.x = planner_x + value;
                block.has_x = true;
                break;
            case 2:
                if (!axis_is_configured(&stepper_system.y))
                    stepper_error("Y axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.y = value;
                else
                    block.y = planner_y + value;
                block.has_y = true;
                break;
            case 3:
                if (!axis_is_configured(&stepper_system.z))
                    stepper_error("Z axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.z = value;
                else
                    block.z = planner_z + value;
                block.has_z = true;
                break;
            case 4:
                // G-code feedrate is in mm/min, convert to mm/s for internal use
                block.feedrate = value / 60.0f;
                gcode_buffer.current_feedrate = block.feedrate;
                gcode_buffer.feedrate_set = true;
                break;
            case 5:
                block.i = value;
                break;
            case 6:
                block.j = value;
                break;
            case 7:
                block.k = value;
                break;
            case 8:
                block.r = value;
                block.use_radius = true;
                break;
            default:
                stepper_error("Unknown parameter");
            }
        }

        // Check soft limits for target positions
        if (block.has_x && !position_within_limits(&stepper_system.x, block.x))
            stepper_error("X position exceeds soft limits");
        if (block.has_y && !position_within_limits(&stepper_system.y, block.y))
            stepper_error("Y position exceeds soft limits");
        if (block.has_z && !position_within_limits(&stepper_system.z, block.z))
            stepper_error("Z position exceeds soft limits");

        // Validate feedrate is set for G1, G2, G3 commands
        if (block.type == GCODE_LINEAR_MOVE ||
            block.type == GCODE_CW_ARC ||
            block.type == GCODE_CCW_ARC)
        {
            if (!gcode_buffer.feedrate_set)
                stepper_error("Feedrate not set");
        }

        // Validate arc commands have required parameters
        if (block.type == GCODE_CW_ARC || block.type == GCODE_CCW_ARC)
        {
            if (!block.use_radius && (block.i == 0 && block.j == 0))
                stepper_error("Arc requires I,J or R parameter");

            // Handle arc by linearizing into segments
            float start_x = planner_x;
            float start_y = planner_y;
            float start_z = planner_z;
            float target_x = block.has_x ? block.x : start_x;
            float target_y = block.has_y ? block.y : start_y;
            float target_z = block.has_z ? block.z : start_z;

            bool is_clockwise = (block.type == GCODE_CW_ARC);

            // Limit feedrate for arc based on axis constraints
            float max_feedrate = block.feedrate;
            if (axis_is_configured(&stepper_system.x) && stepper_system.x.max_velocity < max_feedrate)
                max_feedrate = stepper_system.x.max_velocity;
            if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_velocity < max_feedrate)
                max_feedrate = stepper_system.y.max_velocity;

            if (!plan_arc_move(start_x, start_y, start_z,
                               target_x, target_y, target_z,
                               block.i, block.j, block.k,
                               block.r, block.use_radius, is_clockwise,
                               max_feedrate))
            {
                stepper_error("Arc planning failed - buffer full or exceeds soft limits");
            }

            // Arc segments update last position in plan_and_add_linear_segment
            return;
        }

        // Limit feedrate based on max velocity of referenced axes
        // For linear moves, calculate the velocity each axis would experience
        // and limit feedrate so no axis exceeds its max_velocity
        if (block.type == GCODE_RAPID_MOVE || block.type == GCODE_LINEAR_MOVE)
        {
            // Calculate distance for each axis
            float dx = block.has_x ? (block.x - planner_x) : 0.0f;
            float dy = block.has_y ? (block.y - planner_y) : 0.0f;
            float dz = block.has_z ? (block.z - planner_z) : 0.0f;

            // Total distance of move
            float total_dist = sqrtf(dx * dx + dy * dy + dz * dz);

            if (total_dist > 0.0f)
            {
                float max_feedrate = block.feedrate;

                // Check each axis: velocity = (axis_dist / total_dist) * feedrate
                // So max_feedrate = max_velocity * total_dist / axis_dist
                if (block.has_x && fabsf(dx) > 0.001f)
                {
                    float axis_max = stepper_system.x.max_velocity * total_dist / fabsf(dx);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }
                if (block.has_y && fabsf(dy) > 0.001f)
                {
                    float axis_max = stepper_system.y.max_velocity * total_dist / fabsf(dy);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }
                if (block.has_z && fabsf(dz) > 0.001f)
                {
                    float axis_max = stepper_system.z.max_velocity * total_dist / fabsf(dz);
                    if (axis_max < max_feedrate)
                        max_feedrate = axis_max;
                }

                // Apply the limited feedrate
                block.feedrate = max_feedrate;
            }
        }

        // Pre-compute motion profile for ISR (multi-axis Bresenham)
        // IMPORTANT: Use planner position as the start so buffered moves chain correctly.
        float start_x = planner_x;
        float start_y = planner_y;
        float start_z = planner_z;
        float target_x = block.has_x ? block.x : start_x;
        float target_y = block.has_y ? block.y : start_y;
        float target_z = block.has_z ? block.z : start_z;
        block.x = target_x;
        block.y = target_y;
        block.z = target_z;

        // Calculate distance and steps for each axis
        float dx_mm = 0.0f, dy_mm = 0.0f, dz_mm = 0.0f;
        block.x_steps_planned = 0;
        block.y_steps_planned = 0;
        block.z_steps_planned = 0;
        block.x_dir = true;
        block.y_dir = true;
        block.z_dir = true;

        if (axis_is_configured(&stepper_system.x))
        {
            dx_mm = target_x - start_x;
            block.x_dir = (dx_mm >= 0.0f);
            block.x_steps_planned = (int32_t)(fabsf(dx_mm) * stepper_system.x.steps_per_mm);
        }
        if (axis_is_configured(&stepper_system.y))
        {
            dy_mm = target_y - start_y;
            block.y_dir = (dy_mm >= 0.0f);
            block.y_steps_planned = (int32_t)(fabsf(dy_mm) * stepper_system.y.steps_per_mm);
        }
        if (axis_is_configured(&stepper_system.z))
        {
            dz_mm = target_z - start_z;
            block.z_dir = (dz_mm >= 0.0f);
            block.z_steps_planned = (int32_t)(fabsf(dz_mm) * stepper_system.z.steps_per_mm);
        }

        // Find major axis (most steps) - this drives the timing
        block.major_steps = block.x_steps_planned;
        if (block.y_steps_planned > block.major_steps)
            block.major_steps = block.y_steps_planned;
        if (block.z_steps_planned > block.major_steps)
            block.major_steps = block.z_steps_planned;

        if (block.major_steps > 0)
        {
            // Calculate total distance for feedrate scaling
            float total_dist_mm = sqrtf(dx_mm * dx_mm + dy_mm * dy_mm + dz_mm * dz_mm);
            if (total_dist_mm < 0.001f)
                total_dist_mm = 0.001f;

            // For the major axis, calculate steps per mm in the direction of motion
            // We use a normalized "virtual axis" that travels total_dist_mm in major_steps
            float virtual_steps_per_mm = (float)block.major_steps / total_dist_mm;
            block.distance = total_dist_mm;
            block.virtual_steps_per_mm = virtual_steps_per_mm;
            block.unit_x = dx_mm / total_dist_mm;
            block.unit_y = dy_mm / total_dist_mm;
            block.unit_z = dz_mm / total_dist_mm;

            // Determine target velocity (use the effective limited feedrate)
            float target_velocity = block.feedrate;
            if (block.type == GCODE_RAPID_MOVE)
            {
                // For rapids, find the lowest max_velocity that limits us
                target_velocity = 1e9f; // Start high
                if (block.x_steps_planned > 0 && fabsf(dx_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.x.max_velocity * total_dist_mm / fabsf(dx_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
                if (block.y_steps_planned > 0 && fabsf(dy_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.y.max_velocity * total_dist_mm / fabsf(dy_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
                if (block.z_steps_planned > 0 && fabsf(dz_mm) > 0.001f)
                {
                    float axis_limit = stepper_system.z.max_velocity * total_dist_mm / fabsf(dz_mm);
                    if (axis_limit < target_velocity)
                        target_velocity = axis_limit;
                }
            }

            block.max_velocity = target_velocity;

            // Use the lowest acceleration of any active axis (scaled to total distance)
            float min_accel = 1e9f;
            if (block.x_steps_planned > 0)
            {
                float axis_accel = stepper_system.x.max_accel * total_dist_mm / fabsf(dx_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }
            if (block.y_steps_planned > 0)
            {
                float axis_accel = stepper_system.y.max_accel * total_dist_mm / fabsf(dy_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }
            if (block.z_steps_planned > 0)
            {
                float axis_accel = stepper_system.z.max_accel * total_dist_mm / fabsf(dz_mm);
                if (axis_accel < min_accel)
                    min_accel = axis_accel;
            }

            // accel_increment = accel * virtual_steps_per_mm * RATE_SCALE / ISR_FREQ
            block.min_accel = min_accel;
            block.accel_increment = (int32_t)(min_accel * virtual_steps_per_mm * (float)RATE_SCALE / (float)ISR_FREQ);
            if (block.accel_increment == 0)
                block.accel_increment = 1;

            // Default behavior: start/stop at each block (junction blending may override)
            block.entry_rate = 0;
            block.exit_rate = 0;
            recompute_profile_for_block(&block);

            // Minimal junction speed: adjust previous exit + this entry for corners
            if (!gcode_buffer.exact_stop_mode)
                try_apply_junction_blend(&block);
        }
        else
        {
            block.is_planned = false; // No movement needed
        }

        if (gcode_buffer_is_full(&gcode_buffer))
            stepper_error("G-code buffer full");

        // Add to circular buffer
        if (!gcode_buffer_add(&gcode_buffer, &block))
            stepper_error("Failed to add to buffer");

        // Update planner position for next command (planner-only state)
        planner_x = target_x;
        planner_y = target_y;
        planner_z = target_z;

        return;
    }

    // STEPPER BUFFER - Return number of commands in buffer
    if (checkstring(cmdline, (unsigned char *)"BUFFER"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        // Print buffer status
        char buf[80];
        sprintf(buf, "Buffer: %d/%d blocks, executed: %lu\r\n",
                gcode_buffer.count, gcode_buffer.size,
                (unsigned long)gcode_buffer.blocks_executed);
        MMPrintString(buf);

        // ISR health + current move internals
        sprintf(buf, "Ticks: %lu\r\n", (unsigned long)stepper_tick_count);
        MMPrintString(buf);
        sprintf(buf, "Move: phase=%d step_rate=%ld cruise=%ld exit=%ld rem=%ld/%ld\r\n",
                (int)current_move.phase,
                (long)current_move.step_rate,
                (long)current_move.cruise_rate,
                (long)current_move.exit_rate,
                (long)current_move.steps_remaining,
                (long)current_move.total_steps_remaining);
        MMPrintString(buf);

        sprintf(buf, "Buffer idx: head=%d tail=%d\r\n", (int)gcode_buffer.head, (int)gcode_buffer.tail);
        MMPrintString(buf);
        return;
    }

    // STEPPER CLEAR - Clear the G-code buffer
    if (checkstring(cmdline, (unsigned char *)"CLEAR"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        // Don't allow clearing while executing motion
        if (stepper_system.motion_active)
            stepper_error("Cannot clear buffer while motion active");

        gcode_buffer_init(&gcode_buffer);
        return;
    }

    // STEPPER RUN - Arm and start executing buffered commands
    if (checkstring(cmdline, (unsigned char *)"RUN"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        if (!stepper_system.position_known)
            stepper_error("Machine position unknown - use STEPPER POSITION or G28 homing first");

        // If a limit switch is still asserted, refuse to start motion.
        // User should clear the switch condition and re-home (G28).
        if (stepper_system.limits_enabled)
        {
            uint64_t gpio_state = gpio_get_all64();
            if ((~gpio_state & stepper_system.limit_switch_mask) != 0)
                stepper_error("Limit switch active - clear switch and re-home (G28)");
        }

        if (stepper_armed)
        {
            MMPrintString("Already armed\r\n");
            return;
        }

        // Explicit RUN means "arm and execute": re-enable configured drivers (active-low)
        uint32_t save = save_and_disable_interrupts();
        if (stepper_system.x.enable_pin != 0xFF)
            gpio_put(stepper_system.x.enable_pin, 0);
        if (stepper_system.y.enable_pin != 0xFF)
            gpio_put(stepper_system.y.enable_pin, 0);
        if (stepper_system.z.enable_pin != 0xFF)
            gpio_put(stepper_system.z.enable_pin, 0);
        stepper_armed = true;
        restore_interrupts(save);
        MMPrintString("Stepper armed - executing buffered commands\r\n");
        return;
    }

    // STEPPER STATUS - Show current status
    if (checkstring(cmdline, (unsigned char *)"STATUS"))
    {
        if (!stepper_initialized)
        {
            MMPrintString("Stepper not initialized\r\n");
            return;
        }

        char buf[120];
        sprintf(buf, "Armed: %s, Motion: %s\r\n",
                stepper_armed ? "YES" : "NO",
                stepper_system.motion_active ? "ACTIVE" : "IDLE");
        MMPrintString(buf);

        sprintf(buf, "S-curve: %s, Jerk: %.1f mm/s^3\r\n",
                stepper_scurve_enable ? "ON" : "OFF",
                (double)stepper_jerk_limit_mm_s3);
        MMPrintString(buf);

        sprintf(buf, "Buffer: %d/%d blocks, executed: %lu\r\n",
                gcode_buffer.count, gcode_buffer.size,
                (unsigned long)gcode_buffer.blocks_executed);
        MMPrintString(buf);

        // Core state useful for diagnosing "ignored" motion.
        sprintf(buf, "Position known: %s\r\n", stepper_system.position_known ? "YES" : "NO");
        MMPrintString(buf);
        sprintf(buf, "Planner: X=%.3f Y=%.3f Z=%.3f\r\n", (double)planner_x, (double)planner_y, (double)planner_z);
        MMPrintString(buf);
        sprintf(buf, "G92 offsets: X=%.3f Y=%.3f Z=%.3f\r\n",
                (double)stepper_system.x_g92_offset,
                (double)stepper_system.y_g92_offset,
                (double)stepper_system.z_g92_offset);
        MMPrintString(buf);

        sprintf(buf, "HW limits: %s, mask=0x%08lx%08lx\r\n",
                stepper_system.limits_enabled ? "ON" : "OFF",
                (unsigned long)(stepper_system.limit_switch_mask >> 32),
                (unsigned long)(stepper_system.limit_switch_mask & 0xFFFFFFFFULL));
        MMPrintString(buf);
        sprintf(buf, "HW limit pins: Xmin=%d Xmax=%d Ymin=%d Ymax=%d Zmin=%d Zmax=%d\r\n",
                (int)stepper_system.x_min_limit_pin,
                (int)stepper_system.x_max_limit_pin,
                (int)stepper_system.y_min_limit_pin,
                (int)stepper_system.y_max_limit_pin,
                (int)stepper_system.z_min_limit_pin,
                (int)stepper_system.z_max_limit_pin);
        MMPrintString(buf);

        // Show axis positions
        if (axis_is_configured(&stepper_system.x))
        {
            sprintf(buf, "X: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.x.current_pos / (double)stepper_system.x.steps_per_mm,
                    (long)stepper_system.x.current_pos);
            MMPrintString(buf);

            sprintf(buf, "X cfg: step=%u dir=%u en=%u inv=%d spmm=%.3f vmax=%.3f(mm/s) amax=%.3f\r\n",
                    (unsigned)stepper_system.x.step_pin,
                    (unsigned)stepper_system.x.dir_pin,
                    (unsigned)stepper_system.x.enable_pin,
                    stepper_system.x.dir_invert ? 1 : 0,
                    (double)stepper_system.x.steps_per_mm,
                    (double)stepper_system.x.max_velocity,
                    (double)stepper_system.x.max_accel);
            MMPrintString(buf);
        }
        if (axis_is_configured(&stepper_system.y))
        {
            sprintf(buf, "Y: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.y.current_pos / (double)stepper_system.y.steps_per_mm,
                    (long)stepper_system.y.current_pos);
            MMPrintString(buf);

            sprintf(buf, "Y cfg: step=%u dir=%u en=%u inv=%d spmm=%.3f vmax=%.3f(mm/s) amax=%.3f\r\n",
                    (unsigned)stepper_system.y.step_pin,
                    (unsigned)stepper_system.y.dir_pin,
                    (unsigned)stepper_system.y.enable_pin,
                    stepper_system.y.dir_invert ? 1 : 0,
                    (double)stepper_system.y.steps_per_mm,
                    (double)stepper_system.y.max_velocity,
                    (double)stepper_system.y.max_accel);
            MMPrintString(buf);
        }
        if (axis_is_configured(&stepper_system.z))
        {
            sprintf(buf, "Z: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.z.current_pos / (double)stepper_system.z.steps_per_mm,
                    (long)stepper_system.z.current_pos);
            MMPrintString(buf);

            sprintf(buf, "Z cfg: step=%u dir=%u en=%u inv=%d spmm=%.3f vmax=%.3f(mm/s) amax=%.3f\r\n",
                    (unsigned)stepper_system.z.step_pin,
                    (unsigned)stepper_system.z.dir_pin,
                    (unsigned)stepper_system.z.enable_pin,
                    stepper_system.z.dir_invert ? 1 : 0,
                    (double)stepper_system.z.steps_per_mm,
                    (double)stepper_system.z.max_velocity,
                    (double)stepper_system.z.max_accel);
            MMPrintString(buf);
        }

        return;
    }

    stepper_error("Unknown STEPPER subcommand");
}
#endif