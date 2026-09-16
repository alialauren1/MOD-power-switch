**Purpose**

Designing data logger & power switch to know where system is underwater and turn
higher powered oceanographic instrumentation on/off.

**Current Main Status Description:** (V1.2)

Controller with GPIO pin output for high/low controller output, based on depth
and direction from Keller pressure sensor and magnetic Hall effect sensor.

Controller and Logger are decoupled — both can be on, or just one. They function
independently of one another.

Switch-on depth is adaptive. The controller derives a lag at startup from
`expected_bottom_turnaround_depth_mbar`, then re-corrects `switch_on_depth_mbar`
after every measured bottom turn-around, so the trigger tracks the actual
profile rather than staying at whatever the user set. `switch_off_depth_mbar`
remains a fixed threshold.

Adaptation is currently derived for `DOWNCAST` only. `UPCAST` would need an
expected *top* turn-around, which isn't implemented.

The instrument output (PA13) is fail-safe: it defaults HIGH (instrument ON) at
boot, on every stop, and on entry to the error state, so a fault never leaves the
payload unpowered.

**User sets the following in `config.cfg` on the SD card:**

- sample_rate_hz
- logging_on_flg — logging task on/off
- controller_on_flg — controller task on/off
- switch_off_direction
- switch_off_depth_mbar
- switch_on_direction
- switch_on_depth_mbar — starting value; adapts during deployment
- expected_bottom_turnaround_depth_mbar — seed until a real turn-around is measured

If `config.cfg` is missing, it is created with defaults. If it is missing keys,
it is rewritten with them.

**CLI functions**

- start_acqu : starts acquisition and goes into acquisition state in executive task
- stop_acqu : stops acquisition and goes to idle state in executive task
- read_sensors : reads current values on sensors
- get_time : checks the current time
- set_time : sets the time and stores that with relative time into file for
  backlogging sample time — requires an open data file
- get_file_name : checks the file name that is open
- sd_read : reads back from the card
- echo_str / echo_int : SDK loopback tests

`start_acqu`, `stop_acqu` and `read_sensors` are refused while the system is in
`SYS_ERR`.
