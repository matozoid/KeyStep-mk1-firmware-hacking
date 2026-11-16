#include <string.h>
#include "test.h"
#include "../src/core/usb_midi_packet.h"

static uint8_t pkts[16][4];
static int n_pkts;

static void on_pkt(void *ctx, const uint8_t p[4])
{
    (void)ctx;
    memcpy(pkts[n_pkts++], p, 4);
}

int main(void)
{
    uint8_t p[4];
    static const uint8_t on[] = {0x93, 60, 100};
    CHECK(usb_midi_encode(0, on, 3, p));
    CHECK(p[0] == 0x09 && p[1] == 0x93 && p[2] == 60 && p[3] == 100);
    static const uint8_t pc[] = {0xC0, 5};
    CHECK(usb_midi_encode(1, pc, 2, p));
    CHECK(p[0] == 0x1C && p[3] == 0);
    static const uint8_t clk[] = {0xF8};
    CHECK(usb_midi_encode(0, clk, 1, p));
    CHECK_EQ(p[0], 0x0F);
    static const uint8_t spp[] = {0xF2, 1, 2};
    CHECK(usb_midi_encode(0, spp, 3, p));
    CHECK_EQ(p[0], 0x03);
    CHECK_EQ(usb_midi_packet_len(p), 3);

    /* SysEx of 6 bytes: start(3) + end-with-3 */
    usb_midi_sysex_enc_t e = {0};
    static const uint8_t sx[] = {0xF0, 1, 2, 3, 4, 0xF7};
    n_pkts = 0;
    for (unsigned i = 0; i < sizeof sx; i++)
        usb_midi_sysex_feed(&e, 0, sx[i], on_pkt, 0);
    CHECK_EQ(n_pkts, 2);
    CHECK_EQ(pkts[0][0], 0x04);
    CHECK_EQ(pkts[1][0], 0x07);
    CHECK_EQ(pkts[1][3], 0xF7);

    /* SysEx of 4 bytes: start(3) + end-with-1 */
    static const uint8_t sx2[] = {0xF0, 1, 2, 0xF7};
    n_pkts = 0;
    for (unsigned i = 0; i < sizeof sx2; i++)
        usb_midi_sysex_feed(&e, 0, sx2[i], on_pkt, 0);
    CHECK_EQ(n_pkts, 2);
    CHECK_EQ(pkts[1][0], 0x05);
    CHECK_EQ(usb_midi_packet_len(pkts[1]), 1);
    return test_report("usb_midi_packet");
}
