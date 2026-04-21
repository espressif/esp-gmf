#include <stdio.h>
int video_render_compose_monitor_test(void);

int main(void)
{
    int ret = video_render_compose_monitor_test();
    if (ret != 0) {
        fprintf(stderr, "[compose] video compose monitor test failed\n");
        return 1;
    }
    printf("[compose] video compose monitor test passed\n");
    return 0;
}
