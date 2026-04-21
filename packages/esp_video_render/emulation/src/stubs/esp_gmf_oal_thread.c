#include "esp_gmf_oal_thread.h"

#include <pthread.h>
#include <stdlib.h>

typedef struct {
    pthread_t                  tid;
    esp_gmf_oal_thread_func_t  entry;
    void                      *arg;
} thread_wrap_t;

static void *thread_trampoline(void *p)
{
    thread_wrap_t *t = (thread_wrap_t *)p;
    t->entry(t->arg);
    free(t);
    return NULL;
}

esp_gmf_err_t esp_gmf_oal_thread_create(esp_gmf_oal_thread_t *out_handle,
                                        const char *name,
                                        esp_gmf_oal_thread_func_t entry,
                                        void *arg,
                                        uint32_t stack_size,
                                        int prio,
                                        bool pinned,
                                        int core_id)
{
    (void)name;
    (void)prio;
    (void)pinned;
    (void)core_id;

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    if (stack_size > 0) {
        pthread_attr_setstacksize(&attr, (size_t)stack_size);
    }

    thread_wrap_t *t = (thread_wrap_t *)calloc(1, sizeof(*t));
    if (!t) {
        pthread_attr_destroy(&attr);
        return -1;
    }
    t->entry = entry;
    t->arg = arg;
    if (pthread_create(&t->tid, &attr, thread_trampoline, t) != 0) {
        free(t);
        pthread_attr_destroy(&attr);
        return -1;
    }
    pthread_attr_destroy(&attr);

    if (out_handle) {
        *out_handle = (esp_gmf_oal_thread_t)t;
    }
    return ESP_GMF_ERR_OK;
}

void esp_gmf_oal_thread_delete(esp_gmf_oal_thread_t handle)
{
    // In esp_video_render this is called with NULL from within the thread entry.
    // We don't support self-delete; threads are detached instead.
    if (handle) {
        pthread_detach(((thread_wrap_t *)handle)->tid);
        free(handle);
    } else {
        // Self-delete case: thread_trampoline will free the wrapper
        pthread_detach(pthread_self());
    }
}
