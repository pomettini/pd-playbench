#include "pd_playbench.h"

#ifdef PD_PLAYBENCH_ENABLED

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef PD_PLAYBENCH_MAX_LINE
#define PD_PLAYBENCH_MAX_LINE 256
#endif

#ifndef PD_PLAYBENCH_FILE_BUFFER_SIZE
#define PD_PLAYBENCH_FILE_BUFFER_SIZE 16384
#endif

#ifndef PD_PLAYBENCH_REPORT_BUFFER_SIZE
#define PD_PLAYBENCH_REPORT_BUFFER_SIZE 2048
#endif

#define PD_PLAYBENCH_DEFAULT_TARGET_FPS 50
#define PD_PLAYBENCH_DEFAULT_REPORT_PATH "pd-playbench-report.txt"

typedef enum PDBenchCommandType {
    PB_CMD_WAIT,
    PB_CMD_INPUT,
    PB_CMD_STOP
} PDBenchCommandType;

typedef struct PDBenchCommand {
    PDBenchCommandType type;
    int buttons;
    int frames;
} PDBenchCommand;

typedef struct PDBenchStats {
    int total_frames;
    int measured_frames;
    int slow_frames;
    int skipped_frames;
    float elapsed_ms;
    float best_frame_ms;
    float worst_frame_ms;
} PDBenchStats;

typedef struct PDBenchState {
    PlaydateAPI* pd;
    PDBenchConfig config;
    PDBenchInputMode input_mode;
    PDButtons manual_trigger_button;
    const char* report_path;

    PDBenchCommand commands[PD_PLAYBENCH_MAX_COMMANDS];
    int command_count;
    int command_index;
    int command_frames_left;
    int need_enter_command;

    int running;
    int finished;
    int armed_for_trigger;
    int pending_auto_stop;
    int measurement_active;
    int current_script_buttons;

    PDBenchStats stats;
    char last_error[128];
    char file_buffer[PD_PLAYBENCH_FILE_BUFFER_SIZE];
} PDBenchState;

static PDBenchState g_pb;

static const char* pb_str_or_empty(const char* value)
{
    return value ? value : "";
}

static void pb_set_error(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(g_pb.last_error, sizeof(g_pb.last_error), format, args);
    va_end(args);
}

static int pb_ascii_ieq(const char* a, const char* b)
{
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return 0;
        }
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static char* pb_trim(char* text)
{
    char* end;

    while (*text && isspace((unsigned char)*text)) {
        text++;
    }

    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';

    return text;
}

static int pb_parse_int(const char* text, int* out_value)
{
    int value = 0;

    if (!text || !*text) {
        return 0;
    }

    while (*text) {
        if (!isdigit((unsigned char)*text)) {
            return 0;
        }
        value = value * 10 + (*text - '0');
        text++;
    }

    *out_value = value;
    return 1;
}

static int pb_seconds_to_frames(float seconds)
{
    int fps = g_pb.config.target_fps > 0 ? g_pb.config.target_fps : PD_PLAYBENCH_DEFAULT_TARGET_FPS;
    int frames;

    if (seconds <= 0.0f) {
        return 0;
    }

    frames = (int)(seconds * (float)fps + 0.5f);
    return frames > 0 ? frames : 1;
}

static int pb_parse_seconds(const char* text, int* out_frames)
{
    float seconds = 0.0f;
    char tail = '\0';

    if (!text || !*text) {
        return 0;
    }

    if (sscanf(text, "%f%c", &seconds, &tail) != 1) {
        return 0;
    }

    *out_frames = pb_seconds_to_frames(seconds);
    return 1;
}

static int pb_parse_button_name(const char* name, int* out_button)
{
    if (pb_ascii_ieq(name, "LEFT")) {
        *out_button = PD_BENCH_BUTTON_LEFT;
    } else if (pb_ascii_ieq(name, "RIGHT")) {
        *out_button = PD_BENCH_BUTTON_RIGHT;
    } else if (pb_ascii_ieq(name, "UP")) {
        *out_button = PD_BENCH_BUTTON_UP;
    } else if (pb_ascii_ieq(name, "DOWN")) {
        *out_button = PD_BENCH_BUTTON_DOWN;
    } else if (pb_ascii_ieq(name, "A")) {
        *out_button = PD_BENCH_BUTTON_A;
    } else if (pb_ascii_ieq(name, "B")) {
        *out_button = PD_BENCH_BUTTON_B;
    } else if (pb_ascii_ieq(name, "MENU")) {
        *out_button = PD_BENCH_BUTTON_MENU;
    } else if (pb_ascii_ieq(name, "NONE")) {
        *out_button = 0;
    } else {
        return 0;
    }

    return 1;
}

static int pb_parse_buttons(const char* text, int* out_buttons)
{
    char buffer[PD_PLAYBENCH_MAX_LINE];
    char* cursor;
    int buttons = 0;

    if (!text || !*text || strlen(text) >= sizeof(buffer)) {
        return 0;
    }

    strcpy(buffer, text);
    cursor = buffer;

    while (*cursor) {
        char* plus = strchr(cursor, '+');
        char* token;
        int button = 0;

        if (plus) {
            *plus = '\0';
        }

        token = pb_trim(cursor);
        if (!pb_parse_button_name(token, &button)) {
            return 0;
        }
        buttons |= button;

        if (!plus) {
            break;
        }
        cursor = plus + 1;
    }

    *out_buttons = buttons;
    return 1;
}

static int pb_add_command(PDBenchCommandType type, int buttons, int frames)
{
    PDBenchCommand* command;

    if (g_pb.command_count >= PD_PLAYBENCH_MAX_COMMANDS) {
        pb_set_error("too many script commands (max %d)", PD_PLAYBENCH_MAX_COMMANDS);
        return 0;
    }

    command = &g_pb.commands[g_pb.command_count++];
    command->type = type;
    command->buttons = buttons;
    command->frames = frames;
    return 1;
}

static int pb_parse_line(char* line, int line_number)
{
    char* comment = strchr(line, '#');
    char* command;
    char* arg1;
    char* arg2;
    char* extra;
    int frames = 0;
    int buttons = 0;

    if (comment) {
        *comment = '\0';
    }

    command = pb_trim(line);
    if (*command == '\0') {
        return 1;
    }

    arg1 = command;
    while (*arg1 && !isspace((unsigned char)*arg1)) {
        arg1++;
    }
    if (*arg1) {
        *arg1++ = '\0';
    }
    arg1 = pb_trim(arg1);

    arg2 = arg1;
    while (*arg2 && !isspace((unsigned char)*arg2)) {
        arg2++;
    }
    if (*arg2) {
        *arg2++ = '\0';
    }
    arg2 = pb_trim(arg2);

    extra = arg2;
    while (*extra && !isspace((unsigned char)*extra)) {
        extra++;
    }
    if (*extra) {
        *extra++ = '\0';
    }
    extra = pb_trim(extra);

    if (pb_ascii_ieq(command, "wait")) {
        if (!pb_parse_int(arg1, &frames) || *arg2) {
            pb_set_error("line %d: expected wait <frames>", line_number);
            return 0;
        }
        return pb_add_command(PB_CMD_WAIT, 0, frames);
    }

    if (pb_ascii_ieq(command, "wait_seconds")) {
        if (!pb_parse_seconds(arg1, &frames) || *arg2) {
            pb_set_error("line %d: expected wait_seconds <seconds>", line_number);
            return 0;
        }
        return pb_add_command(PB_CMD_WAIT, 0, frames);
    }

    if (pb_ascii_ieq(command, "press") || pb_ascii_ieq(command, "hold")) {
        if (!pb_parse_buttons(arg1, &buttons) || !pb_parse_int(arg2, &frames) || *extra) {
            pb_set_error("line %d: expected %s <buttons> <frames>", line_number, command);
            return 0;
        }
        return pb_add_command(PB_CMD_INPUT, buttons, frames);
    }

    if (pb_ascii_ieq(command, "press_seconds") || pb_ascii_ieq(command, "hold_seconds")) {
        if (!pb_parse_buttons(arg1, &buttons) || !pb_parse_seconds(arg2, &frames) || *extra) {
            pb_set_error("line %d: expected %s <buttons> <seconds>", line_number, command);
            return 0;
        }
        return pb_add_command(PB_CMD_INPUT, buttons, frames);
    }

    if (pb_ascii_ieq(command, "release")) {
        if (*arg1) {
            pb_set_error("line %d: expected release", line_number);
            return 0;
        }
        return pb_add_command(PB_CMD_WAIT, 0, 1);
    }

    if (pb_ascii_ieq(command, "stop")) {
        if (*arg1) {
            pb_set_error("line %d: expected stop", line_number);
            return 0;
        }
        return pb_add_command(PB_CMD_STOP, 0, 0);
    }

    pb_set_error("line %d: unknown command '%s'", line_number, command);
    return 0;
}

static void pb_reset_measurement_stats(void)
{
    g_pb.stats.measured_frames = 0;
    g_pb.stats.slow_frames = 0;
    g_pb.stats.skipped_frames = 0;
    g_pb.stats.elapsed_ms = 0.0f;
    g_pb.stats.best_frame_ms = 0.0f;
    g_pb.stats.worst_frame_ms = 0.0f;
}

static void pb_reset_stats(void)
{
    memset(&g_pb.stats, 0, sizeof(g_pb.stats));
}

static void pb_reset_runtime(void)
{
    g_pb.command_index = 0;
    g_pb.command_frames_left = 0;
    g_pb.need_enter_command = 1;
    g_pb.running = 0;
    g_pb.finished = 0;
    g_pb.armed_for_trigger = 0;
    g_pb.pending_auto_stop = 0;
    g_pb.measurement_active = 0;
    g_pb.current_script_buttons = 0;
    pb_reset_stats();
}

static void pb_log_line(const char* line)
{
    if (g_pb.pd && g_pb.pd->system && g_pb.pd->system->logToConsole) {
        g_pb.pd->system->logToConsole("%s", line);
    } else {
        printf("%s\n", line);
    }
}

static void pb_appendf(char* buffer, size_t size, size_t* used, const char* format, ...)
{
    va_list args;
    int written;

    if (*used >= size) {
        return;
    }

    va_start(args, format);
    written = vsnprintf(buffer + *used, size - *used, format, args);
    va_end(args);

    if (written < 0) {
        return;
    }

    if ((size_t)written >= size - *used) {
        *used = size - 1;
    } else {
        *used += (size_t)written;
    }
}

static void pb_build_report(char* buffer, size_t size)
{
    const PDBenchStats* stats = &g_pb.stats;
    const char* device_label = g_pb.config.device_label;
    float avg_frame_ms = 0.0f;
    float estimated_fps = 0.0f;
    float avg_skipped_per_second = 0.0f;
    size_t used = 0;

    if (!device_label) {
#if defined(TARGET_SIMULATOR)
        device_label = "simulator";
#else
        device_label = "device";
#endif
    }

    if (stats->measured_frames > 0) {
        avg_frame_ms = stats->elapsed_ms / (float)stats->measured_frames;
    }
    if (stats->elapsed_ms > 0.0f) {
        estimated_fps = ((float)stats->measured_frames * 1000.0f) / stats->elapsed_ms;
        avg_skipped_per_second = ((float)stats->skipped_frames * 1000.0f) / stats->elapsed_ms;
    }

    buffer[0] = '\0';
    pb_appendf(buffer, size, &used, "pd-playbench report\n");
    pb_appendf(buffer, size, &used, "test=%s\n", pb_str_or_empty(g_pb.config.test_name));
    pb_appendf(buffer, size, &used, "emulator=%s\n", pb_str_or_empty(g_pb.config.emulator_name));
    pb_appendf(buffer, size, &used, "rom=%s\n", pb_str_or_empty(g_pb.config.rom_name));
    pb_appendf(buffer, size, &used, "build=%s\n", pb_str_or_empty(g_pb.config.build_label));
    pb_appendf(buffer, size, &used, "device=%s\n", pb_str_or_empty(device_label));
    pb_appendf(buffer, size, &used, "target_fps=%d\n", g_pb.config.target_fps);
    pb_appendf(buffer, size, &used, "total_frames=%d\n", stats->total_frames);
    pb_appendf(buffer, size, &used, "measured_frames=%d\n", stats->measured_frames);
    pb_appendf(buffer, size, &used, "elapsed_ms=%.2f\n", stats->elapsed_ms);
    pb_appendf(buffer, size, &used, "avg_frame_ms=%.2f\n", avg_frame_ms);
    pb_appendf(buffer, size, &used, "best_frame_ms=%.2f\n", stats->best_frame_ms);
    pb_appendf(buffer, size, &used, "worst_frame_ms=%.2f\n", stats->worst_frame_ms);
    pb_appendf(buffer, size, &used, "slow_frames=%d\n", stats->slow_frames);
    pb_appendf(buffer, size, &used, "skipped_frames=%d\n", stats->skipped_frames);
    pb_appendf(buffer, size, &used, "avg_skipped_per_second=%.2f\n", avg_skipped_per_second);
    pb_appendf(buffer, size, &used, "estimated_fps=%.2f\n", estimated_fps);
}

static PDButtons pb_to_pd_buttons(int bench_buttons)
{
    PDButtons buttons = 0;

    if (bench_buttons & PD_BENCH_BUTTON_LEFT) {
        buttons |= kButtonLeft;
    }
    if (bench_buttons & PD_BENCH_BUTTON_RIGHT) {
        buttons |= kButtonRight;
    }
    if (bench_buttons & PD_BENCH_BUTTON_UP) {
        buttons |= kButtonUp;
    }
    if (bench_buttons & PD_BENCH_BUTTON_DOWN) {
        buttons |= kButtonDown;
    }
    if (bench_buttons & PD_BENCH_BUTTON_A) {
        buttons |= kButtonA;
    }
    if (bench_buttons & PD_BENCH_BUTTON_B) {
        buttons |= kButtonB;
    }

    return buttons;
}

static void pb_finish(int auto_report)
{
    if (!g_pb.running && !g_pb.armed_for_trigger) {
        return;
    }

    g_pb.running = 0;
    g_pb.armed_for_trigger = 0;
    g_pb.pending_auto_stop = 0;
    g_pb.measurement_active = 0;
    g_pb.current_script_buttons = 0;
    g_pb.finished = 1;

    if (auto_report) {
        if (g_pb.config.log_to_console) {
            pd_playbench_print_report();
        }
        if (g_pb.config.write_report_file) {
            pd_playbench_save_report(g_pb.report_path);
        }
    }
}

static void pb_enter_command(void)
{
    while (g_pb.command_index < g_pb.command_count) {
        PDBenchCommand* command = &g_pb.commands[g_pb.command_index];

        if (command->type == PB_CMD_STOP) {
            pb_finish(1);
            return;
        }

        if (command->frames <= 0) {
            g_pb.command_index++;
            continue;
        }

        g_pb.current_script_buttons = command->buttons;
        g_pb.command_frames_left = command->frames;
        g_pb.need_enter_command = 0;
        return;
    }

    pb_finish(1);
}

static void pb_start_now(void)
{
    g_pb.command_index = 0;
    g_pb.command_frames_left = 0;
    g_pb.need_enter_command = 1;
    g_pb.running = 1;
    g_pb.finished = 0;
    g_pb.armed_for_trigger = 0;
    g_pb.pending_auto_stop = 0;
    g_pb.current_script_buttons = 0;
    g_pb.measurement_active = 1;
    pb_reset_stats();
}

void pd_playbench_init(PlaydateAPI* playdate, const PDBenchConfig* config)
{
    memset(&g_pb, 0, sizeof(g_pb));
    g_pb.pd = playdate;
    g_pb.config.target_fps = PD_PLAYBENCH_DEFAULT_TARGET_FPS;
    g_pb.config.log_to_console = 1;
    g_pb.config.input_mode = PD_PLAYBENCH_INPUT_OVERRIDE;
    g_pb.input_mode = PD_PLAYBENCH_INPUT_OVERRIDE;
    g_pb.manual_trigger_button = kButtonA;
    g_pb.report_path = PD_PLAYBENCH_DEFAULT_REPORT_PATH;
    strcpy(g_pb.last_error, "ok");

    if (config) {
        g_pb.config = *config;
        if (g_pb.config.target_fps <= 0) {
            g_pb.config.target_fps = PD_PLAYBENCH_DEFAULT_TARGET_FPS;
        }
        g_pb.input_mode = g_pb.config.input_mode;
        if (g_pb.config.report_path) {
            g_pb.report_path = g_pb.config.report_path;
        }
    }
}

void pd_playbench_shutdown(void)
{
    memset(&g_pb, 0, sizeof(g_pb));
}

int pd_playbench_load_script_from_string(const char* script)
{
    char line[PD_PLAYBENCH_MAX_LINE];
    int line_len = 0;
    int line_number = 1;

    g_pb.command_count = 0;
    pb_reset_runtime();

    if (!script) {
        pb_set_error("script is null");
        return 0;
    }

    while (*script) {
        char ch = *script++;

        if (ch == '\r') {
            continue;
        }

        if (ch == '\n') {
            line[line_len] = '\0';
            if (!pb_parse_line(line, line_number)) {
                g_pb.command_count = 0;
                return 0;
            }
            line_len = 0;
            line_number++;
            continue;
        }

        if (line_len >= PD_PLAYBENCH_MAX_LINE - 1) {
            pb_set_error("line %d: line too long", line_number);
            g_pb.command_count = 0;
            return 0;
        }

        line[line_len++] = ch;
    }

    if (line_len > 0) {
        line[line_len] = '\0';
        if (!pb_parse_line(line, line_number)) {
            g_pb.command_count = 0;
            return 0;
        }
    }

    if (g_pb.command_count == 0) {
        pb_set_error("script has no commands");
        return 0;
    }

    strcpy(g_pb.last_error, "ok");
    return 1;
}

int pd_playbench_load_script_from_file(const char* path)
{
    SDFile* file;
    int bytes_read;

    if (!g_pb.pd || !g_pb.pd->file || !path) {
        pb_set_error("file API unavailable");
        return 0;
    }

    file = g_pb.pd->file->open(path, kFileRead);
    if (!file) {
        pb_set_error("could not open script '%s'", path);
        return 0;
    }

    bytes_read = g_pb.pd->file->read(file, g_pb.file_buffer, sizeof(g_pb.file_buffer) - 1);
    g_pb.pd->file->close(file);

    if (bytes_read < 0) {
        pb_set_error("could not read script '%s'", path);
        return 0;
    }

    g_pb.file_buffer[bytes_read] = '\0';
    return pd_playbench_load_script_from_string(g_pb.file_buffer);
}

const char* pd_playbench_get_last_error(void)
{
    return g_pb.last_error;
}

void pd_playbench_start(void)
{
    if (g_pb.command_count == 0) {
        pb_set_error("cannot start: no script loaded");
        return;
    }

    if (g_pb.input_mode == PD_PLAYBENCH_INPUT_MANUAL_TRIGGER) {
        pb_reset_stats();
        g_pb.command_index = 0;
        g_pb.command_frames_left = 0;
        g_pb.need_enter_command = 1;
        g_pb.running = 0;
        g_pb.finished = 0;
        g_pb.armed_for_trigger = 1;
        g_pb.pending_auto_stop = 0;
        g_pb.measurement_active = 0;
        g_pb.current_script_buttons = 0;
    } else {
        pb_start_now();
    }
}

void pd_playbench_stop(void)
{
    pb_finish(0);
}

void pd_playbench_reset(void)
{
    pb_reset_runtime();
}

void pd_playbench_restart(void)
{
    pd_playbench_stop();
    pd_playbench_start();
}

int pd_playbench_is_running(void)
{
    return g_pb.running;
}

int pd_playbench_is_finished(void)
{
    return g_pb.finished;
}

void pd_playbench_update(void)
{
    if (g_pb.pending_auto_stop) {
        pb_finish(1);
        return;
    }

    if (!g_pb.running) {
        return;
    }

    if (g_pb.need_enter_command) {
        pb_enter_command();
        if (!g_pb.running) {
            return;
        }
    }

    g_pb.stats.total_frames++;

    if (g_pb.command_frames_left > 0) {
        g_pb.command_frames_left--;
    }

    if (g_pb.command_frames_left == 0) {
        g_pb.command_index++;
        g_pb.need_enter_command = 1;
        if (g_pb.command_index >= g_pb.command_count) {
            g_pb.pending_auto_stop = 1;
        }
    }
}

PDButtons pd_playbench_get_buttons(PDButtons real_buttons)
{
    PDButtons script_buttons;

    if (g_pb.armed_for_trigger && (real_buttons & g_pb.manual_trigger_button)) {
        pb_start_now();
    }

    if (!g_pb.running) {
        return real_buttons;
    }

    script_buttons = pb_to_pd_buttons(g_pb.current_script_buttons);

    if (g_pb.input_mode == PD_PLAYBENCH_INPUT_MERGE) {
        return real_buttons | script_buttons;
    }

    return script_buttons;
}

void pd_playbench_set_input_mode(PDBenchInputMode mode)
{
    g_pb.input_mode = mode;
    g_pb.config.input_mode = mode;
}

void pd_playbench_set_manual_trigger_button(PDButtons button_mask)
{
    g_pb.manual_trigger_button = button_mask;
}

void pd_playbench_set_report_path(const char* path)
{
    g_pb.report_path = path ? path : PD_PLAYBENCH_DEFAULT_REPORT_PATH;
}

void pd_playbench_begin_measurement(void)
{
    pb_reset_measurement_stats();
    g_pb.measurement_active = 1;
}

void pd_playbench_end_measurement(void)
{
    g_pb.measurement_active = 0;
}

void pd_playbench_report_frame(float frame_time_ms, int skipped_frames)
{
    float target_ms;

    if (!g_pb.running || !g_pb.measurement_active) {
        return;
    }

    if (frame_time_ms < 0.0f) {
        frame_time_ms = 0.0f;
    }
    if (skipped_frames < 0) {
        skipped_frames = 0;
    }

    g_pb.stats.measured_frames++;
    g_pb.stats.elapsed_ms += frame_time_ms;
    g_pb.stats.skipped_frames += skipped_frames;

    if (g_pb.stats.measured_frames == 1 || frame_time_ms < g_pb.stats.best_frame_ms) {
        g_pb.stats.best_frame_ms = frame_time_ms;
    }
    if (g_pb.stats.measured_frames == 1 || frame_time_ms > g_pb.stats.worst_frame_ms) {
        g_pb.stats.worst_frame_ms = frame_time_ms;
    }

    target_ms = 1000.0f / (float)g_pb.config.target_fps;
    if (frame_time_ms > target_ms) {
        g_pb.stats.slow_frames++;
    }

    if (g_pb.pending_auto_stop) {
        pb_finish(1);
    }
}

void pd_playbench_save_report(const char* path)
{
    char report[PD_PLAYBENCH_REPORT_BUFFER_SIZE];
    const char* save_path = path ? path : g_pb.report_path;
    SDFile* file;

    if (!save_path) {
        save_path = PD_PLAYBENCH_DEFAULT_REPORT_PATH;
    }

    pb_build_report(report, sizeof(report));

    if (!g_pb.pd || !g_pb.pd->file) {
        return;
    }

    file = g_pb.pd->file->open(save_path, kFileWrite);
    if (!file) {
        pb_set_error("could not open report '%s'", save_path);
        return;
    }

    g_pb.pd->file->write(file, report, (unsigned int)strlen(report));
    g_pb.pd->file->close(file);
}

void pd_playbench_print_report(void)
{
    char report[PD_PLAYBENCH_REPORT_BUFFER_SIZE];
    char* line;
    char* next;

    pb_build_report(report, sizeof(report));

    line = report;
    while (line && *line) {
        next = strchr(line, '\n');
        if (next) {
            *next = '\0';
        }
        pb_log_line(line);
        if (!next) {
            break;
        }
        line = next + 1;
    }
}

#endif
