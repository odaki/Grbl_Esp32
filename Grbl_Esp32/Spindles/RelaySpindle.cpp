/*
    RelaySpindle.cpp

    This is used for a basic on/off spindle. All S Values about 1
    will turn the spindle on.

    Part of Grbl_ESP32
    2020 -	Bart Dring

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

*/

// ========================= RelaySpindle ==================================
/*
    This is the same as a PWM spindle, but is a digital rather than PWM output
*/
void RelaySpindle::init() {
    get_pins_and_settings();

    if (_output_pin == UNDEFINED_PIN)
        return;

    pinMode(_output_pin, OUTPUT);

    if (_enable_pin != UNDEFINED_PIN)
        pinMode(_enable_pin, OUTPUT);

    if (_direction_pin != UNDEFINED_PIN)
        pinMode(_direction_pin, OUTPUT);

    is_reversable = (_direction_pin != UNDEFINED_PIN);

    config_message();
}

// prints the startup message of the spindle config
void RelaySpindle :: config_message() {
    grbl_msg_sendf(CLIENT_SERIAL, MSG_LEVEL_INFO, "Relay spindle on Pin:%d", _output_pin);
}

float RelaySpindle::set_rpm(float rpm) {
    if (_output_pin == UNDEFINED_PIN)
        return rpm;

    sys.spindle_speed = rpm;

    if (rpm == 0) {
        sys.spindle_speed = 0.0;
        set_output(0);
    } else {
        sys.spindle_speed = rpm;
        set_output(1);
    }

    return rpm;
}

void RelaySpindle::set_output(uint32_t duty) {
#ifdef INVERT_SPINDLE_PWM
    duty = (duty == 0); // flip duty
#endif
    digitalWrite(_output_pin, duty > 0); // anything greater
}
