/**
 * @file config_state.c
 * @brief Implementación de ConfigState protegida por mutex.
 */

#include "config_state.h"

#define DEFAULT_THRESHOLD_LOW      30.0f
#define DEFAULT_THRESHOLD_MEDIUM   45.0f
#define DEFAULT_THRESHOLD_HIGH     60.0f
#define DEFAULT_THRESHOLD_CRITICAL 80.0f

static ConfigState state;
static struct k_mutex config_mutex;

void config_state_init(void)
{
	k_mutex_init(&config_mutex);
	state.threshold_low = DEFAULT_THRESHOLD_LOW;
	state.threshold_medium = DEFAULT_THRESHOLD_MEDIUM;
	state.threshold_high = DEFAULT_THRESHOLD_HIGH;
	state.threshold_critical = DEFAULT_THRESHOLD_CRITICAL;
}

void config_state_get(ConfigState *out)
{
	k_mutex_lock(&config_mutex, K_FOREVER);
	*out = state;
	k_mutex_unlock(&config_mutex);
}

void config_state_set_thresholds(float low, float medium, float high, float critical)
{
	k_mutex_lock(&config_mutex, K_FOREVER);
	state.threshold_low = low;
	state.threshold_medium = medium;
	state.threshold_high = high;
	state.threshold_critical = critical;
	k_mutex_unlock(&config_mutex);
}