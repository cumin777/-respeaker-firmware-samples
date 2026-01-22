#ifndef APP_BUTTON_H
#define APP_BUTTON_H

#include <zephyr/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
	BUTTON_EVENT_NONE = 0,
	BUTTON_EVENT_SHORT_PRESS,
	BUTTON_EVENT_LONG_PRESS,
	BUTTON_EVENT_DOUBLE_PRESS,
} button_event_t;

int button_init(void);

/* Get next button event.
 * Returns 0 on success, -EAGAIN on timeout/no data.
 */
int button_get_event(button_event_t *evt, k_timeout_t timeout);

static inline int button_get_event_no_wait(button_event_t *evt)
{
	return button_get_event(evt, K_NO_WAIT);
}

#ifdef __cplusplus
}
#endif

#endif /* APP_BUTTON_H */
