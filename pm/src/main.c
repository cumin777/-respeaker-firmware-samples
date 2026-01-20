#include <zephyr/autoconf.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/sys/poweroff.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pm_cycle, LOG_LEVEL_INF);

#define HOLD_SECONDS 10

enum demo_mode {
	DEMO_MODE_ACTIVE_RUN = 0,
	DEMO_MODE_THREAD_SLEEP,
	DEMO_MODE_CPU_IDLE,
	DEMO_MODE_SYS_POWEROFF,
};

static const char *demo_mode_to_str(enum demo_mode mode)
{
	switch (mode) {
	case DEMO_MODE_ACTIVE_RUN:
		return "ACTIVE_RUN(busy)";
	case DEMO_MODE_THREAD_SLEEP:
		return "THREAD_SLEEP(k_msleep)";
	case DEMO_MODE_CPU_IDLE:
		return "CPU_IDLE(k_cpu_idle loop)";
	case DEMO_MODE_SYS_POWEROFF:
		return "SYS_POWEROFF(sys_poweroff)";
	default:
		return "UNKNOWN";
	}
}

static void run_mode(enum demo_mode mode)
{
	const uint64_t t0 = k_uptime_get();

	LOG_INF("Enter -> %s, hold %ds", demo_mode_to_str(mode), HOLD_SECONDS);

	switch (mode) {
	case DEMO_MODE_ACTIVE_RUN: {
		/* Keep CPU active (no sleeping) for HOLD_SECONDS. */
		uint32_t last_print_s = 0;
		while ((k_uptime_get() - t0) < (uint64_t)HOLD_SECONDS * 1000U) {
			uint32_t elapsed_s = (uint32_t)((k_uptime_get() - t0) / 1000U);
			if (elapsed_s != last_print_s) {
				last_print_s = elapsed_s;
				LOG_INF("ACTIVE_RUN: %us/%us", elapsed_s, (uint32_t)HOLD_SECONDS);
			}
			k_busy_wait(1000); /* 1ms busy wait */
		}
		break;
	}
	case DEMO_MODE_THREAD_SLEEP:
		k_msleep(HOLD_SECONDS * 1000);
		break;
	case DEMO_MODE_CPU_IDLE:
		while ((k_uptime_get() - t0) < (uint64_t)HOLD_SECONDS * 1000U) {
			k_cpu_idle();
		}
		break;
	case DEMO_MODE_SYS_POWEROFF:
		LOG_INF("System will power off in %ds...", HOLD_SECONDS);
		k_msleep(HOLD_SECONDS * 1000);
		sys_poweroff();
		__builtin_unreachable();
	default:
		k_msleep(HOLD_SECONDS * 1000);
		break;
	}

	LOG_INF("Exit  <- %s, elapsed=%llums", demo_mode_to_str(mode),
		(unsigned long long)(k_uptime_get() - t0));
}

int main(void)
{
	LOG_INF("PM demo start (nRF5340DK). Each mode runs once, %ds per mode.", HOLD_SECONDS);
	LOG_INF("Order: ACTIVE_RUN -> THREAD_SLEEP -> CPU_IDLE -> SYS_POWEROFF");

	run_mode(DEMO_MODE_ACTIVE_RUN);
	run_mode(DEMO_MODE_THREAD_SLEEP);
	run_mode(DEMO_MODE_CPU_IDLE);
	run_mode(DEMO_MODE_SYS_POWEROFF);

	return 0;
}
