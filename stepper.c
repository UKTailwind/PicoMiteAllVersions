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
#ifdef GCODE
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

// Exported: free slots remaining in G-code buffer (0..GCODE_BUFFER_SIZE)
// Read from MMBasic via MM.CODE.
volatile uint8_t stepper_gcode_buffer_space = 0;

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
    uint8_t prev_idx = (gcode_buffer.head + GCODE_BUFFER_SIZE - 1) % GCODE_BUFFER_SIZE;
    gcode_block_t prev = gcode_buffer.blocks[prev_idx];

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
    if (gcode_buffer.count >= 2 && prev_idx == (gcode_buffer.head + GCODE_BUFFER_SIZE - 1) % GCODE_BUFFER_SIZE)
    {
        gcode_buffer.blocks[prev_idx] = prev;
    }
    restore_interrupts(save);
}

void gcode_buffer_init(gcode_buffer_t *buffer)
{
    // Make reset safe even if called while the stepper ISR is running
    uint32_t save = save_and_disable_interrupts();

    buffer->head = 0;
    buffer->tail = 0;
    buffer->count = 0;
    buffer->buffer_full = false;
    buffer->buffer_empty = true;
    buffer->blocks_executed = 0;

    // Empty buffer => full space available
    stepper_gcode_buffer_space = GCODE_BUFFER_SIZE;

    // Initialize parser state
    buffer->current_motion_mode = GCODE_LINEAR_MOVE;
    buffer->current_feedrate = 0.0f;
    buffer->feedrate_set = false;    // No feedrate set yet
    buffer->absolute_mode = true;    // G90 by default
    buffer->exact_stop_mode = false; // G64 by default

    // Reset planner position
    planner_reset_position();

    restore_interrupts(save);
}

// Check if buffer is full
bool gcode_buffer_is_full(gcode_buffer_t *buffer)
{
    return buffer->count >= GCODE_BUFFER_SIZE;
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
    // Disable stepper interrupt during buffer modification
    uint32_t save = save_and_disable_interrupts();

    if (gcode_buffer_is_full(buffer))
    {
        restore_interrupts(save);
        return false; // Buffer full
    }

    // Copy block to buffer
    buffer->blocks[buffer->head] = *block;

    // Update head pointer
    buffer->head = (buffer->head + 1) % GCODE_BUFFER_SIZE;
    buffer->count++;
    buffer->buffer_empty = false;
    buffer->buffer_full = (buffer->count >= GCODE_BUFFER_SIZE);

    // Keep exported free-space indicator in sync (still IRQ-off)
    stepper_gcode_buffer_space = (buffer->count <= GCODE_BUFFER_SIZE) ? (GCODE_BUFFER_SIZE - buffer->count) : 0;

    restore_interrupts(save);
    return true;
}

// Get the next block to execute (doesn't remove it)
gcode_block_t *gcode_buffer_peek(gcode_buffer_t *buffer)
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

    // Update tail pointer
    buffer->tail = (buffer->tail + 1) % GCODE_BUFFER_SIZE;
    buffer->count--;
    buffer->buffer_full = false;
    buffer->buffer_empty = (buffer->count == 0);
    buffer->blocks_executed++;

    // Keep exported free-space indicator in sync (ISR context)
    stepper_gcode_buffer_space = (buffer->count <= GCODE_BUFFER_SIZE) ? (GCODE_BUFFER_SIZE - buffer->count) : 0;

    return true;
}

// Get available space in buffer (ISR-safe)
uint8_t gcode_buffer_available(gcode_buffer_t *buffer)
{
    uint32_t save = save_and_disable_interrupts();
    uint8_t available = GCODE_BUFFER_SIZE - buffer->count;
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
    block.is_executing = false;

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
    block.is_executing = false;

    return block;
}

// Stepper timer state
#define STEPPER_PWM_SLICE 11
volatile bool stepper_initialized = false; // Non-static for access from External.c
static volatile uint32_t stepper_tick_count = 0;

// Testing: ISR sets index of block to display, main context copies it
// This avoids copying struct with floats in ISR context
static volatile bool stepper_block_ready = false;
static volatile uint8_t stepper_block_index = 0; // Index into gcode_buffer.blocks
static volatile bool stepper_test_mode = false;
// Shadow index for test mode - allows walking through buffer without consuming blocks
static volatile uint8_t test_peek_index = 0;
static volatile uint32_t test_peek_count = 0; // How many blocks we've peeked in test mode

// Current move state (used by ISR)
static volatile stepper_move_t current_move = {0};

// Arc linearization settings
#define DEFAULT_ARC_TOLERANCE_MM 0.5f // Default maximum arc segment length in mm
#define ARC_ANGULAR_DEVIATION 0.1f    // Maximum angular deviation per segment (radians)
#define MIN_ARC_SEGMENTS 4            // Minimum segments for any arc

// Runtime-configurable arc tolerance (mm)
static float stepper_arc_tolerance_mm = DEFAULT_ARC_TOLERANCE_MM;

// Forward declaration for adding a pre-planned arc segment
static bool add_arc_segment(float target_x, float target_y, float target_z,
                            uint32_t entry_rate, uint32_t exit_rate, // ← Back to uint32_t
                            uint32_t accel_increment, float feedrate);

// Arc linearization function
// Breaks G02/G03 arc into small linear segments and adds them to the buffer
// The entire arc shares one acceleration profile (accel once, cruise, decel once)
// Returns true if successful, false if buffer full
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
            return false; // Radius too small for the distance

        if (d < 0.001f)
            return false; // Start and end points too close (would cause division by zero)

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
    float total_arc_length = sqrtf(arc_xy_length * arc_xy_length + z_travel * z_travel);

    // Calculate number of segments
    float tol = stepper_arc_tolerance_mm;
    if (tol < 0.001f)
        tol = 0.001f;
    int segments = (int)(arc_xy_length / tol);
    if (segments < MIN_ARC_SEGMENTS)
        segments = MIN_ARC_SEGMENTS;
    int angular_segments = (int)(fabsf(angular_travel) / ARC_ANGULAR_DEVIATION);
    if (angular_segments > segments)
        segments = angular_segments;

    // Check buffer space
    if (gcode_buffer_available(&gcode_buffer) < segments)
        return false;

    // Calculate motion profile for the ENTIRE arc
    // Use the minimum acceleration of X and Y axes
    float min_accel = stepper_system.x.max_accel;
    if (axis_is_configured(&stepper_system.y) && stepper_system.y.max_accel < min_accel)
        min_accel = stepper_system.y.max_accel;
    if (axis_is_configured(&stepper_system.z) && fabsf(z_travel) > 0.001f && stepper_system.z.max_accel < min_accel)
        min_accel = stepper_system.z.max_accel;

    // Calculate acceleration distance: s = v² / (2*a)
    float accel_distance = (feedrate * feedrate) / (2.0f * min_accel);
    float decel_distance = accel_distance;

    // Check for triangle profile (can't reach full speed)
    float cruise_velocity = feedrate;
    if (accel_distance + decel_distance > total_arc_length)
    {
        // Triangle profile
        accel_distance = total_arc_length / 2.0f;
        decel_distance = total_arc_length - accel_distance;
        cruise_velocity = sqrtf(2.0f * min_accel * accel_distance);
    }

    float cruise_distance = total_arc_length - accel_distance - decel_distance;

    // Calculate segment length
    float segment_length = total_arc_length / (float)segments;
    float angle_per_segment = angular_travel / (float)segments;
    float z_per_segment = z_travel / (float)segments;

    // For step rate calculation, use average steps_per_mm for X and Y
    float avg_steps_per_mm = stepper_system.x.steps_per_mm;
    if (axis_is_configured(&stepper_system.y))
        avg_steps_per_mm = (stepper_system.x.steps_per_mm + stepper_system.y.steps_per_mm) / 2.0f;

    // Calculate accel_increment for the ISR: accel_increment = accel * steps/mm * RATE_SCALE / ISR_FREQ
    int32_t accel_increment = (int32_t)(min_accel * avg_steps_per_mm * RATE_SCALE / (float)ISR_FREQ);
    if (accel_increment == 0)
        accel_increment = 1;

    // Generate segments with velocity based on position in arc
    float current_z = start_z;
    float cumulative_distance = 0.0f;

    for (int i = 1; i <= segments; i++)
    {
        float current_angle = start_angle + angle_per_segment * (float)i;
        float seg_x, seg_y, seg_z;

        // Calculate segment end position
        if (i == segments)
        {
            seg_x = target_x;
            seg_y = target_y;
            seg_z = target_z;
        }
        else
        {
            seg_x = center_x + r_start * cosf(current_angle);
            seg_y = center_y + r_start * sinf(current_angle);
            seg_z = current_z + z_per_segment;
            current_z = seg_z;
        }

        // Distance at START of this segment
        float seg_start_dist = cumulative_distance;
        cumulative_distance += segment_length;
        // Distance at END of this segment
        float seg_end_dist = cumulative_distance;

        // Calculate entry and exit velocities based on position in motion profile
        float entry_velocity, exit_velocity;

        // Entry velocity
        if (seg_start_dist < accel_distance)
        {
            // In acceleration zone: v = sqrt(2 * a * s)
            entry_velocity = sqrtf(2.0f * min_accel * seg_start_dist);
        }
        else if (seg_start_dist < accel_distance + cruise_distance)
        {
            // In cruise zone
            entry_velocity = cruise_velocity;
        }
        else
        {
            // In deceleration zone: v = sqrt(2 * a * remaining_distance)
            float remaining = total_arc_length - seg_start_dist;
            entry_velocity = sqrtf(2.0f * min_accel * remaining);
        }

        // Exit velocity
        if (seg_end_dist < accel_distance)
        {
            exit_velocity = sqrtf(2.0f * min_accel * seg_end_dist);
        }
        else if (seg_end_dist < accel_distance + cruise_distance)
        {
            exit_velocity = cruise_velocity;
        }
        else
        {
            float remaining = total_arc_length - seg_end_dist;
            if (remaining < 0.0f)
                remaining = 0.0f;
            exit_velocity = sqrtf(2.0f * min_accel * remaining);
        }

        // Ensure minimum velocity for very short segments
        if (entry_velocity < 0.1f)
            entry_velocity = 0.1f;
        if (exit_velocity < 0.1f && i < segments)
            exit_velocity = 0.1f;
        if (i == segments)
            exit_velocity = 0.0f; // Stop at end

        // Convert to step rates: step_rate = velocity * steps/mm * RATE_SCALE
        int32_t entry_rate = (int32_t)(entry_velocity * avg_steps_per_mm * (float)RATE_SCALE);
        int32_t exit_rate = (int32_t)(exit_velocity * avg_steps_per_mm * (float)RATE_SCALE);

        // Add the segment
        if (!add_arc_segment(seg_x, seg_y, seg_z, entry_rate, exit_rate, accel_increment, feedrate))
            return false;
    }

    return true;
}

// Add a single arc segment with specified entry/exit velocities
// The segment accelerates/decelerates smoothly between entry and exit rates.
// Each segment is short (0.5mm), so velocity changes are small.
// The entry_rate for each segment is pre-calculated based on position in the overall
// arc motion profile, ensuring smooth velocity transitions between segments.
// Returns false if segment exceeds soft limits or buffer is full.
static bool add_arc_segment(float target_x, float target_y, float target_z,
                            uint32_t entry_rate, uint32_t exit_rate,  // ← Back to uint32_t
                            uint32_t accel_increment, float feedrate) // ← And here
{
    // Check soft limits for arc segment endpoint
    if (!position_within_limits(&stepper_system.x, target_x))
        return false; // Arc segment exceeds X soft limit
    if (!position_within_limits(&stepper_system.y, target_y))
        return false; // Arc segment exceeds Y soft limit
    if (!position_within_limits(&stepper_system.z, target_z))
        return false; // Arc segment exceeds Z soft limit

    gcode_block_t block = {0};

    block.type = GCODE_LINEAR_MOVE; // Arc segments are linear
    block.x = target_x;
    block.y = target_y;
    block.z = target_z;
    block.has_x = axis_is_configured(&stepper_system.x);
    block.has_y = axis_is_configured(&stepper_system.y);
    block.has_z = axis_is_configured(&stepper_system.z);
    block.feedrate = feedrate;
    block.max_velocity = feedrate;

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
        // Policy: keep arc segments on the existing (non S-curve) approach initially.
        block.use_scurve = false;
        block.jerk_increment = 0;
        block.scurve_ju_accel_steps = 0;
        block.scurve_ca_steps = 0;
        block.scurve_jd_accel_steps = 0;
        block.scurve_cruise_steps = 0;
        block.scurve_ju_decel_steps = 0;
        block.scurve_cd_steps = 0;
        block.scurve_jd_decel_steps = 0;

        // Set entry rate and cruise rate for this segment
        block.entry_rate = entry_rate;
        block.exit_rate = exit_rate;
        block.cruise_rate = (entry_rate > exit_rate) ? entry_rate : exit_rate;

        // Geometry for junction estimation (optional for arc segments)
        block.distance = sqrtf(dx_mm * dx_mm + dy_mm * dy_mm + dz_mm * dz_mm);
        if (block.distance < 0.001f)
            block.distance = 0.001f;
        block.virtual_steps_per_mm = (float)block.major_steps / block.distance;
        block.unit_x = dx_mm / block.distance;
        block.unit_y = dy_mm / block.distance;
        block.unit_z = dz_mm / block.distance;
        // Derive effective accel limit from accel_increment parameter
        if (accel_increment > 0 && block.virtual_steps_per_mm > 0.0f)
            block.min_accel = ((float)accel_increment * (float)ISR_FREQ) / (block.virtual_steps_per_mm * (float)RATE_SCALE);
        else
            block.min_accel = stepper_system.x.max_accel; // Fallback to X axis accel

        // Determine if accelerating or decelerating
        if (exit_rate > entry_rate)
        {
            // Accelerating segment
            block.accel_steps = block.major_steps;
            block.cruise_steps = 0;
            block.decel_steps = 0;
            block.accel_increment = accel_increment;
        }
        else if (exit_rate < entry_rate)
        {
            // Decelerating segment
            block.accel_steps = 0;
            block.cruise_steps = 0;
            block.decel_steps = block.major_steps;
            block.accel_increment = accel_increment;
        }
        else
        {
            // Constant velocity segment (cruise)
            block.accel_steps = 0;
            block.cruise_steps = block.major_steps;
            block.decel_steps = 0;
            block.accel_increment = 0;
        }

        block.is_planned = true;
    }
    else
    {
        block.is_planned = false;
    }

    // Update planner position (planner-only state, no IRQ protection needed)
    planner_x = target_x;
    planner_y = target_y;
    planner_z = target_z;

    return gcode_buffer_add(&gcode_buffer, &block);
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
    // Clear the interrupt flag for PWM slice 11
    pwm_clear_irq(STEPPER_PWM_SLICE);

    // Increment tick counter
    stepper_tick_count++;

    // Test mode: read-only observation using shadow index
    // We walk through the buffer without actually consuming blocks
    if (stepper_test_mode && !stepper_block_ready)
    {
        // Check if shadow index points to a valid block
        if (test_peek_count < gcode_buffer.count)
        {
            // Record the shadow index for main context to display
            stepper_block_index = test_peek_index;
            stepper_block_ready = true;
            // Note: POLL command will advance test_peek_index after displaying
        }
    }

    // If in test mode, don't execute motion - just observe
    if (stepper_test_mode)
        return;

    // Check hardware limit switches (active-low, triggers on 0)
    if (stepper_system.limits_enabled && !stepper_system.homing_active)
    {
        uint64_t gpio_state = gpio_get_all64();
        // Limit switches are active-low: if any limit pin reads 0, it's triggered
        if ((~gpio_state & stepper_system.limit_switch_mask) != 0)
        {
            // Limit switch triggered during normal motion - emergency stop
            current_move.phase = MOVE_PHASE_IDLE;
            stepper_system.motion_active = false;
            stepper_test_mode = true; // Enter test mode to prevent motion resume

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
            gcode_buffer.head = 0;
            gcode_buffer.tail = 0;
            gcode_buffer.count = 0;
            gcode_buffer.buffer_full = false;
            gcode_buffer.buffer_empty = true;
            stepper_gcode_buffer_space = GCODE_BUFFER_SIZE;

            // A hard-limit trip invalidates our assumed machine position.
            // Require the user to re-establish position with G28 or STEPPER POSITION.
            stepper_system.position_known = false;
            stepper_system.x_g92_offset = 0.0f;
            stepper_system.y_g92_offset = 0.0f;
            stepper_system.z_g92_offset = 0.0f;

            // Reset test-mode peek state
            stepper_block_ready = false;
            test_peek_index = 0;
            test_peek_count = 0;

            // Note: main code will now refuse RUN until position is re-established.
            return;
        }
    }
    // Note: During homing (homing_active=true), limits are checked by homing logic, not here

    // If idle, check for new work
    if (current_move.phase == MOVE_PHASE_IDLE)
    {
        // If test mode, don't auto-dequeue for execution
        if (stepper_test_mode)
            return;

        // Try to dequeue a new block
        gcode_block_t *block = gcode_buffer_peek(&gcode_buffer);
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
        current_move.step_accumulator = 0;
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

        // Set direction pins for all active axes
        if (current_move.x_active)
        {
            bool dir_level = current_move.x_dir;
            if (stepper_system.x.dir_invert)
                dir_level = !dir_level;
            gpio_put(stepper_system.x.dir_pin, dir_level);
        }
        if (current_move.y_active)
        {
            bool dir_level = current_move.y_dir;
            if (stepper_system.y.dir_invert)
                dir_level = !dir_level;
            gpio_put(stepper_system.y.dir_pin, dir_level);
        }
        if (current_move.z_active)
        {
            bool dir_level = current_move.z_dir;
            if (stepper_system.z.dir_invert)
                dir_level = !dir_level;
            gpio_put(stepper_system.z.dir_pin, dir_level);
        }

        // Remove block from buffer
        gcode_buffer_pop(&gcode_buffer);

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

    // Accumulator-based step timing (prevent overflow)
    {
        uint64_t accum = (uint64_t)current_move.step_accumulator + (uint64_t)current_move.step_rate;
        if (accum >= STEP_ACCUMULATOR_MAX)
        {
            accum -= STEP_ACCUMULATOR_MAX;
            current_move.step_accumulator = (uint32_t)accum;
            // ...existing step pulse and position update code...
        }
        else
        {
            current_move.step_accumulator = (uint32_t)accum;
            return;
        }
    }

    // Bresenham algorithm: determine which axes should step
    bool step_x = false, step_y = false, step_z = false;

    if (current_move.x_active)
    {
        current_move.x_error -= current_move.x_steps;
        if (current_move.x_error < 0)
        {
            current_move.x_error += current_move.major_steps;
            step_x = true;
        }
    }
    if (current_move.y_active)
    {
        current_move.y_error -= current_move.y_steps;
        if (current_move.y_error < 0)
        {
            current_move.y_error += current_move.major_steps;
            step_y = true;
        }
    }
    if (current_move.z_active)
    {
        current_move.z_error -= current_move.z_steps;
        if (current_move.z_error < 0)
        {
            current_move.z_error += current_move.major_steps;
            step_z = true;
        }
    }

    // Generate step pulses - set all HIGH first
    if (step_x)
        gpio_put(stepper_system.x.step_pin, 1);
    if (step_y)
        gpio_put(stepper_system.y.step_pin, 1);
    if (step_z)
        gpio_put(stepper_system.z.step_pin, 1);

    // Brief delay (minimum pulse width)
    __asm volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop");

    // Set all LOW
    if (step_x)
        gpio_put(stepper_system.x.step_pin, 0);
    if (step_y)
        gpio_put(stepper_system.y.step_pin, 0);
    if (step_z)
        gpio_put(stepper_system.z.step_pin, 0);

    // Update positions
    if (step_x)
    {
        if (current_move.x_dir)
            stepper_system.x.current_pos++;
        else
            stepper_system.x.current_pos--;
    }
    if (step_y)
    {
        if (current_move.y_dir)
            stepper_system.y.current_pos++;
        else
            stepper_system.y.current_pos--;
    }
    if (step_z)
    {
        if (current_move.z_dir)
            stepper_system.z.current_pos++;
        else
            stepper_system.z.current_pos--;
    }

    // ...existing code...

    current_move.total_steps_remaining--;
    current_move.steps_remaining--;

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
    // Require TEST mode so we can't race the ISR motion executor.
    if (!stepper_test_mode)
        stepper_error("Cannot home while running - use STEPPER TEST then G28");

    // Parse which axes to home (X, Y, Z parameters with non-zero values)
    bool home_x = false, home_y = false, home_z = false;

    for (int i = 2; i < argc; i += 4)
    {
        if (i + 2 >= argc)
            break;

        char param_char = 0;
        if (checkstring(argv[i], (unsigned char *)"X") != NULL)
            param_char = 'X';
        else if (checkstring(argv[i], (unsigned char *)"Y") != NULL)
            param_char = 'Y';
        else if (checkstring(argv[i], (unsigned char *)"Z") != NULL)
            param_char = 'Z';
        else
        {
            char *param = (char *)getCstring(argv[i]);
            skipspace(param);
            param_char = toupper(param[0]);
        }

        float value = getnumber(argv[i + 2]);
        if (value != 0.0f)
        {
            if (param_char == 'X' && axis_is_configured(&stepper_system.x))
                home_x = true;
            else if (param_char == 'Y' && axis_is_configured(&stepper_system.y))
                home_y = true;
            else if (param_char == 'Z' && axis_is_configured(&stepper_system.z))
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

    // Block stepper command if I2S audio is configured (uses PWM slice 11)
    if (Option.audio_i2s_bclk)
        stepper_error("Stepper incompatible with I2S audio");

    // STEPPER INIT [,arc_tolerance] - Initialize stepper system, G-code buffer, and 100KHz timer interrupt
    if ((tp = checkstring(cmdline, (unsigned char *)"INIT")) != NULL)
    {
        if (stepper_initialized)
            stepper_error("Stepper already initialized");

        // Optional arc tolerance parameter: defaults to 0.5mm
        // Only read if one argument was provided.
        getcsargs(&tp, 1);
        if (argc == 1)
        {
            float tol = getnumber(argv[0]);
            if (tol <= 0.0f)
                stepper_error("Arc tolerance must be > 0");
            stepper_arc_tolerance_mm = tol;
        }
        else
        {
            stepper_arc_tolerance_mm = DEFAULT_ARC_TOLERANCE_MM;
        }

        // Check for conflict with EXT_FAST_TIMER (uses same IRQ)
        if (ExtCurrentConfig[FAST_TIMER_PIN] == EXT_FAST_TIMER)
            stepper_error("Stepper incompatible with FAST TIMER");

        // Initialize all axes to defaults
        stepper_axis_init(&stepper_system.x);
        stepper_axis_init(&stepper_system.y);
        stepper_axis_init(&stepper_system.z);
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

        // Initialize the G-code circular buffer
        gcode_buffer_init(&gcode_buffer);

        // Set up PWM slice 11 for 100KHz interrupt (10 microsecond period)
        // System clock is typically 150MHz on RP2350
        // For 100KHz: 150MHz / 100KHz = 1500 counts
        // Using divider of 1, wrap value of 1499 gives 100KHz
        uint32_t sys_clk = clock_get_hz(clk_sys);
        uint32_t target_freq = 100000; // 100KHz
        uint16_t wrap_value = (sys_clk / target_freq) - 1;

        // Configure PWM slice 11 for 100KHz
        pwm_config cfg = pwm_get_default_config();
        pwm_config_set_clkdiv(&cfg, 1.0f); // No clock division
        pwm_config_set_wrap(&cfg, wrap_value);
        pwm_init(STEPPER_PWM_SLICE, &cfg, false); // Don't start yet

        // Enable test mode
        stepper_test_mode = true;
        stepper_block_ready = false;

        // Clear any pending interrupt
        pwm_clear_irq(STEPPER_PWM_SLICE);

        // Set up interrupt handler on PWM_IRQ_WRAP_1
        irq_set_exclusive_handler(PWM_IRQ_WRAP_1, stepper_timer_isr);
        irq_set_enabled(PWM_IRQ_WRAP_1, true);
        irq_set_priority(PWM_IRQ_WRAP_1, 0); // Highest priority for timing critical

        // Enable interrupt for PWM slice 11 on IRQ1
        pwm_set_irq1_enabled(STEPPER_PWM_SLICE, true);

        // Start the PWM timer
        pwm_set_enabled(STEPPER_PWM_SLICE, true);

        stepper_initialized = true;
        stepper_tick_count = 0;

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

        // Ensure spindle is off on shutdown
        stepper_spindle_off();

        // Stop the PWM timer
        pwm_set_enabled(STEPPER_PWM_SLICE, false);

        // Disable interrupt
        pwm_set_irq1_enabled(STEPPER_PWM_SLICE, false);
        irq_set_enabled(PWM_IRQ_WRAP_1, false);

        // Reset state
        stepper_initialized = false;
        stepper_system.motion_active = false;

        // Stepper offline => report no usable buffer space
        stepper_gcode_buffer_space = 0;

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

        stepper_system.motion_active = false;

        // Clear buffer and reset modal state
        gcode_buffer.head = 0;
        gcode_buffer.tail = 0;
        gcode_buffer.count = 0;
        gcode_buffer.buffer_full = false;
        gcode_buffer.buffer_empty = true;

        // Empty buffer => full space available
        stepper_gcode_buffer_space = GCODE_BUFFER_SIZE;
        gcode_buffer.current_motion_mode = GCODE_LINEAR_MOVE;
        gcode_buffer.current_feedrate = 0.0f;
        gcode_buffer.feedrate_set = false;
        gcode_buffer.absolute_mode = true; // Default to G90

        // Enter test mode to prevent any auto-execution until explicit STEPPER RUN
        stepper_test_mode = true;
        stepper_block_ready = false;
        test_peek_index = 0;
        test_peek_count = 0;

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
        MMPrintString("Use STEPPER ENABLE and STEPPER RUN to resume\r\n");
        return;
    }

    // STEPPER AXIS X|Y|Z, step_pin, dir_pin [, enable_pin] [, dir_invert] [, steps_per_mm] [, max_velocity] [, max_accel]
    if ((tp = checkstring(cmdline, (unsigned char *)"AXIS")) != NULL)
    {
        getcsargs(&tp, 15);
        if (argc < 5)
            stepper_error("Syntax");

        // Get axis letter (X, Y, or Z)
        char *axis_str = (char *)getCstring(argv[0]);
        if (strlen(axis_str) != 1)
            stepper_error("Invalid axis");

        stepper_axis_t *axis = get_axis_ptr(axis_str[0]);
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

        gpio_init(axis->dir_pin);
        gpio_set_dir(axis->dir_pin, GPIO_OUT);
        gpio_put(axis->dir_pin, 0);
        ExtCfg(dir_pin, EXT_DIG_OUT, 0);

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

        // Optional: max velocity (mm/s)
        if (argc >= 13 && *argv[12])
        {
            axis->max_velocity = getnumber(argv[12]);
            if (axis->max_velocity <= 0)
                stepper_error("Max velocity must be > 0");
        }

        // Optional: max acceleration (mm/s²)
        if (argc >= 15 && *argv[14])
        {
            axis->max_accel = getnumber(argv[14]);
            if (axis->max_accel <= 0)
                stepper_error("Max acceleration must be > 0");
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

    // STEPPER RESET - Reset all axes to default state
    if (checkstring(cmdline, (unsigned char *)"RESET"))
    {
        // Ensure spindle is off when resetting configuration
        stepper_spindle_off();
        stepper_axis_init(&stepper_system.x);
        stepper_axis_init(&stepper_system.y);
        stepper_axis_init(&stepper_system.z);
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

        // Get axis letter
        char *axis_str = (char *)getCstring(argv[0]);
        if (strlen(axis_str) != 1)
            stepper_error("Invalid axis");

        stepper_axis_t *axis = get_axis_ptr(axis_str[0]);
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
                toupper(axis_str[0]), (double)min_mm, (double)max_mm,
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

        char *axis_str = (char *)getCstring(argv[0]);
        if (strcasecmp(axis_str, "ALL") == 0)
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
            stepper_axis_t *axis = get_axis_ptr(axis_str[0]);
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

        char *axis_str = (char *)getCstring(argv[0]);
        stepper_axis_t *axis = get_axis_ptr(axis_str[0]);
        if (axis == NULL)
            stepper_error("Axis must be X, Y, or Z");

        axis->dir_invert = getint(argv[2], 0, 1);
        return;
    }

    // STEPPER POSITION X|Y|Z, position - Set current position for an axis
    if ((tp = checkstring(cmdline, (unsigned char *)"POSITION")) != NULL)
    {
        getcsargs(&tp, 3);
        if (argc != 3)
            stepper_error("Syntax");

        char *axis_str = (char *)getCstring(argv[0]);
        stepper_axis_t *axis = get_axis_ptr(axis_str[0]);
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

    // STEPPER GCODE G0|G1|G2|G3 [, X x] [, Y y] [, Z z] [, F feedrate] [, I i] [, J j] [, R r]
    // Add a G-code motion command to the circular buffer
    if ((tp = checkstring(cmdline, (unsigned char *)"GCODE")) != NULL)
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        gcode_block_t block = {0};
        block.is_planned = false;
        block.is_executing = false;

        // Parse arguments: command, then optional coordinate pairs
        getcsargs(&tp, 15);
        if (argc < 1)
            stepper_error("Syntax");

        // First argument is the G-code command (G0, G00, G1, G01, G2, G02, G3, G03, G28)
        // Try checkstring for common unquoted literals first
        int gcode = -1;
        int mcode = -1;
        if (checkstring(argv[0], (unsigned char *)"G0") != NULL ||
            checkstring(argv[0], (unsigned char *)"G00") != NULL)
            gcode = 0;
        else if (checkstring(argv[0], (unsigned char *)"G1") != NULL ||
                 checkstring(argv[0], (unsigned char *)"G01") != NULL)
            gcode = 1;
        else if (checkstring(argv[0], (unsigned char *)"G2") != NULL ||
                 checkstring(argv[0], (unsigned char *)"G02") != NULL)
            gcode = 2;
        else if (checkstring(argv[0], (unsigned char *)"G3") != NULL ||
                 checkstring(argv[0], (unsigned char *)"G03") != NULL)
            gcode = 3;
        else if (checkstring(argv[0], (unsigned char *)"G28") != NULL)
            gcode = 28;
        else if (checkstring(argv[0], (unsigned char *)"G90") != NULL)
            gcode = 90;
        else if (checkstring(argv[0], (unsigned char *)"G91") != NULL)
            gcode = 91;
        else if (checkstring(argv[0], (unsigned char *)"G61") != NULL)
            gcode = 61;
        else if (checkstring(argv[0], (unsigned char *)"G64") != NULL)
            gcode = 64;
        else if (checkstring(argv[0], (unsigned char *)"M3") != NULL ||
                 checkstring(argv[0], (unsigned char *)"M03") != NULL)
            mcode = 3;
        else if (checkstring(argv[0], (unsigned char *)"M5") != NULL ||
                 checkstring(argv[0], (unsigned char *)"M05") != NULL)
            mcode = 5;
        else
        {
            // Fall back to getCstring for quoted strings or variables
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
                    char param_char = 0;

                    // Try checkstring for unquoted literals first
                    if (checkstring(argv[i], (unsigned char *)"X") != NULL)
                        param_char = 'X';
                    else if (checkstring(argv[i], (unsigned char *)"Y") != NULL)
                        param_char = 'Y';
                    else if (checkstring(argv[i], (unsigned char *)"Z") != NULL)
                        param_char = 'Z';
                    else
                    {
                        // Fall back to getCstring for quoted strings or variables
                        char *param = (char *)getCstring(argv[i]);
                        skipspace(param);
                        param_char = toupper(param[0]);
                    }

                    switch (param_char)
                    {
                    case 'X':
                        if (!axis_is_configured(&stepper_system.x))
                            stepper_error("X axis not configured");
                        // Calculate offset: hardware_pos - workspace_pos
                        stepper_system.x_g92_offset =
                            ((float)stepper_system.x.current_pos / stepper_system.x.steps_per_mm) - value;
                        break;
                    case 'Y':
                        if (!axis_is_configured(&stepper_system.y))
                            stepper_error("Y axis not configured");
                        stepper_system.y_g92_offset =
                            ((float)stepper_system.y.current_pos / stepper_system.y.steps_per_mm) - value;
                        break;
                    case 'Z':
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
            char param_char = 0;

            // Try checkstring for unquoted literals first
            if (checkstring(argv[i], (unsigned char *)"X") != NULL)
                param_char = 'X';
            else if (checkstring(argv[i], (unsigned char *)"Y") != NULL)
                param_char = 'Y';
            else if (checkstring(argv[i], (unsigned char *)"Z") != NULL)
                param_char = 'Z';
            else if (checkstring(argv[i], (unsigned char *)"F") != NULL)
                param_char = 'F';
            else if (checkstring(argv[i], (unsigned char *)"I") != NULL)
                param_char = 'I';
            else if (checkstring(argv[i], (unsigned char *)"J") != NULL)
                param_char = 'J';
            else if (checkstring(argv[i], (unsigned char *)"K") != NULL)
                param_char = 'K';
            else if (checkstring(argv[i], (unsigned char *)"R") != NULL)
                param_char = 'R';
            else
            {
                // Fall back to getCstring for quoted strings or variables
                char *param = (char *)getCstring(argv[i]);
                skipspace(param);
                param_char = toupper(param[0]);
            }

            switch (param_char)
            {
            case 'X':
                if (!axis_is_configured(&stepper_system.x))
                    stepper_error("X axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.x = value;
                else
                    block.x = planner_x + value;
                block.has_x = true;
                break;
            case 'Y':
                if (!axis_is_configured(&stepper_system.y))
                    stepper_error("Y axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.y = value;
                else
                    block.y = planner_y + value;
                block.has_y = true;
                break;
            case 'Z':
                if (!axis_is_configured(&stepper_system.z))
                    stepper_error("Z axis not configured");
                if (gcode_buffer.absolute_mode)
                    block.z = value;
                else
                    block.z = planner_z + value;
                block.has_z = true;
                break;
            case 'F':
                // G-code feedrate is in mm/min, convert to mm/s for internal use
                block.feedrate = value / 60.0f;
                gcode_buffer.current_feedrate = block.feedrate;
                gcode_buffer.feedrate_set = true;
                break;
            case 'I':
                block.i = value;
                break;
            case 'J':
                block.j = value;
                break;
            case 'K':
                block.k = value;
                break;
            case 'R':
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
                gcode_buffer.count, GCODE_BUFFER_SIZE,
                (unsigned long)gcode_buffer.blocks_executed);
        MMPrintString(buf);
        return;
    }

    // STEPPER POLL - Check if ISR has dequeued a block and print it
    if (checkstring(cmdline, (unsigned char *)"POLL"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        if (stepper_block_ready)
        {
            // Copy the block from buffer in main context (safe to access floats here)
            gcode_block_t block = gcode_buffer.blocks[stepper_block_index];

            char buf[120];
            const char *gcode_names[] = {"G0", "G1", "G2", "G3"};
            sprintf(buf, "Peeked[%lu]: %s X:%.3f Y:%.3f Z:%.3f F:%.1f\r\n",
                    (unsigned long)test_peek_count,
                    gcode_names[block.type],
                    (double)block.x,
                    (double)block.y,
                    (double)block.z,
                    (double)block.feedrate);
            MMPrintString(buf);

            if (block.type == GCODE_CW_ARC ||
                block.type == GCODE_CCW_ARC)
            {
                if (block.use_radius)
                    sprintf(buf, "  Arc R:%.3f\r\n", (double)block.r);
                else
                    sprintf(buf, "  Arc I:%.3f J:%.3f\r\n",
                            (double)block.i,
                            (double)block.j);
                MMPrintString(buf);
            }

            // In test mode, advance shadow index to next block (without consuming)
            if (stepper_test_mode)
            {
                test_peek_index = (test_peek_index + 1) % GCODE_BUFFER_SIZE;
                test_peek_count++;
            }

            stepper_block_ready = false;
        }
        else
        {
            if (stepper_test_mode && test_peek_count >= gcode_buffer.count)
                MMPrintString("End of buffer (all blocks peeked)\r\n");
            else
                MMPrintString("No block ready\r\n");
        }
        return;
    }

    // STEPPER CLEAR - Clear the G-code buffer
    if (checkstring(cmdline, (unsigned char *)"CLEAR"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        // Don't allow clearing while executing motion
        if (stepper_system.motion_active && !stepper_test_mode)
            stepper_error("Cannot clear buffer while motion active");

        gcode_buffer_init(&gcode_buffer);
        return;
    }

    // STEPPER RUN - Exit test mode and start executing buffered commands
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

        if (!stepper_test_mode)
        {
            MMPrintString("Already running\r\n");
            return;
        }

        uint32_t save = save_and_disable_interrupts();
        stepper_test_mode = false;
        stepper_block_ready = false;
        test_peek_index = 0;
        test_peek_count = 0;
        restore_interrupts(save);
        MMPrintString("Stepper running - executing buffered commands\r\n");
        return;
    }

    // STEPPER TEST - Enter test mode (read-only observation, no motion)
    if (checkstring(cmdline, (unsigned char *)"TEST"))
    {
        if (!stepper_initialized)
            stepper_error("Stepper not initialized");

        if (stepper_test_mode)
        {
            MMPrintString("Already in test mode\r\n");
            return;
        }

        // Wait for current motion to complete before entering test mode
        if (stepper_system.motion_active)
        {
            MMPrintString("Waiting for motion to complete...\r\n");
            while (stepper_system.motion_active)
            {
                // Yield to allow ISR to run
            }
        }

        uint32_t save = save_and_disable_interrupts();
        stepper_test_mode = true;
        stepper_block_ready = false;
        // Initialize shadow index to current tail (first unexecuted block)
        test_peek_index = gcode_buffer.tail;
        test_peek_count = 0;
        restore_interrupts(save);
        MMPrintString("Test mode enabled - buffer observation only, no motion\r\n");
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
        sprintf(buf, "Mode: %s, Motion: %s\r\n",
                stepper_test_mode ? "TEST" : "RUN",
                stepper_system.motion_active ? "ACTIVE" : "IDLE");
        MMPrintString(buf);

        sprintf(buf, "Buffer: %d/%d blocks, executed: %lu\r\n",
                gcode_buffer.count, GCODE_BUFFER_SIZE,
                (unsigned long)gcode_buffer.blocks_executed);
        MMPrintString(buf);

        // Show axis positions
        if (axis_is_configured(&stepper_system.x))
        {
            sprintf(buf, "X: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.x.current_pos / (double)stepper_system.x.steps_per_mm,
                    (long)stepper_system.x.current_pos);
            MMPrintString(buf);
        }
        if (axis_is_configured(&stepper_system.y))
        {
            sprintf(buf, "Y: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.y.current_pos / (double)stepper_system.y.steps_per_mm,
                    (long)stepper_system.y.current_pos);
            MMPrintString(buf);
        }
        if (axis_is_configured(&stepper_system.z))
        {
            sprintf(buf, "Z: %.3f mm (%ld steps)\r\n",
                    (double)stepper_system.z.current_pos / (double)stepper_system.z.steps_per_mm,
                    (long)stepper_system.z.current_pos);
            MMPrintString(buf);
        }

        return;
    }

    stepper_error("Unknown STEPPER subcommand");
}
#endif