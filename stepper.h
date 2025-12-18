#ifndef STEPPER_H
#define STEPPER_H
/***********************************************************************************************************************
PicoMite MMBasic

stepper.h

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

#include <stdbool.h>
#include "pico/stdlib.h"

/*
STEPPER INIT
Initialise the stepper subsystem.
This starts the 100kHz timer interrupt, initialises the G-code buffer and
starts DISARMED (no motion execution) until STEPPER RUN is issued.

STEPPER INIT [,arc_tolerance] [,buffer_size]
Initialise the stepper subsystem and optionally set the arc tolerance in mm.
This controls the maximum linear segment length used to approximate arcs.
Default is 0.5mm.
The optional buffer_size sets the number of buffered G-code blocks (default 16).

STEPPER CLOSE
Shutdown the stepper subsystem and stop the 100kHz timer interrupt.

STEPPER ESTOP
Emergency stop.
Immediately halts motion, clears the G-code buffer, disables drivers (if
enable pins are configured) and DISARMS motion execution.

STEPPER RECOVER
Recover from an abnormal state (eg. after a runtime error).
Stops any executing move, clears the G-code buffer, turns the spindle off,
and DISARMS motion execution (no automatic resume). Drivers are not automatically disabled.

STEPPER AXIS X|Y|Z, step_pin, dir_pin [,enable_pin] [,dir_invert] [,steps_per_mm] [,max_velocity] [,max_accel]
Configure an axis.
Pins may be specified as a physical pin number or GPxx.
‘enable_pin’ is optional; if omitted then ENABLE for that axis is unavailable.
‘dir_invert’ is 0 or 1.
‘max_velocity’ is in mm/min (same units as G-code F).
‘max_accel’ is in mm/s^2.
A reasonable default jerk is automatically calculated when axes are configured.

STEPPER JERK jerk_mm_s^3
Manually override the global jerk limit used for S-curve motion planning.
Optional: a conservative default (10× max acceleration) is auto-calculated when axes are configured.
Use this command to fine-tune jerk for specific machine characteristics.

STEPPER SCURVE 0|1
Enable (1) or disable (0) jerk-limited S-curve motion planning for standalone G0/G1 moves.
Arc segments (from G2/G3 linearisation) use the existing approach initially.

STEPPER INVERT X|Y|Z, 0|1
Invert the direction signal for an axis.

STEPPER ENABLE X|Y|Z|ALL [,0|1]
Enable (1, default) or disable (0) stepper drivers.
Enable pins are assumed active-low.

STEPPER SPINDLE pin [,invert]
Configure a spindle enable output pin.
M03 turns the spindle on and M05 turns it off.
Pins may be specified as a physical pin number or GPxx.
If 'invert' is 1 then the output is active-low.
Use pin 0 to disable spindle control.

STEPPER HWLIMITS X_MIN, Y_MIN, Z_MIN [,X_MAX] [,Y_MAX] [,Z_MAX]
Configure hardware limit switch pins (minimum 3, maximum 6 pins).
Pins are active-low (triggered when grounded).
Internal pull-ups are enabled, switches should connect pin to GND.
Limit switches trigger emergency stop in ISR: motion halts immediately,
G-code buffer is cleared, and the system is DISARMED.
Use 0 or omit optional parameters to disable max limit switches.
Example: STEPPER HWLIMITS GP2, GP3, GP4, GP5, GP6, GP7
Example: STEPPER HWLIMITS 10, 11, 12  ' Only min limits

STEPPER LIMITS X|Y|Z, min_mm, max_mm
Set soft limits for an axis.
Moves that would exceed limits will generate an error.

STEPPER POSITION X|Y|Z, position_mm
Set the current position for an axis (in mm).
This establishes the hardware position and marks position as known.
G28 homing will override this by zeroing the hardware position.

STEPPER RESET
Reset all axis configurations to defaults.
Note: This does not stop the 100kHz timer; use STEPPER CLOSE for shutdown.

STEPPER GCODE G0|G1|G2|G3|G28|G61|G64|G90|G91|G92|M03|M05 [,X x] [,Y y] [,Z z] [,F feedrate] [,I i] [,J j] [,K k] [,R r]
Queue a G-code command into the circular buffer.
G28: Home specified axes (requires min limit switches configured).
     Homes in negative direction at 50% then 5% of max speed.
     Zeros hardware position and clears G92 offsets.
     Example: STEPPER GCODE G28, X, 1, Y, 1 (homes X and Y)
G92: Set workspace coordinate offsets (without moving machine).
     G92 X10 means "current position is now X=10 in workspace coordinates".
     Allows setting work coordinate system independent of machine zero.
     Example: STEPPER GCODE G92, X, 0, Y, 0 (set current position as workspace origin)
M03: Spindle on (requires STEPPER SPINDLE configured). Immediate (not buffered).
M05: Spindle off (requires STEPPER SPINDLE configured). Immediate (not buffered).
G61 selects exact stop mode (no corner blending between moves).
G64 selects continuous mode (corner blending enabled for consecutive linear/rapid moves).
G90 selects absolute mode and G91 selects incremental mode (no motion).
For G1/G2/G3 a feedrate must have been set.
Feedrate 'F' is specified in mm/min (as per G-code) and is converted to mm/s internally.
Arc commands G2/G3 require either I,J (centre offsets) or R (radius).
All motion commands use workspace coordinates (respecting G92 offsets).
Soft limits are checked against hardware position (workspace coords - G92 offset).

STEPPER BUFFER
Report the current number of buffered blocks and the executed count.

STEPPER CLEAR
Clear the G-code buffer.
Cannot be used while motion is active.

STEPPER RUN
Arm motion execution and begin executing queued motion.

STEPPER STATUS
Report arming state, motion state, buffer status, and axis positions.

MM.CODE
Pseudo-variable returning free slots remaining in the stepper G-code buffer.
Use this to avoid "G-code buffer full" errors, e.g. only issue STEPPER GCODE when MM.CODE > 0.
*/

// Free slots remaining in the G-code circular buffer (0..GCODE_BUFFER_SIZE).
// Updated by buffer push/pop and safe to read from MMBasic (MM.CODE).
#ifdef rp2350
extern volatile uint16_t stepper_gcode_buffer_space;

// Manual recovery hook to return the stepper subsystem to a known-safe state.
// Safe to call even if STEPPER INIT has never been run (it will be a no-op).
void stepper_recover_to_safe_state(void);

// Error abort hook: used on runtime error termination.
// Does not stop the 100kHz IRQ or lose axis configuration.
// DISARMS motion execution, disables drivers (if enable pins exist), clears the buffer,
// turns the spindle off, and marks position as unknown.
void stepper_abort_to_safe_state_on_error(void);

// Fully shuts down the stepper subsystem (like STEPPER CLOSE).
// Safe to call even if STEPPER INIT has never been run (it will be a no-op).
void stepper_close_subsystem(void);

// Structure to hold parameters for a single stepper motor axis
typedef struct
{
    // GPIO pin assignments
    uint step_pin;   // Step pulse pin
    uint dir_pin;    // Direction control pin
    uint enable_pin; // Enable pin (optional, set to 0xFF if not used)

    // Motion parameters
    float steps_per_mm; // Steps per millimeter (or unit of measurement)
    float max_velocity; // Maximum velocity in mm/s (configured in mm/min)
    float max_accel;    // Maximum acceleration in mm/s²

    // Current state
    int32_t current_pos; // Current position in steps
    int32_t target_pos;  // Target position in steps
    float current_vel;   // Current velocity in steps/s

    // Motion profile variables
    float accel_distance;   // Distance needed for acceleration phase
    float decel_distance;   // Distance needed for deceleration phase
    uint32_t step_delay_us; // Current delay between steps in microseconds

    // Direction and limits
    bool dir_invert;   // Invert direction signal
    int32_t min_limit; // Soft minimum limit in steps
    int32_t max_limit; // Soft maximum limit in steps
    bool homing_dir;   // Homing direction (true = positive)

} stepper_axis_t;

// Structure to hold all three axes
typedef struct
{
    stepper_axis_t x; // X-axis parameters
    stepper_axis_t y; // Y-axis parameters
    stepper_axis_t z; // Z-axis parameters

    // Global motion parameters
    bool motion_active;     // Is any motion currently active
    uint32_t step_interval; // Base step timer interval in microseconds

    // Hardware limit switches (optional)
    uint8_t x_min_limit_pin;    // X minimum limit switch pin (0xFF = not used)
    uint8_t x_max_limit_pin;    // X maximum limit switch pin (0xFF = not used)
    uint8_t y_min_limit_pin;    // Y minimum limit switch pin (0xFF = not used)
    uint8_t y_max_limit_pin;    // Y maximum limit switch pin (0xFF = not used)
    uint8_t z_min_limit_pin;    // Z minimum limit switch pin (0xFF = not used)
    uint8_t z_max_limit_pin;    // Z maximum limit switch pin (0xFF = not used)
    uint64_t limit_switch_mask; // Bitmask of all limit switch pins for fast checking
    bool limits_enabled;        // True if any limit switches are configured

    // Homing state
    bool homing_active;     // True when executing G28 homing cycle
    float homing_fast_rate; // Fast homing speed as fraction of max (default 0.5 = 50%)
    float homing_slow_rate; // Slow homing speed as fraction of max (default 0.05 = 5%)

    // Workspace coordinate system (G92 offsets)
    float x_g92_offset;  // G92 workspace offset for X axis (mm)
    float y_g92_offset;  // G92 workspace offset for Y axis (mm)
    float z_g92_offset;  // G92 workspace offset for Z axis (mm)
    bool position_known; // True if POSITION or G28 has established machine position

    // Spindle control (optional)
    uint8_t spindle_pin; // Output pin for spindle enable (0xFF = not used)
    bool spindle_invert; // 0 = active-high, 1 = active-low
    bool spindle_on;     // Current spindle output state

} stepper_system_t;

// Motion phases for a single move
typedef enum
{
    MOVE_PHASE_IDLE = 0, // No move in progress
    MOVE_PHASE_ACCEL,    // Accelerating
    MOVE_PHASE_CRUISE,   // Constant velocity
    MOVE_PHASE_DECEL,    // Decelerating

    // Jerk-limited S-curve phases (optional per-block)
    MOVE_PHASE_S_JERK_UP_ACCEL,   // Accel: jerk up (a ramps 0 -> +amax)
    MOVE_PHASE_S_CONST_ACCEL,     // Accel: constant +amax
    MOVE_PHASE_S_JERK_DOWN_ACCEL, // Accel: jerk down (+amax -> 0)
    MOVE_PHASE_S_CRUISE,          // Cruise: a = 0
    MOVE_PHASE_S_JERK_UP_DECEL,   // Decel: jerk up (a ramps 0 -> -amax)
    MOVE_PHASE_S_CONST_DECEL,     // Decel: constant -amax
    MOVE_PHASE_S_JERK_DOWN_DECEL  // Decel: jerk down (-amax -> 0)
} move_phase_t;

// Structure to track an executing multi-axis move (integer math for ISR)
// Uses Bresenham algorithm for coordinated motion

// Fundamental timing constants
#define ISR_FREQ 100000                // 100KHz interrupt frequency (Hz)
#define STEP_ACCUMULATOR_MAX 100000000 // 100M threshold for step timing

// STEP pulse width in microseconds.
// Many stepper drivers require a minimum high time (often >= 1us). Keeping this
// configurable allows tuning for different driver requirements.
#ifndef STEPPER_STEP_PULSE_US
#define STEPPER_STEP_PULSE_US 2
#endif

// Derived constant for velocity calculations
// Derived from ISR_FREQ and STEP_ACCUMULATOR_MAX.
//
// Accumulator-based step timing:
//   - Each ISR tick: step_accumulator += step_rate
//   - When step_accumulator >= STEP_ACCUMULATOR_MAX: take a step
//   - Step frequency = step_rate * ISR_FREQ / STEP_ACCUMULATOR_MAX
//
// RATE UNIT SYSTEM (ISR integer arithmetic):
// All velocity and acceleration values in the ISR and gcode_block_t use
// dimensionless "rate units" to avoid floating-point math in interrupt context.
//
// Conversion formulas:
//   rate_to_vel = steps_per_mm × RATE_SCALE
//
// Velocity (mm/s) ↔ Rate units:
//   step_rate = velocity(mm/s) × steps_per_mm × RATE_SCALE
//   velocity(mm/s) = step_rate / rate_to_vel
//
// Acceleration (mm/s²) ↔ Rate units per tick:
//   accel_increment = acceleration(mm/s²) × steps_per_mm × RATE_SCALE / ISR_FREQ
//   acceleration(mm/s²) = accel_increment × ISR_FREQ / (steps_per_mm × RATE_SCALE)
//
// Jerk (mm/s³) ↔ Rate units per tick²:
//   jerk_increment = jerk(mm/s³) × steps_per_mm × RATE_SCALE / ISR_FREQ²
//   jerk(mm/s³) = jerk_increment × ISR_FREQ² / (steps_per_mm × RATE_SCALE)
//
// Example (200 steps/mm, 100 mm/s velocity):
//   rate_to_vel = 200 × 1000 = 200,000
//   step_rate = 100 × 200,000 = 20,000,000 rate units
//
// Note: All motion planning calculations in planner use mm/s and mm/s², then
// convert to rate units before storing in gcode_block_t for ISR execution.
//
#define RATE_SCALE ((float)STEP_ACCUMULATOR_MAX / (float)ISR_FREQ) // = 1000 (velocity to step_rate)

typedef struct arc_segment_runtime
{
    int32_t x_steps;
    int32_t y_steps;
    int32_t z_steps;
    bool x_dir;
    bool y_dir;
    bool z_dir;
    int32_t major_steps;
    uint8_t major_axis_mask;
} arc_segment_runtime_t;

typedef struct
{
    // Current phase
    volatile move_phase_t phase;

    // Axis participation (which axes are moving)
    volatile bool x_active;
    volatile bool y_active;
    volatile bool z_active;

    // Direction per axis (true = positive, false = negative)
    volatile bool x_dir;
    volatile bool y_dir;
    volatile bool z_dir;

    // Metadata for boundary management
    volatile uint8_t major_axis_mask;      // Bitmask of axes whose step counts match the major axis
    volatile bool allow_accumulator_carry; // True if this block permits step accumulator continuity

    // Bresenham algorithm state
    // Major axis is the one with most steps - it drives the timing
    volatile int32_t major_steps; // Total steps for major axis
    volatile int32_t x_steps;     // Total steps for X
    volatile int32_t y_steps;     // Total steps for Y
    volatile int32_t z_steps;     // Total steps for Z
    volatile int32_t x_error;     // Bresenham error term for X
    volatile int32_t y_error;     // Bresenham error term for Y
    volatile int32_t z_error;     // Bresenham error term for Z

    // Steps tracking (based on major axis)
    volatile int32_t steps_remaining;       // Steps left in current phase
    volatile int32_t total_steps_remaining; // Total steps left in move (major axis)

    // Step timing (fixed-point accumulator)
    // All rate values in ISR "rate units" = velocity(mm/s) × steps_per_mm × RATE_SCALE
    volatile uint32_t step_accumulator; // Accumulates until >= STEP_ACCUMULATOR_MAX
    volatile int32_t step_rate;         // Current velocity in rate units (signed)
    volatile int32_t cruise_rate;       // Target cruise velocity in rate units (signed)
    volatile int32_t exit_rate;         // Target end velocity in rate units (signed)
    volatile int32_t accel_increment;   // Acceleration in rate units per ISR tick (signed)

    // Optional jerk-limited S-curve state
    volatile int32_t accel_rate;     // Acceleration rate in rate units per tick (signed)
    volatile int32_t jerk_increment; // Jerk in rate units per tick² (signed)

    // S-curve phase step counts (for transition detection, based on major axis)
    volatile int32_t scurve_ju_accel_steps;
    volatile int32_t scurve_ca_steps;
    volatile int32_t scurve_jd_accel_steps;
    volatile int32_t scurve_cruise_steps;
    volatile int32_t scurve_ju_decel_steps;
    volatile int32_t scurve_cd_steps;
    volatile int32_t scurve_jd_decel_steps;

    // Phase step counts (for transition detection, based on major axis)
    volatile int32_t accel_steps;  // Total steps in accel phase
    volatile int32_t cruise_steps; // Total steps in cruise phase
    volatile int32_t decel_steps;  // Total steps in decel phase

    // Arc execution state (when running a bundled multi-segment arc)
    volatile bool arc_active;
    volatile uint16_t arc_segment_count;
    volatile uint16_t arc_segment_index;
    volatile int32_t arc_segment_steps_remaining;
    volatile arc_segment_runtime_t *arc_segments;

} stepper_move_t;

// G-code command types
typedef enum
{
    GCODE_RAPID_MOVE = 0,  // G00 - Rapid positioning
    GCODE_LINEAR_MOVE = 1, // G01 - Linear interpolation
    GCODE_CW_ARC = 2,      // G02 - Circular interpolation clockwise
    GCODE_CCW_ARC = 3      // G03 - Circular interpolation counterclockwise
} gcode_motion_type_t;

// Structure for a single G-code motion command
typedef struct
{
    gcode_motion_type_t type; // Command type (G00, G01, G02, G03)

    // Target position (absolute or relative depending on mode)
    float x;    // X target coordinate
    float y;    // Y target coordinate
    float z;    // Z target coordinate
    bool has_x; // X coordinate specified
    bool has_y; // Y coordinate specified
    bool has_z; // Z coordinate specified

    // Arc parameters (for G02/G03)
    float i;         // X offset to arc center
    float j;         // Y offset to arc center
    float k;         // Z offset to arc center (for helical arcs)
    float r;         // Arc radius (alternative to I, J)
    bool use_radius; // True if R parameter used instead of I, J

    // Motion parameters
    float feedrate; // Feedrate in mm/s (converted from G-code mm/min)

    // Calculated values (filled during planning)
    float distance;   // Total distance of move
    uint32_t steps_x; // Number of steps for X axis
    uint32_t steps_y; // Number of steps for Y axis
    uint32_t steps_z; // Number of steps for Z axis

    // Motion profile
    float entry_velocity; // Velocity at start of move
    float exit_velocity;  // Velocity at end of move
    float max_velocity;   // Maximum velocity for this move
    float accel_distance; // Distance for acceleration phase
    float decel_distance; // Distance for deceleration phase

    // Pre-computed motion profile for ISR (integer math, multi-axis)
    // Per-axis step counts and directions
    int32_t x_steps_planned; // Absolute steps for X
    int32_t y_steps_planned; // Absolute steps for Y
    int32_t z_steps_planned; // Absolute steps for Z
    bool x_dir;              // X direction (true = positive)
    bool y_dir;              // Y direction (true = positive)
    bool z_dir;              // Z direction (true = positive)

    // Major axis (most steps) determines timing
    int32_t major_steps;          // Steps for major axis
    int32_t accel_steps;          // Steps in acceleration phase (major axis)
    int32_t cruise_steps;         // Steps in cruise phase (major axis)
    int32_t decel_steps;          // Steps in deceleration phase (major axis)
    uint8_t major_axis_mask;      // Bitmask of axes that share the major step count
    bool allow_accumulator_carry; // Whether this block allows step accumulator continuity into the next block

    // Velocity and acceleration in ISR accumulator "rate units" (NOT mm/s)
    // Rate units: velocity(mm/s) × steps_per_mm × RATE_SCALE
    // Conversion: rate_to_vel = steps_per_mm × RATE_SCALE
    //   velocity(mm/s) = rate / rate_to_vel
    //   rate = velocity(mm/s) × rate_to_vel
    int32_t entry_rate;      // Initial velocity in rate units (signed)
    int32_t exit_rate;       // Final velocity in rate units (signed)
    int32_t cruise_rate;     // Cruise velocity in rate units (signed)
    int32_t accel_increment; // Acceleration in rate units per ISR tick (signed)

    // Optional jerk-limited S-curve profile (only for standalone G0/G1 blocks)
    bool use_scurve;
    int32_t jerk_increment;     // Jerk in rate units per tick (can be negative)
    uint8_t initial_phase;      // Precomputed starting phase (avoids if-else chain in ISR)
    int32_t initial_accel_rate; // Precomputed starting accel_rate
    int32_t scurve_ju_accel_steps;
    int32_t scurve_ca_steps;
    int32_t scurve_jd_accel_steps;
    int32_t scurve_cruise_steps;
    int32_t scurve_ju_decel_steps;
    int32_t scurve_cd_steps;
    int32_t scurve_jd_decel_steps;

    // Geometry for junction speed estimation (unit direction vector + scaling)
    float unit_x;
    float unit_y;
    float unit_z;
    float virtual_steps_per_mm; // major_steps / move_length_mm
    float min_accel;            // Effective accel limit along this path (mm/s^2)

    // Status flags
    bool is_planned; // Motion planning completed

    // Bundled arc execution payload (only for G02/G03)
    uint16_t arc_segment_count;
    arc_segment_runtime_t *arc_segments; // Heap-allocated array owned by planner until ISR takes it

} gcode_block_t;

// Runtime representation stored in the execution buffer (omits parser-only fields)
typedef struct
{
    gcode_motion_type_t type;
    bool is_planned;
    bool allow_accumulator_carry;
    uint8_t major_axis_mask;

    int32_t x_steps_planned;
    int32_t y_steps_planned;
    int32_t z_steps_planned;
    bool x_dir;
    bool y_dir;
    bool z_dir;

    int32_t major_steps;
    int32_t accel_steps;
    int32_t cruise_steps;
    int32_t decel_steps;

    int32_t entry_rate;
    int32_t exit_rate;
    int32_t cruise_rate;
    int32_t accel_increment;

    bool use_scurve;
    int32_t jerk_increment;
    uint8_t initial_phase;
    int32_t initial_accel_rate;
    int32_t scurve_ju_accel_steps;
    int32_t scurve_ca_steps;
    int32_t scurve_jd_accel_steps;
    int32_t scurve_cruise_steps;
    int32_t scurve_ju_decel_steps;
    int32_t scurve_cd_steps;
    int32_t scurve_jd_decel_steps;

    float unit_x;
    float unit_y;
    float unit_z;
    float virtual_steps_per_mm;
    float min_accel;
    float distance;
    float max_velocity;

    uint16_t arc_segment_count;
    arc_segment_runtime_t *arc_segments;

} stepper_runtime_block_t;

// Circular buffer for G-code blocks
#define GCODE_BUFFER_DEFAULT_SIZE 32
#define GCODE_BUFFER_MAX_SIZE 1024

typedef struct
{
    stepper_runtime_block_t *blocks; // Ring buffer storage (allocated at INIT)
    uint16_t size;                   // Number of blocks allocated

    volatile uint16_t head;  // Write position (where new blocks are added)
    volatile uint16_t tail;  // Read position (currently executing block)
    volatile uint16_t count; // Number of blocks in buffer

    // Parser state (modal G-code state)
    gcode_motion_type_t current_motion_mode; // Current motion mode (modal)
    float current_feedrate;                  // Current feedrate in mm/s (modal)
    bool feedrate_set;                       // True if feedrate has been set
    bool absolute_mode;                      // True for G90, false for G91
    bool exact_stop_mode;                    // True for G61 (exact stop), false for G64 (continuous)
    // Note: Planner position (last_x/y/z) is now separate static state in stepper.c
    // This allows future look-ahead and junction blending without ISR conflicts

    // Buffer management
    bool buffer_full;         // Buffer full flag
    bool buffer_empty;        // Buffer empty flag
    uint32_t blocks_executed; // Total blocks executed counter

} gcode_buffer_t;
#endif
#endif