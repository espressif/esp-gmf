#include "esp_gmf_oal_mem.h"

#include <stdlib.h>

void *esp_gmf_oal_malloc(size_t size)  {  return malloc(size);  }
void *esp_gmf_oal_calloc(size_t n, size_t size)  {  return calloc(n, size);  }
void *esp_gmf_oal_realloc(void *ptr, size_t size)  {  return realloc(ptr, size);  }
void esp_gmf_oal_free(void *ptr)  {  free(ptr);  }

void *esp_gmf_oal_malloc_align(uint8_t align, size_t size)
{
    void *p = NULL;
    if (align <= 1) {
        return malloc(size);
    }
    if (posix_memalign(&p, align, size) != 0) {
        return NULL;
    }
    return p;
}

uint8_t esp_gmf_oal_get_spiram_cache_align(void)
{
    return ESP_GMF_OAL_SPIRAM_CACHE_ALIGN;
}
