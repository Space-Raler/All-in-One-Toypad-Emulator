#include "sky_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void){
    static SkyBase b; sky_base_init(&b);
    uint8_t m[2]={'M',1};
    assert(!b.speaker_on);
    sky_base_handle_command(&b,m,2); assert(b.speaker_on);
    uint8_t r[SKY_PACKET_SIZE]; assert(sky_base_next_packet(&b,r) && r[0]=='M' && r[1]==1 && r[3]==0x19);
    m[1]=0; sky_base_handle_command(&b,m,2); assert(!b.speaker_on);
    assert(sky_base_next_packet(&b,r) && r[0]=='M' && r[1]==0); /* drain its reply */
    m[1]=1; uint8_t m32[32]={'M',1}; sky_base_handle_command(&b,m32,32); assert(b.speaker_on);
    assert(!sky_base_next_packet(&b,r)); /* len 32: still no reply, as before */
    uint8_t R[2]={'R',0}; sky_base_handle_command(&b,R,2); assert(!b.speaker_on);
    puts("core M ok"); return 0;}
