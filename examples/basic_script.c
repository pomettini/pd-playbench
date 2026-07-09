#include "pd_playbench.h"

#ifdef PD_PLAYBENCH_ENABLED

static const char* basic_benchmark_script =
    "# Basic smoke test\n"
    "wait 120\n"
    "press A 6\n"
    "wait 60\n"
    "hold RIGHT 180\n"
    "hold RIGHT+B 120\n"
    "press A 8\n"
    "wait 60\n"
    "stop\n";

void example_init_playbench(PlaydateAPI* playdate)
{
    PDBenchConfig config = {
        .test_name = "basic_smoke",
        .emulator_name = "example-emulator",
        .rom_name = "example-rom",
        .build_label = "dev",
        .target_fps = 60,
        .log_to_console = 1,
        .write_report_file = 1,
        .report_path = "benchmarks/latest.txt",
        .input_mode = PD_PLAYBENCH_INPUT_OVERRIDE
    };

    pd_playbench_init(playdate, &config);
    pd_playbench_load_script_from_string(basic_benchmark_script);
    pd_playbench_start();
}

PDButtons example_filter_buttons(PDButtons real_buttons)
{
    pd_playbench_update();
    return pd_playbench_get_buttons(real_buttons);
}

void example_report_frame(float frame_time_ms, int skipped_frames)
{
    pd_playbench_report_frame(frame_time_ms, skipped_frames);
}

#endif
