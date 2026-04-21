#include "esp_gmf_oal_mutex.h"

#include <pthread.h>
#include <stdlib.h>

esp_gmf_oal_mutex_t esp_gmf_oal_mutex_create(void)
{
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    pthread_mutex_t *m = (pthread_mutex_t *)calloc(1, sizeof(*m));
    if (!m) {
        pthread_mutexattr_destroy(&attr);
        return NULL;
    }
    pthread_mutex_init(m, &attr);
    pthread_mutexattr_destroy(&attr);
    return (esp_gmf_oal_mutex_t)m;
}

void esp_gmf_oal_mutex_delete(esp_gmf_oal_mutex_t m)
{
    if (!m) {
        return;
    }
    pthread_mutex_destroy((pthread_mutex_t *)m);
    free(m);
}

bool esp_gmf_oal_mutex_lock(esp_gmf_oal_mutex_t m, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!m) {
        return false;
    }
    return pthread_mutex_lock((pthread_mutex_t *)m) == 0;
}

bool esp_gmf_oal_mutex_unlock(esp_gmf_oal_mutex_t m)
{
    if (!m) {
        return false;
    }
    return pthread_mutex_unlock((pthread_mutex_t *)m) == 0;
}
