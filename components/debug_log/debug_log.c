#include "debug_log.h"

static const char *TAG = "debug_log";

void debug_log_init(void)
{
    DLOG_I(TAG, "Debug logging subsystem initialized");
}
