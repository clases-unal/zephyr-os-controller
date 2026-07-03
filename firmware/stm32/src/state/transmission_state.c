#include "transmission_state.h"

static TransmissionState state;
static struct k_mutex transmission_mutex;

void transmission_state_init(void)
{
	k_mutex_init(&transmission_mutex);
	state.esp32_connected = false;
	state.last_telemetry_sent_ms = 0;
	state.resend_pending = false;
}

void transmission_state_get(TransmissionState *out)
{
	k_mutex_lock(&transmission_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&transmission_mutex);
}

void transmission_state_set_connected(bool connected)
{
	k_mutex_lock(&transmission_mutex, K_FOREVER);
	state.esp32_connected = connected;
	k_mutex_unlock(&transmission_mutex);
}

void transmission_state_mark_sent(int64_t uptime_ms)
{
	k_mutex_lock(&transmission_mutex, K_FOREVER);
	state.last_telemetry_sent_ms = uptime_ms;
	k_mutex_unlock(&transmission_mutex);
}

void transmission_state_set_resend_pending(bool pending)
{
	k_mutex_lock(&transmission_mutex, K_FOREVER);
	state.resend_pending = pending;
	k_mutex_unlock(&transmission_mutex);
}
