# Generic ROS 2 projection of the portable universal_gnss runtime state.
#
# Fields without capability gating use NaN when unavailable.
# Fields covered by capability_flags / value_flags use the same CAP_* bit:
#   capability=0                    -> unavailable / unsupported
#   capability=1, value=0           -> supported but no current value
#   capability=1, value=1, bool=0   -> available and false
#   capability=1, value=1, bool=1   -> available and true

uint8 FIX_TYPE_UNKNOWN=0
uint8 FIX_TYPE_NO_FIX=1
uint8 FIX_TYPE_FIX=2
uint8 FIX_TYPE_RTK_FLOAT=3
uint8 FIX_TYPE_RTK_FIXED=4
uint8 FIX_TYPE_DEAD_RECKONING=5

uint8 RTK_MODE_UNKNOWN=0
uint8 RTK_MODE_NONE=1
uint8 RTK_MODE_FLOAT=2
uint8 RTK_MODE_FIXED=3

uint32 CAP_RTK_MODE=1
uint32 CAP_HORIZONTAL_ACCURACY=2
uint32 CAP_VERTICAL_ACCURACY=4
uint32 CAP_HDOP=8
uint32 CAP_VDOP=16
uint32 CAP_SATELLITES_USED=32
uint32 CAP_SATELLITES_VISIBLE=64
uint32 CAP_SATELLITES_TRACKED=128
uint32 CAP_MEAN_CN0=256
uint32 CAP_MAX_CN0=512
uint32 CAP_CORRECTION_AGE=1024
uint32 CAP_HEADING=2048
uint32 CAP_DUAL_ANTENNA_HEADING=4096
uint32 CAP_INTERFERENCE_STATE=8192
uint32 CAP_JAMMING_STATE=16384
uint32 CAP_HEADING_ACCURACY=32768
uint32 CAP_DIFFERENTIAL_CORRECTIONS=65536
uint32 CAP_CORRECTIONS_ACTIVE=131072
uint32 CAP_DUAL_ANTENNA_BASELINE=262144
uint32 CAP_BASELINE_AZIMUTH=524288
uint32 CAP_BASELINE_PITCH=1048576
uint32 CAP_BASELINE_LENGTH=2097152
uint32 CAP_BASELINE_SOLUTION_STATUS=4194304

uint8 BASELINE_STATUS_UNKNOWN=0
uint8 BASELINE_STATUS_COMPUTED=1
uint8 BASELINE_STATUS_NOT_SOLVED=2
uint8 BASELINE_STATUS_INSUFFICIENT_OBSERVATIONS=3
uint8 BASELINE_STATUS_NO_CONVERGENCE=4
uint8 BASELINE_STATUS_OUT_OF_TOLERANCE=5
uint8 BASELINE_STATUS_COVARIANCE_TRACE_EXCEEDED=6
uint8 BASELINE_STATUS_NOT_CONFIGURED=7

builtin_interfaces/Time stamp
bool fix_valid
uint8 fix_type
uint8 rtk_mode
uint32 capability_flags
uint32 value_flags
float64 latitude_deg
float64 longitude_deg
float64 altitude_m
float32 horizontal_accuracy_m
float32 vertical_accuracy_m
float32 hdop
float32 vdop
uint16 satellites_used
uint16 satellites_visible
uint16 satellites_tracked
float32 mean_cn0_db_hz
float32 max_cn0_db_hz
float32 correction_age_s
float32 heading_deg
float32 heading_accuracy_deg
bool differential_corrections
bool corrections_active
bool dual_antenna_heading
bool interference_detected
bool jamming_detected
bool dual_antenna_baseline
float32 baseline_azimuth_deg
float32 baseline_pitch_deg
float32 baseline_length_m
uint8 baseline_solution_status
