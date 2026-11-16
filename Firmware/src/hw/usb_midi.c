/* USB full-speed MIDI device on the STM32F103 USB peripheral (RM0008 ch. 23).
 *
 * EP0 control (64), EP1 IN bulk (64, MIDI to host), EP2 OUT bulk (64, MIDI from host).
 * D+ pull-up is switched by PA8. */
#include "usb_midi.h"
#include "system.h"
#include "../core/usb_midi_packet.h"

/* ---- identity ----
 * Default: the pid.codes test pair 0x1209/0x0001 under our own name. Build with
 * `make STOCK_USB_ID=1` to present the stock IDs (docs/re/usb.md), so the vendor editor and
 * existing DAW port mappings find the device as they would with the official firmware. */
#ifdef STOCK_USB_ID
#define USB_VID 0x1C75u
#define USB_PID 0x0288u
#define USB_STR_MANUFACTURER "Arturia"
#define USB_STR_PRODUCT "Arturia KeyStep 32"
#else
#define USB_VID 0x1209u
#define USB_PID 0x0001u
#define USB_STR_MANUFACTURER "KeyStep Open"
#define USB_STR_PRODUCT "KeyStep"
#endif
#define USB_PULLUP_ACTIVE 1 /* stock drives PA8 high when the USB device starts (connect) */

/* ---- EPnR bits ---- */
#define EP_CTR_RX   0x8000u
#define EP_DTOG_RX  0x4000u
#define EP_STAT_RX  0x3000u
#define EP_SETUP    0x0800u
#define EP_TYPE     0x0600u
#define EP_KIND     0x0100u
#define EP_CTR_TX   0x0080u
#define EP_DTOG_TX  0x0040u
#define EP_STAT_TX  0x0030u
#define EP_EA       0x000Fu
#define EP_RW_MASK  (EP_CTR_RX | EP_SETUP | EP_TYPE | EP_KIND | EP_CTR_TX | EP_EA)

#define EP_T_BULK    0x0000u
#define EP_T_CONTROL 0x0200u

#define STAT_STALL 1u
#define STAT_NAK   2u
#define STAT_VALID 3u

#define CNTR_CTRM   (1u << 15)
#define CNTR_RESETM (1u << 10)
#define CNTR_FRES   (1u << 0)
#define ISTR_CTR    (1u << 15)
#define ISTR_RESET  (1u << 10)
#define ISTR_DIR    (1u << 4)

/* Packet memory layout (local PMA byte addresses). */
#define PMA_EP0_TX 0x40u
#define PMA_EP0_RX 0x80u
#define PMA_EP1_TX 0xC0u
#define PMA_EP2_RX 0x100u
#define EP0_SIZE   64u
#define EP_MIDI_SIZE 64u
#define RX_COUNT_64 0x8400u /* BL_SIZE=1 (32-byte blocks), NUM_BLOCK=1 -> 64 bytes */

#define PMA16(local) (*(volatile uint32_t *)(USB_PMA_BASE + (local) * 2u))
#define BT_ADDR_TX(ep)  PMA16((ep) * 8u + 0u)
#define BT_COUNT_TX(ep) PMA16((ep) * 8u + 2u)
#define BT_ADDR_RX(ep)  PMA16((ep) * 8u + 4u)
#define BT_COUNT_RX(ep) PMA16((ep) * 8u + 6u)

/* ---- descriptors ---- */
static const uint8_t dev_desc[18] = {
    18, 1, 0x00, 0x02, 0, 0, 0, EP0_SIZE,
    USB_VID & 0xFF, USB_VID >> 8, USB_PID & 0xFF, USB_PID >> 8,
    0x00, 0x01, 1, 2, 3, 1,
};

#define CFG_LEN 101u
#define MS_LEN  65u
static const uint8_t cfg_desc[CFG_LEN] = {
    9, 2, CFG_LEN, 0, 2, 1, 0, 0x80, 50,       /* configuration: 2 interfaces, bus powered 100 mA */
    9, 4, 0, 0, 0, 1, 1, 0, 0,                 /* interface 0: AudioControl */
    9, 0x24, 1, 0x00, 0x01, 9, 0, 1, 1,        /* AC header, 1 streaming interface (1) */
    9, 4, 1, 0, 2, 1, 3, 0, 0,                 /* interface 1: MIDIStreaming, 2 endpoints */
    7, 0x24, 1, 0x00, 0x01, MS_LEN, 0,         /* MS header */
    6, 0x24, 2, 1, 1, 0,                       /* MIDI IN jack, embedded, id 1 */
    6, 0x24, 2, 2, 2, 0,                       /* MIDI IN jack, external, id 2 */
    9, 0x24, 3, 1, 3, 1, 2, 1, 0,              /* MIDI OUT jack, embedded, id 3 <- 2 */
    9, 0x24, 3, 2, 4, 1, 1, 1, 0,              /* MIDI OUT jack, external, id 4 <- 1 */
    9, 5, 0x02, 2, EP_MIDI_SIZE, 0, 0, 0, 0,   /* EP2 OUT bulk */
    5, 0x25, 1, 1, 1,                          /* -> embedded IN jack 1 */
    9, 5, 0x81, 2, EP_MIDI_SIZE, 0, 0, 0, 0,   /* EP1 IN bulk */
    5, 0x25, 1, 1, 3,                          /* <- embedded OUT jack 3 */
};

static const char *const strings[] = {USB_STR_MANUFACTURER, USB_STR_PRODUCT, "0001"};

/* ---- state ---- */
static uint8_t pending_addr;
static uint8_t configured;
static const uint8_t *ctl_ptr;
static uint16_t ctl_left;
static uint8_t ctl_zlp;
static uint8_t str_buf[2 + 2 * 32];

#define TXQ_PKTS 64u
static uint8_t txq[TXQ_PKTS][4];
static volatile uint8_t txq_head, txq_tail;
static volatile uint8_t ep1_busy;

#define RXQ_PKTS 32u
static uint8_t rxq[RXQ_PKTS][4];
static volatile uint8_t rxq_head, rxq_tail;
static volatile uint8_t rx_paused; /* EP2 left in NAK until the queue has room for a full packet */
#define RX_PKTS_PER_TRANSFER (EP_MIDI_SIZE / 4u)

/* ---- EPnR helpers (toggle bits are written as XOR, CTR bits written 1 to keep) ---- */
static void ep_set_stat_tx(unsigned ep, uint32_t stat)
{
    uint32_t v = USB_EPR(ep);
    USB_EPR(ep) = ((v & (EP_RW_MASK | EP_STAT_TX)) ^ (stat << 4)) | EP_CTR_RX | EP_CTR_TX;
}

static void ep_set_stat_rx(unsigned ep, uint32_t stat)
{
    uint32_t v = USB_EPR(ep);
    USB_EPR(ep) = ((v & (EP_RW_MASK | EP_STAT_RX)) ^ (stat << 12)) | EP_CTR_RX | EP_CTR_TX;
}

static void ep_clear_ctr_rx(unsigned ep)
{
    USB_EPR(ep) = (USB_EPR(ep) & EP_RW_MASK & ~EP_CTR_RX) | EP_CTR_TX;
}

static void ep_clear_ctr_tx(unsigned ep)
{
    USB_EPR(ep) = (USB_EPR(ep) & EP_RW_MASK & ~EP_CTR_TX) | EP_CTR_RX;
}

static void ep_init(unsigned ep, uint32_t type)
{
    /* Reset toggles to DATA0 and both stats to disabled, then set type/address. */
    uint32_t v = USB_EPR(ep);
    USB_EPR(ep) = (v & (EP_DTOG_RX | EP_STAT_RX | EP_DTOG_TX | EP_STAT_TX)) | type | ep;
}

static void pma_write(uint32_t local, const uint8_t *src, unsigned n)
{
    for (unsigned i = 0; i < n; i += 2) {
        uint16_t w = src[i];
        if (i + 1 < n)
            w |= (uint16_t)(src[i + 1] << 8);
        PMA16(local + i) = w;
    }
}

static void pma_read(uint32_t local, uint8_t *dst, unsigned n)
{
    for (unsigned i = 0; i < n; i += 2) {
        uint16_t w = (uint16_t)PMA16(local + i);
        dst[i] = (uint8_t)w;
        if (i + 1 < n)
            dst[i + 1] = (uint8_t)(w >> 8);
    }
}

/* ---- control endpoint ---- */
static void ctl_send_next(void)
{
    unsigned n = ctl_left > EP0_SIZE ? EP0_SIZE : ctl_left;
    if (n == 0 && !ctl_zlp) {
        ctl_ptr = 0;
        return;
    }
    if (n < EP0_SIZE)
        ctl_zlp = 0;
    pma_write(PMA_EP0_TX, ctl_ptr, n);
    BT_COUNT_TX(0) = n;
    ctl_ptr += n;
    ctl_left = (uint16_t)(ctl_left - n);
    ep_set_stat_tx(0, STAT_VALID);
}

static void ctl_reply(const uint8_t *data, uint16_t len, uint16_t wlength)
{
    static const uint8_t none[1];
    if (len > wlength)
        len = wlength;
    ctl_ptr = data ? data : none;
    ctl_left = len;
    ctl_zlp = len < wlength && (len % EP0_SIZE) == 0; /* short transfer must end with a short packet */
    if (len == 0)
        ctl_zlp = 1;
    ctl_send_next();
}

static void ctl_stall(void)
{
    ep_set_stat_tx(0, STAT_STALL);
    ep_set_stat_rx(0, STAT_VALID);
}

static const uint8_t *string_desc(unsigned idx, uint16_t *len)
{
    if (idx == 0) {
        static const uint8_t lang[4] = {4, 3, 0x09, 0x04};
        *len = 4;
        return lang;
    }
    if (idx > sizeof strings / sizeof strings[0])
        return 0;
    const char *s = strings[idx - 1];
    unsigned n = 0;
    while (s[n] && n < 32) {
        str_buf[2 + 2 * n] = (uint8_t)s[n];
        str_buf[3 + 2 * n] = 0;
        n++;
    }
    str_buf[0] = (uint8_t)(2 + 2 * n);
    str_buf[1] = 3;
    *len = str_buf[0];
    return str_buf;
}

static void configure_midi_endpoints(void)
{
    ep_init(1, EP_T_BULK);
    ep_init(2, EP_T_BULK);
    ep_set_stat_tx(1, STAT_NAK);
    ep_set_stat_rx(2, STAT_VALID);
    ep1_busy = 0;
}

static void handle_setup(void)
{
    uint8_t s[8];
    pma_read(PMA_EP0_RX, s, 8);
    uint8_t type = s[0], req = s[1];
    uint16_t wvalue = (uint16_t)(s[2] | s[3] << 8), wlength = (uint16_t)(s[6] | s[7] << 8);
    static uint8_t two[2];

    if ((type & 0x60) != 0) { /* class/vendor: nothing supported */
        ctl_stall();
        return;
    }
    switch (req) {
    case 6: { /* GET_DESCRIPTOR */
        uint16_t len = 0;
        const uint8_t *d = 0;
        switch (wvalue >> 8) {
        case 1: d = dev_desc; len = sizeof dev_desc; break;
        case 2: d = cfg_desc; len = CFG_LEN; break;
        case 3: d = string_desc(wvalue & 0xFF, &len); break;
        default: break;
        }
        if (d)
            ctl_reply(d, len, wlength);
        else
            ctl_stall();
        break;
    }
    case 5: /* SET_ADDRESS: applied after the status stage */
        pending_addr = (uint8_t)(wvalue & 0x7F);
        ctl_reply(0, 0, 0);
        break;
    case 9: /* SET_CONFIGURATION */
        configured = (uint8_t)(wvalue & 0xFF);
        if (configured)
            configure_midi_endpoints();
        ctl_reply(0, 0, 0);
        break;
    case 8: /* GET_CONFIGURATION */
        two[0] = configured;
        ctl_reply(two, 1, wlength);
        break;
    case 0: /* GET_STATUS */
        two[0] = two[1] = 0;
        ctl_reply(two, 2, wlength);
        break;
    case 10: /* GET_INTERFACE */
        two[0] = 0;
        ctl_reply(two, 1, wlength);
        break;
    case 1: case 3: case 11: /* CLEAR/SET_FEATURE, SET_INTERFACE */
        ctl_reply(0, 0, 0);
        break;
    default:
        ctl_stall();
        break;
    }
}

static void usb_reset(void)
{
    USB_BTABLE = 0;
    BT_ADDR_TX(0) = PMA_EP0_TX;
    BT_COUNT_TX(0) = 0;
    BT_ADDR_RX(0) = PMA_EP0_RX;
    BT_COUNT_RX(0) = RX_COUNT_64;
    BT_ADDR_TX(1) = PMA_EP1_TX;
    BT_COUNT_TX(1) = 0;
    BT_ADDR_RX(2) = PMA_EP2_RX;
    BT_COUNT_RX(2) = RX_COUNT_64;

    ep_init(0, EP_T_CONTROL);
    ep_set_stat_rx(0, STAT_VALID);
    ep_set_stat_tx(0, STAT_NAK);
    configured = 0;
    pending_addr = 0;
    ep1_busy = 0;
    rx_paused = 0;
    USB_DADDR = 0x80u;
}

static void ep1_kick(void)
{
    if (ep1_busy || !configured || txq_head == txq_tail)
        return;
    unsigned n = 0;
    while (txq_tail != txq_head && n < EP_MIDI_SIZE) {
        pma_write(PMA_EP1_TX + n, txq[txq_tail % TXQ_PKTS], 4);
        txq_tail++;
        n += 4;
    }
    BT_COUNT_TX(1) = n;
    ep1_busy = 1;
    ep_set_stat_tx(1, STAT_VALID);
}

static void handle_ctr(void)
{
    uint32_t istr;
    while ((istr = USB_ISTR) & ISTR_CTR) {
        unsigned ep = istr & 0xFu;
        uint32_t epr = USB_EPR(ep);
        if (ep == 0) {
            if (epr & EP_CTR_TX) {
                ep_clear_ctr_tx(0);
                if (pending_addr) {
                    USB_DADDR = 0x80u | pending_addr;
                    pending_addr = 0;
                }
                if (ctl_ptr)
                    ctl_send_next();
                ep_set_stat_rx(0, STAT_VALID);
            }
            if (epr & EP_CTR_RX) {
                int setup = (epr & EP_SETUP) != 0;
                ep_clear_ctr_rx(0);
                if (setup)
                    handle_setup();
                ep_set_stat_rx(0, STAT_VALID);
            }
        } else if (ep == 1 && (epr & EP_CTR_TX)) {
            ep_clear_ctr_tx(1);
            ep1_busy = 0;
            ep1_kick();
        } else if (ep == 2 && (epr & EP_CTR_RX)) {
            ep_clear_ctr_rx(2);
            unsigned n = BT_COUNT_RX(2) & 0x3FFu;
            uint8_t buf[EP_MIDI_SIZE];
            if (n > EP_MIDI_SIZE)
                n = EP_MIDI_SIZE;
            pma_read(PMA_EP2_RX, buf, n);
            for (unsigned i = 0; i + 4 <= n; i += 4) {
                if ((uint8_t)(rxq_head - rxq_tail) >= RXQ_PKTS)
                    break; /* overflow: drop */
                for (int k = 0; k < 4; k++)
                    rxq[rxq_head % RXQ_PKTS][k] = buf[i + k];
                rxq_head++;
            }
            /* Take the next transfer only when all of it fits, so the host is held off (NAK)
             * instead of losing data. usb_midi_receive() opens the endpoint again. */
            if (RXQ_PKTS - (uint8_t)(rxq_head - rxq_tail) >= RX_PKTS_PER_TRANSFER)
                ep_set_stat_rx(2, STAT_VALID);
            else
                rx_paused = 1;
        } else {
            /* unexpected: clear both flags */
            USB_EPR(ep) = epr & EP_RW_MASK & ~(EP_CTR_RX | EP_CTR_TX);
        }
    }
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    uint32_t istr = USB_ISTR;
    if (istr & ISTR_RESET) {
        USB_ISTR = ~ISTR_RESET & 0xFFFFu;
        usb_reset();
    }
    if (istr & ISTR_CTR)
        handle_ctr();
    USB_ISTR = 0xFFFFu & ~0x7B00u; /* clear SOF/ESOF/SUSP/WKUP/ERR/PMAOVR; keep a fresh RESET */
}

void usb_midi_init(void)
{
    gpio_config(GPIOA, 8, GPIO_OUT_PP);
    gpio_write(GPIOA, 8, !USB_PULLUP_ACTIVE); /* disconnected while setting up */

    RCC->APB1ENR |= RCC_APB1_USB;
    USB_CNTR = CNTR_FRES; /* power up, held in reset */
    for (volatile int i = 0; i < 1000; i++) {
    }
    USB_CNTR = 0;
    USB_ISTR = 0;
    usb_reset();
    USB_CNTR = CNTR_CTRM | CNTR_RESETM;
    nvic_priority(IRQ_USB_LP_CAN_RX0, 2);
    nvic_enable(IRQ_USB_LP_CAN_RX0);

    gpio_write(GPIOA, 8, USB_PULLUP_ACTIVE);
}

int usb_midi_ready(void)
{
    return configured != 0;
}

int usb_midi_send_packet(const uint8_t pkt[4])
{
    if (!configured)
        return 0;
    uint32_t pm = irq_save();
    int ok = (uint8_t)(txq_head - txq_tail) < TXQ_PKTS;
    if (ok) {
        for (int k = 0; k < 4; k++)
            txq[txq_head % TXQ_PKTS][k] = pkt[k];
        txq_head++;
        ep1_kick();
    }
    irq_restore(pm);
    return ok;
}

int usb_midi_send(const uint8_t *msg, uint8_t len)
{
    uint8_t pkt[4];
    return usb_midi_encode(0, msg, len, pkt) && usb_midi_send_packet(pkt);
}

int usb_midi_receive(uint8_t pkt[4])
{
    if (rxq_head == rxq_tail)
        return 0;
    for (int k = 0; k < 4; k++)
        pkt[k] = rxq[rxq_tail % RXQ_PKTS][k];
    rxq_tail++;
    if (rx_paused && RXQ_PKTS - (uint8_t)(rxq_head - rxq_tail) >= RX_PKTS_PER_TRANSFER) {
        uint32_t pm = irq_save();
        if (rx_paused) {
            rx_paused = 0;
            ep_set_stat_rx(2, STAT_VALID);
        }
        irq_restore(pm);
    }
    return 1;
}

unsigned usb_midi_tx_room(void)
{
    return TXQ_PKTS - (uint8_t)(txq_head - txq_tail);
}

unsigned usb_midi_tx_sent(void)
{
    return txq_tail;
}
