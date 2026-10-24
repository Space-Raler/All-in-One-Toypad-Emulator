#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "sky_core.h"
int main(void) {
    SkyBase b; sky_base_init(&b);
    uint8_t fig[SKY_FIGURE_SIZE]; memset(fig, 0, sizeof fig); fig[0] = 7;
    /* replace while the game has not read the queue: remove then load */
    assert(sky_base_load_figure_at(&b, 0, fig) == 0);
    assert(sky_base_remove_figure(&b, 0));
    assert(!sky_base_slot_present_settled(&b, 0));       /* gone right away */
    assert(sky_base_load_figure_at(&b, 0, fig) == 0);
    assert(sky_base_slot_present_settled(&b, 0));
    /* the game now reads the queue one entry per poll: raw presence flickers, settled does not */
    uint8_t pkt[SKY_PACKET_SIZE];
    int flicker = 0;
    for(int i = 0; i < 10; i++) {
        sky_base_next_packet(&b, pkt);
        assert(sky_base_slot_present_settled(&b, 0));
        if(!sky_base_slot_present(&b, 0)) flicker++;
    }
    assert(flicker > 0);                                  /* proves the old check could say "absent" */
    assert(sky_base_slot_present(&b, 0));                 /* and everything ends up present */
    puts("settled ok");
    return 0;
}
