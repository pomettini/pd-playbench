#ifndef PD_PLAYBENCH_H
#define PD_PLAYBENCH_H

#include "pd_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef PD_PLAYBENCH_MAX_COMMANDS
#define PD_PLAYBENCH_MAX_COMMANDS 256
#endif

typedef enum PDBenchButton {
    PD_BENCH_BUTTON_LEFT  = 1 << 0,
    PD_BENCH_BUTTON_RIGHT = 1 << 1,
    PD_BENCH_BUTTON_UP    = 1 << 2,
    PD_BENCH_BUTTON_DOWN  = 1 << 3,
    PD_BENCH_BUTTON_A     = 1 << 4,
    PD_BENCH_BUTTON_B     = 1 << 5,
    PD_BENCH_BUTTON_MENU  = 1 << 6
} PDBenchButton;

typedef enum PDBenchInputMode {
    PD_PLAYBENCH_INPUT_OVERRIDE = 0,
    PD_PLAYBENCH_INPUT_MERGE = 1,
    PD_PLAYBENCH_INPUT_MANUAL_TRIGGER = 2
} PDBenchInputMode;

typedef struct PDBenchConfig {
    const char* test_name;
    const char* emulator_name;
    const char* rom_name;
    const char* build_label;
    const char* device_label;
    const char* report_path;
    int target_fps;
    int log_to_console;
    int write_report_file;
    PDBenchInputMode input_mode;
} PDBenchConfig;

#ifdef PD_PLAYBENCH_ENABLED

void pd_playbench_init(PlaydateAPI* playdate, const PDBenchConfig* config);
void pd_playbench_shutdown(void);

int pd_playbench_load_script_from_string(const char* script);
int pd_playbench_load_script_from_file(const char* path);
const char* pd_playbench_get_last_error(void);

void pd_playbench_start(void);
void pd_playbench_stop(void);
void pd_playbench_reset(void);
void pd_playbench_restart(void);

int pd_playbench_is_running(void);
int pd_playbench_is_finished(void);

void pd_playbench_update(void);
PDButtons pd_playbench_get_buttons(PDButtons real_buttons);

void pd_playbench_set_input_mode(PDBenchInputMode mode);
void pd_playbench_set_manual_trigger_button(PDButtons button_mask);
void pd_playbench_set_report_path(const char* path);

void pd_playbench_begin_measurement(void);
void pd_playbench_end_measurement(void);
void pd_playbench_report_frame(float frame_time_ms, int skipped_frames);

void pd_playbench_save_report(const char* path);
void pd_playbench_print_report(void);

#else

static inline void pd_playbench_init(PlaydateAPI* playdate, const PDBenchConfig* config) { (void)playdate; (void)config; }
static inline void pd_playbench_shutdown(void) {}

static inline int pd_playbench_load_script_from_string(const char* script) { (void)script; return 0; }
static inline int pd_playbench_load_script_from_file(const char* path) { (void)path; return 0; }
static inline const char* pd_playbench_get_last_error(void) { return "pd-playbench disabled"; }

static inline void pd_playbench_start(void) {}
static inline void pd_playbench_stop(void) {}
static inline void pd_playbench_reset(void) {}
static inline void pd_playbench_restart(void) {}

static inline int pd_playbench_is_running(void) { return 0; }
static inline int pd_playbench_is_finished(void) { return 0; }

static inline void pd_playbench_update(void) {}
static inline PDButtons pd_playbench_get_buttons(PDButtons real_buttons) { return real_buttons; }

static inline void pd_playbench_set_input_mode(PDBenchInputMode mode) { (void)mode; }
static inline void pd_playbench_set_manual_trigger_button(PDButtons button_mask) { (void)button_mask; }
static inline void pd_playbench_set_report_path(const char* path) { (void)path; }

static inline void pd_playbench_begin_measurement(void) {}
static inline void pd_playbench_end_measurement(void) {}
static inline void pd_playbench_report_frame(float frame_time_ms, int skipped_frames) { (void)frame_time_ms; (void)skipped_frames; }

static inline void pd_playbench_save_report(const char* path) { (void)path; }
static inline void pd_playbench_print_report(void) {}

#endif

#ifdef __cplusplus
}
#endif

#endif
