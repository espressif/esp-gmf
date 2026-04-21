#include "esp_random.h"

#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

uint32_t esp_random(void)
{
    static bool init = false;
    if (!init) {
        srand((unsigned)time(NULL));
        init = true;
    }
    return (uint32_t)rand();
}
