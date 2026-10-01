"""One-off probe: replicate ESPHome's logger/lvgl validate_printf regex and
verify every format: string used in this project matches its arg count.
Delete after use."""

import re

CFMT = re.compile(
    r"""
    (                                   # start of capture group 1
    %                                   # literal "%"
    (?:[-+0 #]{0,5})                    # optional flags
    (?:\d+|\*)?                         # width
    (?:\.(?:\d+|\*))?                   # precision
    (?:hh|h|ll|l|j|z|t|L|w|I|I32|I64)?  # size
    [cCdiouxXeEfgGaAnpsSZ]              # type
    )
    """,
    re.VERBOSE,
)


def check(label, fmt, nargs):
    matches = CFMT.findall(fmt)
    status = "OK " if len(matches) == nargs else "FAIL"
    print(f"{status} {label}: {len(matches)} patterns {matches} vs {nargs} args")


# New heartbeat format (fixed)
check(
    "heartbeat NEW",
    "HEARTBEAT uptime=%d s heap_free=%.0f B heap_min_free=%.0f B frag=%.1f pct "
    "psram_free=%.0f B loop_time=%.0f",
    6,
)
# Old heartbeat format (was broken: %% became phantom "% p")
check(
    "heartbeat OLD",
    "HEARTBEAT uptime=%d s heap_free=%.0f B heap_min_free=%.0f B frag=%.1f%% "
    "psram_free=%.0f B loop_time=%.0f",
    6,
)
# Other formats in the project
check("BOOT", "BOOT reset_reason=%s uptime=%d s heap_free=%.0f B psram_free=%.0f B", 4)
check("touch", "Touch at (%d, %d)", 2)
check("play/pause", "DBG play/pause: ha_connected=%d player_playing=%d", 2)
check("ota label", "завершено %0.1f%%", 1)