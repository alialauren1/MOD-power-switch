**Purpose**
Designing data logger & power switch to know where system is underwater and turn higher powered oceanographic instrumentation on/off. 

**Current Main Status Description:**

Rudimentary controller concept with GPIO pin output for high/low controller output, based on depth and direction from Keller pressure sensor and Magnetic Hall effect sensor. 

Controller and Logger are decoupled in which both can be on, or just one, they function independently from one another.

Depths for controller power switch on/off are fixed thresholds and not adaptive. switch_on_depth_mbar / switch_off_depth_mbar are fixed once the user sets them, they don't adapt to the profile pathway or update themselves during deployment unless the user actually connects to the system and calls set_switch_on_depth to change that variable. 

**User sets the following on startup:**

- sample rate hz
- logging task on/off via logging_on_flg
- controller task on/off via controller_on_flg
- switch_off_direction
- switch_off_depth_mbar
- switch_on_direction
- switch_on_depth_mbar 

**CLI functions** 

- change the switch_on_depth value via: set_switch_on_depth
- stop_acqu : stops acquisition and goes to idle state in executive task
- start_acqu : starts acquisition and goes into acquisition state in executive task
- read_sensors : reads current values on sensors
- get_time : checks the current time 
- set_time : sets the time and stores that with relative time into file for backlogging sample time
- get_file_name : checks the file name that is open
