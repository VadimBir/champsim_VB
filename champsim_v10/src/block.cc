#include "block.h"
int PACKET_QUEUE::check_queue(PACKET *packet) {
    // cout << "PACKET: "  << sizeof(*packet) << endl;
    
    if ((head == tail) && occupancy == 0)
        return -1;
    if (head < tail) {
        for (uint16_t i=head; i<tail; i++) {
            if (NAME == "L1D_WQ") {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].full_addr == packet->full_addr && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].full_addr == packet->full_addr) {
#endif

                    return i;
                }
            }
            else {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].address == packet->address && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].address == packet->address) {
#endif
                    return i;
                } 
            }
        }
    }
    else {
        for (uint16_t i=head; i<SIZE; i++) {
            if (NAME == "L1D_WQ") {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].full_addr == packet->full_addr && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].full_addr == packet->full_addr) {
#endif

                    return i;
                }
            }
            else {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].address == packet->address && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].address == packet->address) {
#endif

                    return i;
                }
            }
        }
        for (uint16_t i=0; i<tail; i++) {
            if (NAME == "L1D_WQ") {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].full_addr == packet->full_addr && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].full_addr == packet->full_addr) {
#endif

                    return i;
                }
            }
            else {
                __builtin_prefetch(&entry[i+8], 0, 3);
#ifdef BYPASS_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
                if (entry[i].address == packet->address && (entry[i].bypassed_levels == packet->bypassed_levels)) {
#else
                if (entry[i].address == packet->address) {
#endif

                    return i;
                }
            }
        }
    }

    return -1;
}

void PACKET_QUEUE::add_queue(PACKET *packet) {
#ifdef TRUE_SANITY_CHECK
    if (occupancy && (head == tail))
        assert(0);
#endif
    // Find next free slot starting from tail
    // Prevents overwriting occupied entries in sparse arrays
    uint16_t add_index = tail;
    uint16_t checked = 0;

    while (entry[add_index].address != 0 && checked < SIZE) {
        add_index++;
        if (add_index >= SIZE)
            add_index = 0;
        checked++;
    }
    
#ifdef TRUE_SANITY_CHECK
    if (checked >= SIZE) {
        cerr << "[" << NAME << "] add_queue failed: no free slot despite occupancy=" 
             << occupancy << "/" << SIZE << endl;
        assert(0);
    }
#endif

    entry[add_index].fast_copy_packet(entry[add_index], *packet);

    occupancy++;
    tail = add_index + 1;
    if (tail >= SIZE)
        tail = 0;
}
void PACKET_QUEUE::remove_queue(PACKET *packet) {

#ifdef SANITY_CHECK
    if ((occupancy == 0) && (head == tail))
        assert(0);
#endif
    packet->quickReset();
    occupancy--;
    // Only increment head if removing the head entry
    if (packet == &entry[head]) {
        head++;
        if (head >= SIZE)
            head = 0;
        // if (head == tail)
        //     assert(0&&"SANITY FAIL: PACKET_QUEUE::remove_queue => QUEUE HEAD TAIL EQUAL ");
    }
    // For arbitrary removal, just leave hole - scheduling logic handles it
}