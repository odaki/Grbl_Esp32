/*
  polar_coaster.cpp - Implements simple inverse kinematics for Grbl_ESP32
  Part of Grbl_ESP32

  Copyright (c) 2019 Barton Dring @buildlog


  Grbl is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Grbl is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Grbl.  If not, see <http://www.gnu.org/licenses/>.

	Inverse kinematics determine the joint parameters required to get to a position
	in 3D space. Grbl will still work as 3 axes of steps, but these steps could
	represent angles, etc instead of linear units.

	Unless forward kinematics are applied to the reporting, Grbl will report raw joint
	values instead of the normal Cartesian positions

	How it works...

	If you tell it to go to X10 Y10 Z10 in Cartesian space, for example, the equations
	will convert those values to the required joint values. In the case of a polar machine, X represents the radius,
	Y represents the polar degrees and Z would be unchanged.

	In most cases, a straight line in Cartesian space could cause a curve in the new system.
	To fix this, the line is broken into very small segments and each segment is converted
	to the new space. While each segment is also distorted, the amount is so small it cannot be seen.

	This segmentation is how normal Grbl draws arcs.

	Feed Rate

	Feed rate is given in steps/time. Due to the new coordinate units and non linearity issues, the
	feed rate may need to be adjusted. The ratio of the step distances in the original coordinate system
	determined and applied to the feed rate.

	TODO:
		Add y offset, for completeness
		Add ZERO_NON_HOMED_AXES option


*/

// This file is enabled by defining CUSTOM_CODE_FILENAME "polar_coaster.cpp"
// in Machines/polar_coaster.h, thus causing this file to be included
// from ../custom_code.cpp

static void  calc_polar(float* target_xyz, float* polar, float last_angle);
static float abs_angle(float ang);

static float last_angle  = 0.0f;
static float last_radius = 0.0f;

// this get called before homing
// return false to complete normal home
// return true to exit normal homing
bool kinematics_pre_homing(uint8_t cycle_mask) {
    bool rc = false;
    if (cycle_mask == HOMING_CYCLE_ALL || bitnum_istrue(cycle_mask, RADIUS_AXIS)) {
        sys_position[RADIUS_AXIS] = 0;
        last_radius               = 0.0f;
        // reset polar axis too
        sys_position[POLAR_AXIS] = 0;
        last_angle               = 0.0f;
    }
    if (bitnum_istrue(cycle_mask, POLAR_AXIS)) {
        sys_position[POLAR_AXIS] = 0;
        last_angle               = 0.0f;
        rc                       = true;  // the homing is completed.
    }
    return rc;  // continue to the normal homing cycle
}

void kinematics_post_homing(uint8_t cycle_mask) {
    if (cycle_mask == HOMING_CYCLE_ALL || bitnum_istrue(cycle_mask, RADIUS_AXIS)) {
        float x_offset = gc_state.coord_system[RADIUS_AXIS] + gc_state.coord_offset[RADIUS_AXIS];  // offset from machine coordinate system
        last_radius    = sys_position[RADIUS_AXIS] / axis_settings[RADIUS_AXIS]->steps_per_mm->get() - x_offset;
    }
}

/*
 Apply inverse kinematics for a polar system

 float target: 					The desired target location in machine space
 plan_line_data_t *pl_data:		Plan information like feed rate, etc
 float *position:				The previous "from" location of the move

 Note: It is assumed only the radius axis (X) is homed and only X and Z have offsets


*/

bool cartesian_to_motors(float* target, plan_line_data_t* pl_data, float* position) {
    float    dx, dy, dz;     // distances in each cartesian axis
    float    dist;           // the distances in both systems...used to determine feed rate
    uint32_t segment_count;  // number of segments the move will be broken in to.
    float    x_offset = gc_state.coord_system[X_AXIS] + gc_state.coord_offset[X_AXIS];  // offset from machine coordinate system
    float    z_offset = gc_state.coord_system[Z_AXIS] + gc_state.coord_offset[Z_AXIS];  // offset from machine coordinate system
    //grbl_sendf(CLIENT_SERIAL, "Position: %4.2f %4.2f %4.2f \r\n", position[X_AXIS] - x_offset, position[Y_AXIS], position[Z_AXIS]);
    //grbl_sendf(CLIENT_SERIAL, "Target: %4.2f %4.2f %4.2f \r\n", target[X_AXIS] - x_offset, target[Y_AXIS], target[Z_AXIS]);
    // calculate cartesian move distance for each axis
    dx = target[X_AXIS] - position[X_AXIS];
    dy = target[Y_AXIS] - position[Y_AXIS];
    dz = target[Z_AXIS] - position[Z_AXIS];
    // calculate the total X,Y axis move distance
    // Z axis is the same in both coord systems, so it is ignored
    dist = sqrt((dx * dx) + (dy * dy));
    if (pl_data->motion.rapidMotion) {
        segment_count = 1;  // rapid G0 motion is not used to draw, so skip the segmentation
    } else {
        segment_count = ceil(dist / SEGMENT_LENGTH);  // determine the number of segments we need	... round up so there is at least 1
    }
    dist /= segment_count;  // segment distance

    plan_line_data_t tmp_plan_data = *pl_data;  // struct copy to save original feed_rate
    for (uint32_t segment = 1; segment <= segment_count; segment++) {
        // determine this segment's target
        float seg_target[N_AXIS];  // The target of the current segment
        seg_target[X_AXIS] = position[X_AXIS] + (dx / float(segment_count) * segment) - x_offset;
        seg_target[Y_AXIS] = position[Y_AXIS] + (dy / float(segment_count) * segment);
        seg_target[Z_AXIS] = position[Z_AXIS] + (dz / float(segment_count) * segment) - z_offset;

        float polar[N_AXIS];  // target location in polar coordinates
        calc_polar(seg_target, polar, last_angle);
        // begin determining new feed rate
        // calculate move distance for each axis
        float p_dx, p_dy;  // distances in each polar axis
        p_dx = polar[RADIUS_AXIS] - last_radius;
        if (polar[RADIUS_AXIS] - last_radius <= 180.0f) {
            p_dy = sin(radians(polar[POLAR_AXIS])) * polar[RADIUS_AXIS] - sin(radians(last_angle)) * last_radius;
        } else {
            p_dy = sin(radians(last_angle)) * last_radius - sin(radians(polar[POLAR_AXIS])) * polar[RADIUS_AXIS];
        }
        float polar_dist          = sqrt((p_dx * p_dx) + (p_dy * p_dy));  // calculate the total move distance
        float polar_rate_multiply = 1.0f;
        if (polar_dist != 0.0f && dist != 0.0f) {
            // calc a feed rate multiplier
            polar_rate_multiply = polar_dist / dist;
            if (polar_rate_multiply < 0.5f) {
                // prevent much slower speed
                polar_rate_multiply = 0.5f;
            }
        }
        tmp_plan_data.feed_rate = pl_data->feed_rate * polar_rate_multiply;  // apply the distance ratio between coord systems
        // end determining new feed rate
        polar[RADIUS_AXIS] += x_offset;
        polar[Z_AXIS] += z_offset;

        // mc_line() returns false if a jog is cancelled.
        // In that case we stop sending segments to the planner.
        if (!mc_line(polar, &tmp_plan_data)) {
            return false;
        }

        //
        last_radius = polar[RADIUS_AXIS] - x_offset;
        last_angle  = polar[POLAR_AXIS];
    }
    // TO DO don't need a feedrate for rapids
    return true;
}

/*
Forward kinematics converts position back to the original cartesian system. It is
typically used for reporting

For example, on a polar machine, you tell it to go to a place like X10Y10. It
converts to a radius and angle using inverse kinematics. The machine posiiton is now
in those units X14.14 (radius) and Y45 (degrees). If you want to report those units as
X10,Y10, you would use forward kinematics

position = the current machine position
converted = position with forward kinematics applied.

*/
void motors_to_cartesian(float* cartesian, float* motors, int n_axis) {
    const float mm_per_rotation = 90.0f * 4;
    float       x_offset        = gc_state.coord_system[X_AXIS] + gc_state.coord_offset[X_AXIS];  // offset from machine coordinate system

    float angle = 360.0f * fmod(motors[Y_AXIS], mm_per_rotation) / mm_per_rotation;
    float r     = motors[X_AXIS] - x_offset;

    float x = cos(radians(angle)) * r;
    float y = sin(radians(angle)) * r;

    cartesian[X_AXIS] = -x;
    cartesian[Y_AXIS] = y;
    cartesian[Z_AXIS] = motors[Z_AXIS];  // unchanged
}

// helper functions

/*******************************************
* Calculate polar values from Cartesian values
* 	float target_xyz:	An array of target axis positions in Cartesian (xyz) space
*		float polar: 			An array to return the polar values
*		float last_angle:	The polar angle of the "from" point.
*
*   Angle calculated is 0 to 360, but you don't want a line to go from 350 to 10. This would
*   be a long line backwards. You want it to go from 350 to 370. The same is true going the other way.
*
*   This means the angle could accumulate to very high positive or negative values over the coarse of
*   a long job.
*
*/
static void calc_polar(float* target_xyz, float* polar, float last_angle) {
    float delta_ang;  // the difference from the last and next angle
    polar[RADIUS_AXIS] = hypot_f(target_xyz[X_AXIS], target_xyz[Y_AXIS]);
    if (polar[RADIUS_AXIS] == 0) {
        polar[POLAR_AXIS] = last_angle;  // don't care about angle at center
    } else {
        polar[POLAR_AXIS] = atan2(target_xyz[Y_AXIS], target_xyz[X_AXIS]) * 180.0 / M_PI;
        // no negative angles...we want the absolute angle not -90, use 270
        polar[POLAR_AXIS] = abs_angle(polar[POLAR_AXIS]);
    }
    polar[Z_AXIS] = target_xyz[Z_AXIS];  // Z is unchanged
    delta_ang     = polar[POLAR_AXIS] - abs_angle(last_angle);
    // if the delta is above 180 degrees it means we are crossing the 0 degree line
    if (fabs(delta_ang) <= 180.0)
        polar[POLAR_AXIS] = last_angle + delta_ang;
    else {
        if (delta_ang > 0.0) {
            // crossing zero counter clockwise
            polar[POLAR_AXIS] = last_angle - (360.0 - delta_ang);
        } else
            polar[POLAR_AXIS] = last_angle + delta_ang + 360.0;
    }
}

// Return a 0-360 angle ... fix above 360 and below zero
static float abs_angle(float ang) {
    ang = fmod(ang, 360.0);  // 0-360 or 0 to -360
    if (ang < 0.0)
        ang = 360.0 + ang;
    return ang;
}

// Polar coaster has macro buttons, this handles those button pushes.
void user_defined_macro(uint8_t index) {
    switch (index) {
        case 0:
            WebUI::inputBuffer.push("$H\r");  // home machine
            break;
        case 1:
            WebUI::inputBuffer.push("[ESP220]/1.nc\r");  // run SD card file 1.nc
            break;
        case 2:
            WebUI::inputBuffer.push("[ESP220]/2.nc\r");  // run SD card file 2.nc
            break;
        default:
            break;
    }
}

// handle the M30 command
void user_m30() {
    WebUI::inputBuffer.push("$H\r");
}
