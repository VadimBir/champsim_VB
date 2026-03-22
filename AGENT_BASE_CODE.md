
void CACHE::handle_fill() {
    uint16_t fill_cpu = (MSHR.next_fill_index == MSHR_SIZE) ? NUM_CPUS : MSHR.entry[MSHR.next_fill_index].cpu;
    if (fill_cpu == NUM_CPUS)
        return;
    if (MSHR.next_fill_cycle <= current_core_cycle[fill_cpu]) {
#ifdef TRUE_SANITY_CHECK
        if (MSHR.next_fill_index >= MSHR.SIZE)
            assert(0);
#endif
        uint16_t mshr_index = MSHR.next_fill_index;
        uint32_t set = get_set(MSHR.entry[mshr_index].address), way;
        if (cache_type == IS_LLC) {
            way = llc_find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);
        }
        else
            way = find_victim(fill_cpu, MSHR.entry[mshr_index].instr_id, set, block[set], MSHR.entry[mshr_index].ip, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].type);

        uint8_t  do_fill = 1;
        if (block[set][way].dirty) {
            if (lower_level) {
                if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address)) {
                    do_fill = 0;
                    lower_level->increment_WQ_FULL(block[set][way].address);
                    STALL[MSHR.entry[mshr_index].type]++;
                }
                else {
                    PACKET writeback_packet;
                    writeback_packet.fill_level = fill_level << 1;
                    writeback_packet.cpu = fill_cpu;
                    writeback_packet.address = block[set][way].address;
                    writeback_packet.full_addr = block[set][way].full_addr;
                    writeback_packet.data = block[set][way].data;
                    writeback_packet.instr_id = MSHR.entry[mshr_index].instr_id;
                    writeback_packet.ip = 0; 
                    writeback_packet.type = WRITEBACK;
                    writeback_packet.event_cycle = current_core_cycle[fill_cpu];
                    lower_level->add_wq(&writeback_packet);
                }
            }
#ifdef TRUE_SANITY_CHECK
            else {
                if (cache_type != IS_STLB)
                    assert(0);
            }
#endif
        }
        if (do_fill){
            if (cache_type == IS_L1D)
	      l1d_prefetcher_cache_fill(MSHR.entry[mshr_index].full_addr, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0, block[set][way].address<<LOG2_BLOCK_SIZE,
					MSHR.entry[mshr_index].pf_metadata);
            if  (cache_type == IS_L2C)
	      MSHR.entry[mshr_index].pf_metadata = l2c_prefetcher_cache_fill(MSHR.entry[mshr_index].address<<LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
									     block[set][way].address<<LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
            if (cache_type == IS_LLC){
		        cpu = fill_cpu;
		        MSHR.entry[mshr_index].pf_metadata = llc_prefetcher_cache_fill(MSHR.entry[mshr_index].address<<LOG2_BLOCK_SIZE, set, way, (MSHR.entry[mshr_index].type == PREFETCH) ? 1 : 0,
									       block[set][way].address<<LOG2_BLOCK_SIZE, MSHR.entry[mshr_index].pf_metadata);
                cpu = 0;
	      }
            if (cache_type == IS_LLC) {
                llc_update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);
            }
            else
                update_replacement_state(fill_cpu, set, way, MSHR.entry[mshr_index].full_addr, MSHR.entry[mshr_index].ip, block[set][way].full_addr, MSHR.entry[mshr_index].type, 0);
            sim_miss[fill_cpu][MSHR.entry[mshr_index].type]++;
            sim_access[fill_cpu][MSHR.entry[mshr_index].type]++;
            fill_cache(set, way, &MSHR.entry[mshr_index]);
            if (cache_type == IS_L1D) {
                if (MSHR.entry[mshr_index].type == RFO)
                    block[set][way].dirty = 1;
            }
            if (MSHR.entry[mshr_index].fill_level < fill_level) {
                if (MSHR.entry[mshr_index].instruction)
                    upper_level_icache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
                else 
                    upper_level_dcache[fill_cpu]->return_data(&MSHR.entry[mshr_index]);
            }
            if (cache_type == IS_ITLB) {
                MSHR.entry[mshr_index].instruction_pa = block[set][way].data;
                if (PROCESSED.occupancy < PROCESSED.SIZE){
                    PROCESSED.add_queue(&MSHR.entry[mshr_index]);
                } else {
                    cerr << "ITLB PROCESSED FULL" << endl;
                    assert(0&&"RETURN IS LOST FOREVER!!!! ");
                }
            }
            else if (cache_type == IS_DTLB) {
                MSHR.entry[mshr_index].data_pa = block[set][way].data;
                if (PROCESSED.occupancy < PROCESSED.SIZE){
                    PROCESSED.add_queue(&MSHR.entry[mshr_index]);
                } else {
                    cerr << "DTLB PROCESSED FULL" << endl;
                    assert(0&&"RETURN IS LOST FOREVER!!!! ");
                }
            }
            else if (cache_type == IS_L1I) {
                if (PROCESSED.occupancy < PROCESSED.SIZE){
                    PROCESSED.add_queue(&MSHR.entry[mshr_index]);
                } else {
                    cerr << "L1I PROCESSED FULL" << endl;
                    assert(0&&"RETURN IS LOST FOREVER!!!! ");
                }
            }
            else if ((cache_type == IS_L1D) && (MSHR.entry[mshr_index].type != PREFETCH)) {
                if (PROCESSED.occupancy < PROCESSED.SIZE){
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[MSHR.entry[mshr_index].cpu]) {
                        cout << "[" << NAME << "_FILL_PROCESSED] " << __func__ << " L1D add processed instrID: " <<  MSHR.entry[mshr_index].instr_id;
                        cout << " addr: " <<  hex << MSHR.entry[mshr_index].address << " full_addr: " << MSHR.entry[mshr_index].full_addr << dec;
                        cout << " occupancy: " << (int)MSHR.occupancy << " event cy: " << MSHR.entry[mshr_index].event_cycle << " curr cy: " << current_core_cycle[cpu];
                        cout << " BYP: " << (int)MSHR.entry[mshr_index].bypassed_levels << " type: " << (int)MSHR.entry[mshr_index].type << " fill lvl: " << (int)MSHR.entry[mshr_index].fill_level;
                        cout << endl;
                    });
                    PROCESSED.add_queue(&MSHR.entry[mshr_index]);
                } else {
                    cerr << "L1D PROCESSED FULL" << endl;
                    assert(0&&"RETURN IS LOST FOREVER!!!! ");
                }
            }
#ifdef BYPASS_L1_LOGIC
            else if ((cache_type == IS_L2C) && (MSHR.entry[mshr_index].type == LOAD) && MSHR.entry[mshr_index].bypassed_levels == 1 ) {
                if (PROCESSED.occupancy < PROCESSED.SIZE){
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[MSHR.entry[mshr_index].cpu]) {
                        cout << "[" << NAME << "_FILL_ByP_PROCESSED] " << __func__ << " L2C=>L1D add processed instrID: " <<  MSHR.entry[mshr_index].instr_id;
                        cout << " addr: " <<  hex << MSHR.entry[mshr_index].address << " full_addr: " << MSHR.entry[mshr_index].full_addr << dec;
                        cout << " occupancy: " << (int)MSHR.occupancy << " event cy: " << MSHR.entry[mshr_index].event_cycle << " curr cy: " << current_core_cycle[cpu];
                        cout << " BYP: " << (int)MSHR.entry[mshr_index].bypassed_levels << " type: " << (int)MSHR.entry[mshr_index].type << " fill lvl: " << (int)MSHR.entry[mshr_index].fill_level;
                        cout << endl;
                    });
                    PROCESSED.add_queue(&MSHR.entry[mshr_index]);
                } else {
                    cerr << "L2C PROCESSED FULL" << endl;
                    assert(0&&"RETURN IS LOST FOREVER!!!! ");
                }
            }
#endif
	        if(warmup_complete[fill_cpu]){
		        uint64_t current_miss_latency = (current_core_cycle[fill_cpu] - MSHR.entry[mshr_index].cycle_enqueued);
		        total_miss_latency += current_miss_latency;
	        }
            MSHR.remove_queue(&MSHR.entry[mshr_index]);
            MSHR.num_returned--;
            update_fill_cycle();
        }
    }
}
inline void CACHE::merge_with_prefetch(PACKET &mshr_packet, PACKET &queue_packet ) {
    uint8_t prior_returned = mshr_packet.returned;
    uint64_t prior_event_cycle = mshr_packet.event_cycle;
#ifdef BYPASS_L1_LOGIC
    uint8_t prior_fill = mshr_packet.fill_level;
#endif
#ifdef DEBUG_PRINT
    if (warmup_complete[0])
        cout << "MSHR " << (int)mshr_packet.type << " Q: " << (int)queue_packet.type << endl;
#endif
    mshr_packet = queue_packet;
    mshr_packet.returned = prior_returned;
    mshr_packet.event_cycle = prior_event_cycle;
    mshr_packet.fill_level = prior_fill;
}
void CACHE::handle_writeback() {
    uint32_t writeback_cpu = WQ.entry[WQ.head].cpu;
    if (writeback_cpu == NUM_CPUS)
        return;
    if ((WQ.entry[WQ.head].event_cycle <= current_core_cycle[writeback_cpu]) && (WQ.occupancy > 0)) {
        int index = WQ.head;
        uint32_t set = get_set(WQ.entry[index].address);
        int way = check_hit(&WQ.entry[index]);
        if (way >= 0) { 
            if (cache_type == IS_LLC) {
                llc_update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);
            }
            else
                update_replacement_state(writeback_cpu, set, way, block[set][way].full_addr, WQ.entry[index].ip, 0, WQ.entry[index].type, 1);
            sim_hit[writeback_cpu][WQ.entry[index].type]++;
            sim_access[writeback_cpu][WQ.entry[index].type]++;
            block[set][way].dirty = 1;
            if (cache_type == IS_ITLB)
                WQ.entry[index].instruction_pa = block[set][way].data;
            else if (cache_type == IS_DTLB)
                WQ.entry[index].data_pa = block[set][way].data;
            else if (cache_type == IS_STLB)
                WQ.entry[index].data = block[set][way].data;
            if (WQ.entry[index].fill_level < fill_level) {
                if (WQ.entry[index].instruction)
                    upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
                else 
                    upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
            }
            HIT[WQ.entry[index].type]++;
            ACCESS[WQ.entry[index].type]++;
            WQ.remove_queue(&WQ.entry[index]);
        }
        else { 
            if (cache_type == IS_L1D) { 
                uint8_t miss_handled = 1;
                int mshr_index = check_mshr(&WQ.entry[index]);
                if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE)) { 
                    if(cache_type == IS_LLC) {
                        if (lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address)){
                            miss_handled = 0;
                        } else {
                            add_mshr(&WQ.entry[index]);
                            lower_level->add_rq(&WQ.entry[index]);
                        }
                    } else {
                        if (lower_level &&
                            lower_level->get_occupancy(1, WQ.entry[index].address) == lower_level->get_size(1, WQ.entry[index].address)) {
                            miss_handled = 0;
                        } else {
                            add_mshr(&WQ.entry[index]);
                            lower_level->add_rq(&WQ.entry[index]);
                        }
                    }
                } else {
                    if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE)) { 
                        miss_handled = 0;
                        STALL[WQ.entry[index].type]++;
                    }
                    else if (mshr_index != -1) { 
                        if (WQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level)
                            MSHR.entry[mshr_index].fill_level = WQ.entry[index].fill_level;
                        if (MSHR.entry[mshr_index].type == PREFETCH) {
                            merge_with_prefetch(MSHR.entry[mshr_index], WQ.entry[index]);
                        }
                        MSHR_MERGED[WQ.entry[index].type]++;
                    }
                    else { 
                        cerr << "[" << NAME << "] MSHR errors" << endl;
                        assert(0);
                    }
                }
                if (miss_handled) {
                    MISS[WQ.entry[index].type]++;
                    ACCESS[WQ.entry[index].type]++;
                    WQ.remove_queue(&WQ.entry[index]);
                }
            }
            else {
                uint32_t set = get_set(WQ.entry[index].address), way;
                if (cache_type == IS_LLC) {
                    way = llc_find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);
                }
                else
                    way = find_victim(writeback_cpu, WQ.entry[index].instr_id, set, block[set], WQ.entry[index].ip, WQ.entry[index].full_addr, WQ.entry[index].type);
                uint8_t  do_fill = 1;
                if (block[set][way].dirty) {
                    if (lower_level) {
                        if (lower_level->get_occupancy(2, block[set][way].address) == lower_level->get_size(2, block[set][way].address)) {
                            do_fill = 0;
                            lower_level->increment_WQ_FULL(block[set][way].address);
                            STALL[WQ.entry[index].type]++;
                        }
                        else {
                            PACKET writeback_packet;
                            writeback_packet.fill_level = fill_level << 1;
                            writeback_packet.cpu = writeback_cpu;
                            writeback_packet.address = block[set][way].address;
                            writeback_packet.full_addr = block[set][way].full_addr;
                            writeback_packet.data = block[set][way].data;
                            writeback_packet.instr_id = WQ.entry[index].instr_id;
                            writeback_packet.ip = 0;
                            writeback_packet.type = WRITEBACK;
                            writeback_packet.event_cycle = current_core_cycle[writeback_cpu];
                            lower_level->add_wq(&writeback_packet);
                        }
                    }
#ifdef TRUE_SANITY_CHECK
                    else {
                        if (cache_type != IS_STLB)
                            assert(0);
                    }
#endif
                }
                if (do_fill) {
                    if (cache_type == IS_L1D)
		      l1d_prefetcher_cache_fill(WQ.entry[index].full_addr, set, way, 0, block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
                    else if (cache_type == IS_L2C)
		      WQ.entry[index].pf_metadata = l2c_prefetcher_cache_fill(WQ.entry[index].address<<LOG2_BLOCK_SIZE, set, way, 0,
									      block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
                    if (cache_type == IS_LLC)
		      {
			cpu = writeback_cpu;
			WQ.entry[index].pf_metadata =llc_prefetcher_cache_fill(WQ.entry[index].address<<LOG2_BLOCK_SIZE, set, way, 0,
									       block[set][way].address<<LOG2_BLOCK_SIZE, WQ.entry[index].pf_metadata);
			cpu = 0;
		      }
                    if (cache_type == IS_LLC) {
                        llc_update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);
                    }
                    else
                        update_replacement_state(writeback_cpu, set, way, WQ.entry[index].full_addr, WQ.entry[index].ip, block[set][way].full_addr, WQ.entry[index].type, 0);
                    sim_miss[writeback_cpu][WQ.entry[index].type]++;
                    sim_access[writeback_cpu][WQ.entry[index].type]++;
                    fill_cache(set, way, &WQ.entry[index]);
                    block[set][way].dirty = 1;
                    if (WQ.entry[index].fill_level < fill_level) {
                        if (WQ.entry[index].instruction)
                            upper_level_icache[writeback_cpu]->return_data(&WQ.entry[index]);
                        else 
                            upper_level_dcache[writeback_cpu]->return_data(&WQ.entry[index]);
                    }
                    MISS[WQ.entry[index].type]++;
                    ACCESS[WQ.entry[index].type]++;
                    WQ.remove_queue(&WQ.entry[index]);
                }
            }
        }
    }
}
void CACHE::handle_read() {
    for (uint16_t i=0; i<MAX_READ; i++) {
      uint16_t read_cpu = RQ.entry[RQ.head].cpu;
      if (read_cpu == NUM_CPUS)
      {
        return;
      }
        if ((RQ.entry[RQ.head].event_cycle <= current_core_cycle[read_cpu]) && (RQ.occupancy > 0)) {
            int index = RQ.head;
            uint32_t set = get_set(RQ.entry[index].address);
            int way = check_hit(&RQ.entry[index]);
            if (way >= 0) { 
                if (cache_type == IS_ITLB) {
                    RQ.entry[index].instruction_pa = block[set][way].data;
                    if (PROCESSED.occupancy < PROCESSED.SIZE){
                        char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                        for (size_t i = 0; i < sizeof(PACKET); i += 64) {
                            __builtin_prefetch(dst + i, 1, 3);
                        }
                        PROCESSED.add_queue(&RQ.entry[index]);
                    } else {
                        cerr << "ITLB PROCESSED FULL" << endl;
                        assert(0&&"RETURN IS LOST FOREVER!!!! ");
                    }
                }
                else if (cache_type == IS_DTLB) {
                    RQ.entry[index].data_pa = block[set][way].data;
                    if (PROCESSED.occupancy < PROCESSED.SIZE){
                        char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                        for (size_t i = 0; i < sizeof(PACKET); i += 64) {
                            __builtin_prefetch(dst + i, 1, 3);
                        }
                        PROCESSED.add_queue(&RQ.entry[index]);
                    } else {
                        cerr << "DTLB PROCESSED FULL" << endl;
                        assert(0&&"RETURN IS LOST FOREVER!!!! ");
                    }
                }
                else if (cache_type == IS_STLB)
                    RQ.entry[index].data = block[set][way].data;
                else if (cache_type == IS_L1I) {
                    if (PROCESSED.occupancy < PROCESSED.SIZE){
                        char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                        for (size_t i = 0; i < sizeof(PACKET); i += 64) {
                            __builtin_prefetch(dst + i, 1, 3);
                        }
                        PROCESSED.add_queue(&RQ.entry[index]);
                    } else {
                        cerr << "L1I PROCESSED FULL" << endl;
                        assert(0&&"RETURN IS LOST FOREVER!!!! ");
                    }
                }
                else if ((cache_type == IS_L1D) && (RQ.entry[index].type != PREFETCH)) {
                    if (PROCESSED.occupancy < PROCESSED.SIZE){
                        char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                        for (size_t i = 0; i < sizeof(PACKET); i += 64) {
                            __builtin_prefetch(dst + i, 1, 3);
                        }
                        PROCESSED.add_queue(&RQ.entry[index]);
                        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                        cout << "[" << NAME << "_RQ_PROCESSED] " << __func__ << " instr_id: " << RQ.entry[index].instr_id << " Read Hit ";
                        cout << hex << " read: " << RQ.entry[index].address << dec;
                        cout << " idx: " << MAX_READ;
                        cout << " RQ ByP: " << (int) RQ.entry[index].bypassed_levels << " type: " << (int)RQ.entry[index].type  << endl; });
                    } else {
                        cerr << "L1D PROCESSED FULL" << endl;
                        assert(0&&"RETURN IS LOST FOREVER!!!! ");
                    }
                }
#ifdef  BYPASS_L1_LOGIC
                else if ((cache_type == IS_L2C) && (RQ.entry[index].type == LOAD && RQ.entry[index].bypassed_levels == 1 && !RQ.entry[index].instruction)) {
                    if (PROCESSED.occupancy < PROCESSED.SIZE){
                        char* dst = (char*)&PROCESSED.entry[PROCESSED.tail];
                        for (size_t i = 0; i < sizeof(PACKET); i += 64) {
                            __builtin_prefetch(dst + i, 1, 3);
                        }
                        PROCESSED.add_queue(&RQ.entry[index]);
                        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                        cout << "[" << NAME << "_RQ_ByP_PROCESSED] " << __func__  << " Read Hit" << " instr_id: " << RQ.entry[index].instr_id ;
                        dump_req(RQ.entry[index]);
                        cout << " idx: " << MAX_READ;
                        cout << endl; });
                    } else {
                        cerr << "L2C PROCESSED FULL" << endl;
                        assert(0&&"RETURN IS LOST FOREVER!!!! ");
                    }
                }
#endif
                if (RQ.entry[index].type == LOAD) {
                    if (cache_type == IS_L1D)
                        l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 1, RQ.entry[index].type);
                    else if (cache_type == IS_L2C)
                        l2c_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
                    else if (cache_type == IS_LLC)
                    {
                        cpu = read_cpu;
                        llc_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 1, RQ.entry[index].type, 0);
                        cpu = 0;
                    }
                }
                if (cache_type == IS_LLC) {
                    llc_update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);
                }
                else
                    update_replacement_state(read_cpu, set, way, block[set][way].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type, 1);
                sim_hit[read_cpu][RQ.entry[index].type]++;
                sim_access[read_cpu][RQ.entry[index].type]++;
                if (RQ.entry[index].fill_level < fill_level) {
                    if (RQ.entry[index].instruction)
                        upper_level_icache[read_cpu]->return_data(&RQ.entry[index]);
                    else 
                        upper_level_dcache[read_cpu]->return_data(&RQ.entry[index]);
                }
                if (block[set][way].prefetch) {
                    pf_useful++;
                    block[set][way].prefetch = 0;
                }
                block[set][way].used = 1;
                HIT[RQ.entry[index].type]++;
                ACCESS[RQ.entry[index].type]++;
                if (RQ.entry[index].bypassed_levels != 0)
                    total_ByP_cnt++;
                RQ.remove_queue(&RQ.entry[index]);
                reads_available_this_cycle--;
            } else { 
                uint8_t miss_handled = 1;
                int mshr_index = check_mshr(&RQ.entry[index]);
                #ifdef BYPASS_L1D_OnNewMiss
                    if (cache_type == IS_L1D && RQ.entry[index].type == LOAD && mshr_index == -1 && lower_level->RQ.SIZE > lower_level->RQ.occupancy && shall_L1D_Bypass_OnCacheMissedMSHRcap(cpu, (CACHE*)this, (CACHE*)lower_level, (CACHE*)lower_level->lower_level) && warmup_complete[cpu]) {
                            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                    cout << "[" << NAME << "_L1_ByP] " << __func__;
                                    cout << " ";
                                    dump_req(RQ.entry[index]);
                                    cout << endl;
                                };);
                            RQ.entry[index].bypassed_levels = 1;
                            RQ.entry[index].fill_level = FILL_L2;
                            lower_level->add_rq(&RQ.entry[index]);
                    } else 
                #endif
                if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE)) { 
                    if(cache_type == IS_LLC){
                        if (lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)){
                            miss_handled = 0;
                        } else {
                            add_mshr(&RQ.entry[index]);
                            if(lower_level) {
                                DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                    cout << "[" << NAME << "_MSHR_NEW_MISS] " << __func__;
                                    cout << " and lower request to RQ ";
                                    dump_req(RQ.entry[index]);
                                    cout << endl;
                                };);
                                lower_level->add_rq(&RQ.entry[index]);
                            }
                        }
                    } else {
                        if (lower_level && lower_level->get_occupancy(1, RQ.entry[index].address) == lower_level->get_size(1, RQ.entry[index].address)) {
                            miss_handled = 0;
                        } else {
                            add_mshr(&RQ.entry[index]);
                            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                cout << "[" << NAME << "_MSHR_NEW_MISS] " << __func__;
                                cout << " and lower request to RQ ";
                                dump_req(RQ.entry[index]);
                                cout << endl;
                            };);
                            if (lower_level)
                                lower_level->add_rq(&RQ.entry[index]);
                            else { 
                                if (cache_type == IS_STLB) {
                                    uint64_t pa = va_to_pa(read_cpu, RQ.entry[index].instr_id, RQ.entry[index].full_addr, RQ.entry[index].address);
                                    RQ.entry[index].data = pa >> LOG2_PAGE_SIZE;
                                    RQ.entry[index].event_cycle = current_core_cycle[read_cpu];
                                    return_data(&RQ.entry[index]);
                                }
                            }
                        }
                    }
                } else {
                    if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE)) { 
                        miss_handled = 0;
                        STALL[RQ.entry[index].type]++;
                    }
                    else if (mshr_index != -1) { 
                        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                            cout << "[" << NAME << "_MSHR_MERGING] " << __func__;
                            cout << " RQ ";
                            dump_req(RQ.entry[index]);
                            cout << " MSHR ";
                            dump_req(MSHR.entry[mshr_index]);
                            cout << endl;
                        };);
                        if (RQ.entry[index].type == RFO) {
                            if (RQ.entry[index].bypassed_levels)
                                assert(0&&"RFO BYPASS NOT EXPECTED ... ");
                            if (RQ.entry[index].tlb_access) {
                                uint16_t sq_index = RQ.entry[index].sq_index;
                                MSHR.entry[mshr_index].store_merged = 1;
                                MSHR.entry[mshr_index].sq_index_depend_on_me.insert (sq_index);
				                MSHR.entry[mshr_index].sq_index_depend_on_me.join (RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
                            }
                            if (RQ.entry[index].load_merged) {
                                MSHR.entry[mshr_index].load_merged = 1;
				                MSHR.entry[mshr_index].lq_index_depend_on_me.join (RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
                            }
                        } else {
                            if (RQ.entry[index].instruction) {
                                uint16_t rob_index = RQ.entry[index].rob_index;
                                MSHR.entry[mshr_index].instr_merged = 1;
                                MSHR.entry[mshr_index].rob_index_depend_on_me.insert (rob_index);
                                if (RQ.entry[index].instr_merged) {
				                    MSHR.entry[mshr_index].rob_index_depend_on_me.join (RQ.entry[index].rob_index_depend_on_me, ROB_SIZE);
                                }
                            } else {
                                uint16_t lq_index = RQ.entry[index].lq_index;
                                MSHR.entry[mshr_index].load_merged = 1;
                                MSHR.entry[mshr_index].lq_index_depend_on_me.insert (lq_index);
                                MSHR.entry[mshr_index].lq_index_depend_on_me.join (RQ.entry[index].lq_index_depend_on_me, LQ_SIZE);
                                if (RQ.entry[index].store_merged) {
                                    MSHR.entry[mshr_index].store_merged = 1;
				                    MSHR.entry[mshr_index].sq_index_depend_on_me.join (RQ.entry[index].sq_index_depend_on_me, SQ_SIZE);
                                }
                            }
                            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                cout << "[" << NAME << "_MSHR_POST_MERGE] " << __func__;
                                dump_req(MSHR.entry[mshr_index]);
                                cout <<  endl;
                            };);
                        }
                        if (RQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level) {
                            MSHR.entry[mshr_index].fill_level = RQ.entry[index].fill_level;
                        }
#ifdef BYPASS_L1_LOGIC
                        if (cache_type == IS_L2C) {
                            if (RQ.entry[index].bypassed_levels != MSHR.entry[mshr_index].bypassed_levels) {
                                if (MSHR.entry[mshr_index].type != PREFETCH) {
                                    auto *l1d = (CACHE *) this->upper_level_dcache[cpu];
                                    for (uint16_t m = 0; m < l1d->MSHR_SIZE; m++) {
                                        if (l1d->MSHR.entry[m].address == MSHR.entry[mshr_index].address) {
                                            l1d->MSHR.entry[m].load_merged = 1;
                                            l1d->MSHR.entry[m].lq_index_depend_on_me.insert(
                                                RQ.entry[index].lq_index);
                                            if (RQ.entry[index].load_merged) {
                                                ITERATE_SET(dep, RQ.entry[index].lq_index_depend_on_me, LQ_SIZE) {
                                                    l1d->MSHR.entry[m].lq_index_depend_on_me.insert(dep);
                                                }
                                            }
                                            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[l1d->MSHR.cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                                cout << "[" << NAME << "_MSHR_POST_MERGE] " << __func__;
                                                cout << " before rm l1d->MSHR.entry.lq_index: " << l1d->MSHR.entry[m].lq_index;
                                                dump_req(l1d->MSHR.entry[m]);
                                                cout << endl;
                                            };);
                                            l1d->MSHR.entry[m].lq_index_depend_on_me.remove(
                                                l1d->MSHR.entry[m].lq_index);
                                            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[l1d->MSHR.cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                                cout << "[" << NAME << "_MSHR_POST_MERGE] " << __func__;
                                                cout << " after rm l1d->MSHR.entry.lq_index: " << l1d->MSHR.entry[m].lq_index;
                                                dump_req(l1d->MSHR.entry[m]);
                                                cout << endl;
                                            };);
                                            break;
                                        }
                                    }
                                    RQ.entry[index].bypassed_levels = 0;
                                    MSHR.entry[mshr_index].bypassed_levels = 0;
                                    MSHR.entry[mshr_index].fill_level = 1;
                                } else if (MSHR.entry[mshr_index].fill_level < fill_level) {
                                    RQ.entry[index].bypassed_levels = 0;
                                }
                                DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")){
                                    cout << "["<< NAME << "ByP MISMATCH] " << __func__;
                                     cout << " RQ";
                                     dump_req(RQ.entry[index]);
                                     cout << " MSHR";
                                     dump_req(MSHR.entry[mshr_index]);
                                     cout << endl;});
#ifdef DEBUG_PRINT
#endif
                            } else {
                                DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                                    cout << "["<< NAME << " ByP NOT mismatch] " << __func__;
                                    cout << " RQ";
                                    dump_req(RQ.entry[index]);
                                    cout << " MSHR";
                                    dump_req(MSHR.entry[mshr_index]);
                                    cout << endl;});
                            }
                        }
#endif
                        if (MSHR.entry[mshr_index].type == PREFETCH) {
                            pf_late++;
                            merge_with_prefetch(MSHR.entry[mshr_index], RQ.entry[index]);
                        } else {
                            if (RQ.entry[index].instruction) {
                                MSHR.entry[mshr_index].rob_index_depend_on_me.insert(RQ.entry[index].rob_index);
                                MSHR.entry[mshr_index].instr_merged = 1;
                            } else if (RQ.entry[index].type == LOAD) {
                                MSHR.entry[mshr_index].lq_index_depend_on_me.insert(RQ.entry[index].lq_index);
                                MSHR.entry[mshr_index].load_merged = 1;
                            } else if (RQ.entry[index].type == RFO) {
                                MSHR.entry[mshr_index].sq_index_depend_on_me.insert(RQ.entry[index].sq_index);
                                MSHR.entry[mshr_index].store_merged = 1;
                            }
                        }
                        MSHR_MERGED[RQ.entry[index].type]++;
                    }
                    else { 
                        cerr << "[" << NAME << "] MSHR errors" << endl;
                        assert(0);
                    }
                }
                if (miss_handled) {
		            if (RQ.entry[index].type == LOAD) {
                        if (cache_type == IS_L1D)
                            l1d_prefetcher_operate(RQ.entry[index].full_addr, RQ.entry[index].ip, 0, RQ.entry[index].type);
                        if (cache_type == IS_L2C)
			                l2c_prefetcher_operate(RQ.entry[index].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
                        if (cache_type == IS_LLC){
            			    cpu = read_cpu;
			                llc_prefetcher_operate(RQ.entry[index].address<<LOG2_BLOCK_SIZE, RQ.entry[index].ip, 0, RQ.entry[index].type, 0);
                            cpu = 0;
                        }
                    }
                    MISS[RQ.entry[index].type]++;
                    ACCESS[RQ.entry[index].type]++;
                    RQ.remove_queue(&RQ.entry[index]);
		            reads_available_this_cycle--;
                }
            }
        } else {
            return;
        }
	    if(reads_available_this_cycle == 0){
	        return;
	    }
    }
}
void CACHE::handle_prefetch() {
    for (uint16_t i=0; i<MAX_READ; i++) {
        uint16_t prefetch_cpu = PQ.entry[PQ.head].cpu;
        if (prefetch_cpu == NUM_CPUS){
            return;
        }
        if ((PQ.entry[PQ.head].event_cycle <= current_core_cycle[prefetch_cpu]) && (PQ.occupancy > 0)) {
            int index = PQ.head;
            uint32_t set = get_set(PQ.entry[index].address);
            int way = check_hit(&PQ.entry[index]);
            if (way >= 0) { 
                if (cache_type == IS_LLC) {
                    llc_update_replacement_state(prefetch_cpu, set, way, block[set][way].full_addr, PQ.entry[index].ip, 0, PQ.entry[index].type, 1);
                }
                else
                    update_replacement_state(prefetch_cpu, set, way, block[set][way].full_addr, PQ.entry[index].ip, 0, PQ.entry[index].type, 1);
                sim_hit[prefetch_cpu][PQ.entry[index].type]++;
                sim_access[prefetch_cpu][PQ.entry[index].type]++;
                if(PQ.entry[index].pf_origin_level < fill_level){
                    if (cache_type == IS_L1D)
                        l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 1, PREFETCH);
                    else if (cache_type == IS_L2C)
                        PQ.entry[index].pf_metadata = l2c_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
                    else if (cache_type == IS_LLC){
                        cpu = prefetch_cpu;
                        PQ.entry[index].pf_metadata = llc_prefetcher_operate(block[set][way].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, 1, PREFETCH, PQ.entry[index].pf_metadata);
                        cpu = 0;
                    }
                }
                if (PQ.entry[index].fill_level < fill_level) {
                    if (PQ.entry[index].instruction)
                        upper_level_icache[prefetch_cpu]->return_data(&PQ.entry[index]);
                    else 
                        upper_level_dcache[prefetch_cpu]->return_data(&PQ.entry[index]);
                }
                HIT[PQ.entry[index].type]++;
                ACCESS[PQ.entry[index].type]++;
                PQ.remove_queue(&PQ.entry[index]);
		        reads_available_this_cycle--;
            }
            else { 
                DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[prefetch_cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                cout << "[" << NAME << "_PQ_miss] " << __func__ << (int)this->cache_type;
                cout << " instr_id: " << PQ.entry[index].instr_id << " addr: " << hex << PQ.entry[index].address;
                cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << (int) PQ.entry[index].fill_level;
                cout << " cy: " << PQ.entry[index].event_cycle << endl; });
                uint8_t miss_handled = 1;
                int mshr_index = check_mshr(&PQ.entry[index]);
                if ((mshr_index == -1) && (MSHR.occupancy < MSHR_SIZE)) { 
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[PQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                    cout << "[" << NAME << "_PQ] " <<  __func__ << " want to add instr_id: " << PQ.entry[index].instr_id << " addr: " << hex << PQ.entry[index].address;
                    cout << " full_addr: " << PQ.entry[index].full_addr << dec;
                    cout << " occup: " << lower_level->get_occupancy(3, PQ.entry[index].address) << " SIZE: " << lower_level->get_size(3, PQ.entry[index].address) << endl; });
                    if (lower_level) {
                        if (cache_type == IS_LLC) {
                            if (lower_level->get_occupancy(1, PQ.entry[index].address) == lower_level->get_size(1, PQ.entry[index].address))
                                miss_handled = 0;
                            else {
                                if(PQ.entry[index].pf_origin_level < fill_level){
                                    if (cache_type == IS_LLC){
                                        cpu = prefetch_cpu;
                                        PQ.entry[index].pf_metadata = llc_prefetcher_operate(PQ.entry[index].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
                                        cpu = 0;
                                    }
                                }
                                if (PQ.entry[index].fill_level <= fill_level)
                                    add_mshr(&PQ.entry[index]);
                                lower_level->add_rq(&PQ.entry[index]); 
                            }
                        } else {
                            if (lower_level->get_occupancy(3, PQ.entry[index].address) == lower_level->get_size(3, PQ.entry[index].address))
                                miss_handled = 0;
                            else {
                                if(PQ.entry[index].pf_origin_level < fill_level) {
                                    if (cache_type == IS_L1D)
                                        l1d_prefetcher_operate(PQ.entry[index].full_addr, PQ.entry[index].ip, 0, PREFETCH);
                                    if (cache_type == IS_L2C)
                                        PQ.entry[index].pf_metadata = l2c_prefetcher_operate(PQ.entry[index].address<<LOG2_BLOCK_SIZE, PQ.entry[index].ip, 0, PREFETCH, PQ.entry[index].pf_metadata);
                                }
                                if (PQ.entry[index].fill_level <= fill_level)
                                    add_mshr(&PQ.entry[index]);
                                int success = lower_level->add_pq(&PQ.entry[index]); 
                                if (success == -2) {
                                    assert(0&&" PQ added MSHR && lower lvl add_pq FAILURE!!!");
                                }
			                }
		                }
		            }
                } else {
                    if ((mshr_index == -1) && (MSHR.occupancy == MSHR_SIZE)) { 
                        miss_handled = 0;
                        STALL[PQ.entry[index].type]++;
                    } else if (mshr_index != -1) { 
                        if (PQ.entry[index].fill_level < MSHR.entry[mshr_index].fill_level) {
                            if (MSHR.entry[mshr_index].type == PREFETCH) {
                                MSHR.entry[mshr_index].fill_level = PQ.entry[index].fill_level;
                            }
                    #ifdef BYPASS_L1_LOGIC
                        else if (MSHR.entry[mshr_index].bypassed_levels == 1) {
                            auto *l1d = (CACHE *) this->upper_level_dcache[cpu];
                            if (l1d->probe_mshr(&PQ.entry[index]) != -1) {
                                MSHR.entry[mshr_index].fill_level = PQ.entry[index].fill_level;
                                MSHR.entry[mshr_index].bypassed_levels = 0;
                            }
                        }
                    #endif
                        }
                        MSHR_MERGED[PQ.entry[index].type]++;
                        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[prefetch_cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                        cout << "[" << NAME << "] " << __func__ << " mshr merged";
                        cout << " instr_id: " << PQ.entry[index].instr_id << " prior_id: " << MSHR.entry[mshr_index].instr_id;
                        dump_req(PQ.entry[index]);
                        cout << " MSHR: " ;
                        dump_req(MSHR.entry[mshr_index]);
                        cout << " cy: " << MSHR.entry[mshr_index].event_cycle << endl; });
                    } else { 
                        cerr << "[" << NAME << "] MSHR errors" << endl;
                        assert(0);
                    }
                }
                if (miss_handled) {
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[prefetch_cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                    cout << "[" << NAME << "] " << __func__ << " prefetch miss handled";
                    cout << " instr_id: " << PQ.entry[index].instr_id << " addr: " << hex << PQ.entry[index].address;
                    cout << " full_addr: " << PQ.entry[index].full_addr << dec << " fill_level: " << (int)PQ.entry[index].fill_level;
                    cout << " cy: " << PQ.entry[index].event_cycle << endl; });
                    MISS[PQ.entry[index].type]++;
                    ACCESS[PQ.entry[index].type]++;
                    PQ.remove_queue(&PQ.entry[index]);
		            reads_available_this_cycle--;
                }
            }
        } else{
	        return;
	    }
        if(reads_available_this_cycle == 0) {
            return;
        }
    }
}
void CACHE::operate() {
   if (warmup_complete[cpu]) {
        /* ---- α = total accesses at this level ---- */
        uint64_t alpha = 0;
        for (int t = 0; t < NUM_TYPES; t++)
            alpha += sim_access[cpu][t];
        /* ---- hit_active: requests in H-cycle phase ---- */
        bool hit_active = (RQ.occupancy | WQ.occupancy | PQ.occupancy) > 0;
        /* ---- miss_active: outstanding miss penalty ---- */
#ifdef LPM_STRICT_MISS
        /* Paper-exact: miss-access cycles end at data return.
         * Count only INFLIGHT MSHR entries (returned != COMPLETED).
         * COMPLETED entries are in fill phase, not miss penalty.
         * Cost: O(MSHR_SIZE) per cycle. MSHR_SIZE typically 8-16. */
        bool miss_active = false;
        for (uint16_t i = 0; i < MSHR_SIZE; i++) {
            if (MSHR.entry[i].address && MSHR.entry[i].returned != COMPLETED) {
                miss_active = true;
                break;
            }
        }
#else
        /* Simple: any MSHR occupancy.
         * Includes returned-not-filled → overestimates m by
         * ~LATENCY cycles per fill event. Conservative.            */
        bool miss_active = (MSHR.occupancy > 0);
#endif
        /* ---- bypass detection (L2C only) ---- */
        bool has_byp = false;
#ifdef BYPASS_L1_LOGIC
        if (cache_type == IS_L2C) {
            for (uint16_t i = 0; i < MSHR_SIZE; i++) {
                if (MSHR.entry[i].address && MSHR.entry[i].bypassed_levels > 0) {
                    has_byp = true;
                    break;
                }
            }
        }
#endif
        /* ---- tick + update cached metrics ---- */
        lpm_operate(cpu, cache_type, hit_active, miss_active,
                    alpha, has_byp);
    }
    /* >>> end LPM <<< */
    handle_fill();
    handle_writeback();
    reads_available_this_cycle = MAX_READ;
    handle_read();
    if (PQ.occupancy && (reads_available_this_cycle > 0))
        handle_prefetch();
}
uint32_t CACHE::get_set(const uint64_t address) {
    return (uint32_t) (address & ((1 << lg2(NUM_SET)) - 1));
}
uint32_t CACHE::get_way(const uint64_t address, const uint32_t set) {
    for (uint32_t way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == address))
            return way;
    }
    return NUM_WAY;
}
void CACHE::fill_cache(const uint32_t set, const uint32_t way, PACKET *packet) {
#ifdef TRUE_SANITY_CHECK
    if (cache_type == IS_ITLB) {
        if (packet->data == 0)
            assert(0);
    }
    if (cache_type == IS_DTLB) {
        if (packet->data == 0)
            assert(0);
    }
    if (cache_type == IS_STLB) {
        if (packet->data == 0)
            assert(0);
    }
#endif
    if (block[set][way].prefetch && (block[set][way].used == 0))
        pf_useless++;
    if (block[set][way].valid == 0)
        block[set][way].valid = 1;
    block[set][way].dirty = 0;
    block[set][way].prefetch = (packet->type == PREFETCH) ? 1 : 0;
    block[set][way].used = 0;
    if (block[set][way].prefetch)
        pf_fill++;
    block[set][way].tag = packet->address;
    block[set][way].address = packet->address;
    block[set][way].full_addr = packet->full_addr;
    block[set][way].data = packet->data;
    block[set][way].cpu = packet->cpu;
    block[set][way].instr_id = packet->instr_id;
}
int CACHE::check_hit(PACKET *packet)
{
    uint32_t set = get_set(packet->address);
    int match_way = -1;
    if (FORCE_ALL_HITS) { 
        block[set][0].valid = 1;
        block[set][0].tag = packet->address;
        return 0; 
    }
#ifdef TRUE_SANITY_CHECK
    if (NUM_SET < set) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set idx: " << set << " NUM_SET: " << NUM_SET;
        cerr << " addr: " << hex << packet->address << " full_addr: " << packet->full_addr << dec;
        cerr << " event: " << packet->event_cycle << endl;
        assert(0);
    }
#endif
    for (uint32_t way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == packet->address)) {
            match_way = way;
            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "] " << __func__ << " instr_id: " << packet->instr_id << " type: " << +packet->type << hex << " addr: " << packet->address;
            cout << " full_addr: " << packet->full_addr << " tag: " << block[set][way].tag << " data: " << block[set][way].data << dec;
            cout << " set: " << set << " way: " << way << " lru: " << block[set][way].lru;
            cout << " event: " << packet->event_cycle << " cy: " << current_core_cycle[cpu] << endl; });
            break;
        }
    }
    return match_way;
}
int CACHE::invalidate_entry(uint64_t inval_addr) {
    uint32_t set = get_set(inval_addr);
    int match_way = -1;
#ifdef TRUE_SANITY_CHECK
    if (NUM_SET < set) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " invalid set idx: " << set << " NUM_SET: " << NUM_SET;
        cerr << " inval_addr: " << hex << inval_addr << dec << endl;
        assert(0);
    }
#endif
    for (uint32_t way=0; way<NUM_WAY; way++) {
        if (block[set][way].valid && (block[set][way].tag == inval_addr)) {
            block[set][way].valid = 0;
            match_way = way;
            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "] " << __func__ << " inval_addr: " << hex << inval_addr;
            cout << " tag: " << block[set][way].tag << " data: " << block[set][way].data << dec;
            cout << " set: " << set << " way: " << way << " lru: " << block[set][way].lru << " cy: " << current_core_cycle[cpu] << endl; });
            break;
        }
    }
    return match_way;
}
int CACHE::add_rq(PACKET *packet) {
    int wq_index = WQ.check_queue(packet);
    if (wq_index != -1) {
        if (WQ.entry[wq_index].bypassed_levels == 1 && packet->bypassed_levels == 0)
            assert(0);
        if (packet->fill_level < fill_level) {
            packet->data = WQ.entry[wq_index].data;
            if (packet->instruction)
                upper_level_icache[packet->cpu]->return_data(packet);
            else 
                upper_level_dcache[packet->cpu]->return_data(packet);
        }
#ifdef TRUE_SANITY_CHECK
        if (cache_type == IS_ITLB)
            assert(0);
        else if (cache_type == IS_DTLB)
            assert(0);
        else if (cache_type == IS_L1I)
            assert(0);
#endif
        if ((cache_type == IS_L1D) && (packet->type != PREFETCH)) {
            if (PROCESSED.occupancy < PROCESSED.SIZE)
                PROCESSED.add_queue(packet);
            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "_RQWQ_PROCESSED] " << __func__ << " instr_id: " << packet->instr_id << " found recent writebacks";
            cout << hex << " read: " << packet->address << " writeback: " << WQ.entry[wq_index].address << dec;
            cout << " idx: " << MAX_READ;
            cout << " WQ ByP: " << (int) WQ.entry[wq_index].bypassed_levels << " type: " << (int) WQ.entry[wq_index].type << " Pack ByP: " << (int) packet->bypassed_levels << " type: " << packet->type  << endl; });
        }
#ifdef BYPASS_SANITY_CHECK
#endif
#ifdef BYPASS_L1_LOGIC
        if ((cache_type == IS_L2C) && (packet->type == LOAD) && packet->bypassed_levels == 1 && !packet->instruction) {
            if (PROCESSED.occupancy < PROCESSED.SIZE) {
                PROCESSED.add_queue(packet);
            }
            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "_RQWQ_ByP_PROCESSED] " << __func__ << " instr_id: " << packet->instr_id << " found recent writebacks";
            cout << hex << " read: " << packet->address << " writeback: " << WQ.entry[wq_index].address << dec;
            cout << " idx: " << MAX_READ;
            cout << " WQ ByP: " << (int) WQ.entry[wq_index].bypassed_levels << " type: " << (int) WQ.entry[wq_index].type << " Pack ByP: " << (int) packet->bypassed_levels << " type: " << packet->type  << endl; });
        }
#endif
        HIT[packet->type]++;
        ACCESS[packet->type]++;
        WQ.FORWARD++;
        RQ.ACCESS++;
        return -1;
    }
    int index = RQ.check_queue(packet);
#ifdef DEBUG_PRINT
    if (warmup_complete[cpu]) {
        if (RQ.entry[index].bypassed_levels == 1 && packet->bypassed_levels == 0) {
            cout << "[" << NAME << "] " << __func__ << " RQ instrID: " << RQ.entry[index].instr_id << " addr: " << RQ.entry[index].address << " ByP: " << (int)RQ.entry[index].bypassed_levels << " fill: "  << (int)RQ.entry[index].fill_level;
            cout << " newIN pak: instrID: " << packet->instr_id << " addr: " << packet->address << " ByP: " << (int)packet->bypassed_levels << " fill: "  << (int)packet->fill_level;
            cout << endl;
        }
    }
#endif
    if (index != -1) {
        if (packet->instruction) {
            uint16_t rob_index = packet->rob_index;
            RQ.entry[index].rob_index_depend_on_me.insert (rob_index);
            RQ.entry[index].instr_merged = 1;
        } else {
            if (packet->type == RFO) {
                uint16_t sq_index = packet->sq_index;
                RQ.entry[index].sq_index_depend_on_me.insert (sq_index);
                RQ.entry[index].sq_index_depend_on_me.join(packet->sq_index_depend_on_me, SQ_SIZE);  
                RQ.entry[index].store_merged = 1;
#ifdef BYPASS_L1_LOGIC
                if (cache_type == IS_L2C) {
                    if (RQ.entry[index].bypassed_levels != packet->bypassed_levels) {
                        RQ.entry[index].bypassed_levels = 0;
                        if (packet->fill_level < RQ.entry[index].fill_level)
                            RQ.entry[index].fill_level = packet->fill_level;
                    }
                }
#endif
                DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                cout << "[RFO_MERGED] " << __func__ << " cpu: " << (int) packet->cpu;
                dump_req(packet);
                dump_req(RQ.entry[index]);
                cout <<  endl;});
            } 
            else {
                uint16_t lq_index = packet->lq_index;
                RQ.entry[index].lq_index_depend_on_me.insert(lq_index);
                RQ.entry[index].lq_index_depend_on_me.join(packet->lq_index_depend_on_me, LQ_SIZE);  
                RQ.entry[index].load_merged = 1;
#ifdef BYPASS_L1_LOGIC
                if (cache_type == IS_L2C) {
                    if (RQ.entry[index].bypassed_levels != packet->bypassed_levels) {
                        RQ.entry[index].bypassed_levels = 0;
                        packet->bypassed_levels = 0;
                        if (packet->fill_level < RQ.entry[index].fill_level)
                            RQ.entry[index].fill_level = packet->fill_level;
                    }
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
                    cout << "[ByP_DATA_MERGED] " << __func__ << " cpu: " << (int) packet->cpu;
                    dump_req(packet);
                    dump_req(RQ.entry[index]);
                    cout <<  endl;});
                } else {
#endif
                    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu]) {
                        cout << "[NOT L2] " << __func__ << " cpu: " << (int) packet->cpu;
                        cout << " new Packet";
                        dump_req(packet);
                        cout << " RQ";
                        dump_req(RQ.entry[index]);
                        cout <<  endl;});
                }
            }
        }
        RQ.MERGED++;
        RQ.ACCESS++;
        return index; 
    }
    if (RQ.occupancy == RQ_SIZE) {
        RQ.FULL++;
        return -2; 
    }
    index = RQ.tail;
#ifdef TRUE_SANITY_CHECK
    if (RQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty idx: " << index;
        cerr << " addr: " << hex << RQ.entry[index].address;
        cerr << " full_addr: " << RQ.entry[index].full_addr << dec << endl;
        assert(0);
    }
#endif
    RQ.entry[index]= std::move(*packet);
    if (RQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
        RQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        RQ.entry[index].event_cycle += LATENCY;
    RQ.occupancy++;
    RQ.tail++;
    if (RQ.tail >= RQ.SIZE)
        RQ.tail = 0;
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[RQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
    cout << "[" << NAME << "_RQ] " <<  __func__;
    cout << " packet.add=>RQ";
    dump_req(packet);
    cout << " instr?: " << (int)packet->instruction << " rq_index " << index;
    cout << endl; });
#ifdef TRUE_SANITY_CHECK
    if (packet->address == 0)
        assert(0);
#endif
    RQ.TO_CACHE++;
    RQ.ACCESS++;
    return -1;
}
int CACHE::add_wq(PACKET *packet) {
    int index = WQ.check_queue(packet);
    if (index != -1) {
        WQ.MERGED++;
        WQ.ACCESS++;
        return index; 
    }
#ifdef TRUE_SANITY_CHECK
    if (WQ.occupancy >= WQ.SIZE)
        assert(0);
#endif
    index = WQ.tail;
#ifdef TRUE_SANITY_CHECK
    if (WQ.entry[index].address != 0 || WQ.entry[index].bypassed_levels == 1) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty idx: " << index;
        cerr << " addr: " << hex << WQ.entry[index].address;
        cerr << " full_addr: " << WQ.entry[index].full_addr << dec << endl;
        assert(0);
    }
#endif
    WQ.entry[index]= std::move(*packet);
    if (WQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
        WQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        WQ.entry[index].event_cycle += LATENCY;
    WQ.occupancy++;
    WQ.tail++;
    if (WQ.tail >= WQ.SIZE)
        WQ.tail = 0;
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[WQ.entry[index].cpu]) {
    cout << "[" << NAME << "_WQ] " <<  __func__ << " instr_id: " << WQ.entry[index].instr_id << " addr: " << hex << WQ.entry[index].address;
    cout << " full_addr: " << WQ.entry[index].full_addr << dec;
    cout << " head: " << WQ.head << " tail: " << WQ.tail << " occup: " << WQ.occupancy;
    cout << " data: " << hex << WQ.entry[index].data << dec;
    cout << " event: " << WQ.entry[index].event_cycle << " curr: " << current_core_cycle[WQ.entry[index].cpu] << endl; });
    WQ.TO_CACHE++;
    WQ.ACCESS++;
    return -1;
}
int CACHE::prefetch_line(uint64_t ip, uint64_t base_addr, uint64_t pf_addr, int pf_fill_level, uint32_t prefetch_metadata) {
    pf_requested++;
    if (PQ.occupancy < PQ.SIZE) {
        if ((base_addr>>LOG2_PAGE_SIZE) == (pf_addr>>LOG2_PAGE_SIZE)) {
            PACKET pf_packet;
            pf_packet.fill_level = pf_fill_level;
            pf_packet.pf_origin_level = fill_level;
            pf_packet.pf_metadata = prefetch_metadata;
            pf_packet.cpu = cpu;
            pf_packet.address = pf_addr >> LOG2_BLOCK_SIZE;
            pf_packet.full_addr = pf_addr;
            pf_packet.ip = ip;
            pf_packet.type = PREFETCH;
            pf_packet.event_cycle = current_core_cycle[cpu];
            add_pq(&pf_packet);
            pf_issued++;
            return 1;
        }
    }
    return 0;
}
int CACHE::add_pq(PACKET *packet) {
    int wq_index = WQ.check_queue(packet);
    if (wq_index != -1) {
        if (packet->fill_level < fill_level) {
            packet->data = WQ.entry[wq_index].data;
            if (packet->instruction)
                upper_level_icache[packet->cpu]->return_data(packet);
            else 
                upper_level_dcache[packet->cpu]->return_data(packet);
        }
        HIT[packet->type]++;
        ACCESS[packet->type]++;
        WQ.FORWARD++;
        PQ.ACCESS++;
        return -1;
    }
    int index = PQ.check_queue(packet);
    if (index != -1) {
        if (packet->fill_level < PQ.entry[index].fill_level)
            PQ.entry[index].fill_level = packet->fill_level;
        PQ.MERGED++;
        PQ.ACCESS++;
        return index; 
    }
    if (PQ.occupancy == PQ_SIZE) {
        PQ.FULL++;
        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
        cout << "[" << NAME << "] cannot process add_pq since it is full" << endl; });
        return -2; 
    }
    index = PQ.tail;
#ifdef TRUE_SANITY_CHECK
    if (PQ.entry[index].address != 0) {
        cerr << "[" << NAME << "_ERROR] " << __func__ << " is not empty idx: " << index;
        cerr << " addr: " << hex << PQ.entry[index].address;
        cerr << " full_addr: " << PQ.entry[index].full_addr << dec << endl;
        assert(0);
    }
#endif
    PQ.entry[index]= std::move(*packet);
    if (PQ.entry[index].event_cycle < current_core_cycle[packet->cpu])
        PQ.entry[index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        PQ.entry[index].event_cycle += LATENCY;
    PQ.occupancy++;
    PQ.tail++;
    if (PQ.tail >= PQ.SIZE)
        PQ.tail = 0;
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[PQ.entry[index].cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
    cout << "[" << NAME << "_PQ] " <<  __func__;
    dump_req(packet);
    cout << " event: " << PQ.entry[index].event_cycle << " curr: " << current_core_cycle[PQ.entry[index].cpu] << endl; });
#ifdef TRUE_SANITY_CHECK
    if (packet->address == 0 && !FORCE_ALL_HITS)
        assert(0);
#endif
    PQ.TO_CACHE++;
    PQ.ACCESS++;
    return -1;
}
void CACHE::return_data(PACKET *packet) {
    int mshr_index = check_mshr(packet);
#ifdef TRUE_SANITY_CHECK
    if (mshr_index == -1) {
        cerr << "[" << NAME << "_MSHR] " << __func__ << " instr_id: " << packet->instr_id << " cannot find a matching entry!";
        cerr << " full_addr: " << hex << packet->full_addr;
        cerr << " addr: " << packet->address << dec;
        cerr << " event: " << packet->event_cycle << " curr: " << current_core_cycle[packet->cpu] << endl;
        assert(0);
    }
#endif
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu]) {
    cout << "[" << NAME << "MSHR_ret_before] " <<  __func__;
    dump_req(packet);
    cout << " MSHR: ";
    dump_req(MSHR.entry[mshr_index]);
    cout << endl; });
    MSHR.num_returned++;
    MSHR.entry[mshr_index].returned = COMPLETED;
    MSHR.entry[mshr_index].data = packet->data;
    MSHR.entry[mshr_index].pf_metadata = packet->pf_metadata;
    if (MSHR.entry[mshr_index].event_cycle < current_core_cycle[packet->cpu])
        MSHR.entry[mshr_index].event_cycle = current_core_cycle[packet->cpu] + LATENCY;
    else
        MSHR.entry[mshr_index].event_cycle += LATENCY;
#ifdef BYPASS_L1_LOGIC
    if (cache_type == IS_L1D && packet->type == LOAD && !packet->instruction
        && MSHR.entry[mshr_index].type == PREFETCH) {
        MSHR.entry[mshr_index].type      = packet->type;
        MSHR.entry[mshr_index].instr_id   = packet->instr_id;
        MSHR.entry[mshr_index].lq_index   = packet->lq_index;
        MSHR.entry[mshr_index].rob_index  = packet->rob_index;
        MSHR.entry[mshr_index].full_addr  = packet->full_addr;
        MSHR.entry[mshr_index].ip         = packet->ip;
        if (packet->load_merged) {
            MSHR.entry[mshr_index].load_merged = 1;
            MSHR.entry[mshr_index].lq_index_depend_on_me.join(
                packet->lq_index_depend_on_me, LQ_SIZE);
        }
#ifdef DEBUG_PRINT
        cout << "MSHR.entry[mshr_index].lq_index" << MSHR.entry[mshr_index].lq_index << endl;
#endif
        MSHR.entry[mshr_index].lq_index_depend_on_me.remove(
            MSHR.entry[mshr_index].lq_index);
        }
    if (cache_type == IS_L1D && packet->type == LOAD && !packet->instruction && MSHR.entry[mshr_index].type == LOAD) {
        MSHR.entry[mshr_index].lq_index_depend_on_me.join(packet->lq_index_depend_on_me, LQ_SIZE);
        if (MSHR.entry[mshr_index].instr_id != packet->instr_id) {
            MSHR.entry[mshr_index].lq_index_depend_on_me.insert(packet->lq_index);
        }
    }
    if (cache_type == IS_L1D && packet->type == LOAD && !packet->instruction && MSHR.entry[mshr_index].type == RFO) {
        MSHR.entry[mshr_index].load_merged = 1;
        MSHR.entry[mshr_index].lq_index_depend_on_me.insert(packet->lq_index);
        if (packet->load_merged) {
            MSHR.entry[mshr_index].lq_index_depend_on_me.join(
                packet->lq_index_depend_on_me, LQ_SIZE);
        }
        if (packet->store_merged) {
            MSHR.entry[mshr_index].store_merged = 1;
            MSHR.entry[mshr_index].sq_index_depend_on_me.join(
                packet->sq_index_depend_on_me, SQ_SIZE);
        }
    }
    if (cache_type == IS_L1D && packet->type == RFO && MSHR.entry[mshr_index].type == RFO) {
        if (packet->load_merged) {
            MSHR.entry[mshr_index].load_merged = 1;
            MSHR.entry[mshr_index].lq_index_depend_on_me.join(
                packet->lq_index_depend_on_me, LQ_SIZE);
        }
        if (packet->store_merged) {
            MSHR.entry[mshr_index].store_merged = 1;
            MSHR.entry[mshr_index].sq_index_depend_on_me.join(
                packet->sq_index_depend_on_me, SQ_SIZE);
        }
        }
#endif
    update_fill_cycle();
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu]) {
    cout << "[" << NAME << "_MSHR_ret_after] " <<  __func__;
    cout << " MSHR";
    dump_req(MSHR.entry[mshr_index]);
    cout << endl; });
}
void CACHE::update_fill_cycle() {
    uint64_t min_cycle = UINT64_MAX;
    uint16_t min_index = MSHR.SIZE;
    for (uint16_t i=0; i<MSHR.SIZE; i++) {
        if ((MSHR.entry[i].returned == COMPLETED) && (MSHR.entry[i].event_cycle < min_cycle)) {
            min_cycle = MSHR.entry[i].event_cycle;
            min_index = i;
        }
    }
    MSHR.next_fill_cycle = min_cycle;
    MSHR.next_fill_index = min_index;
    if (min_index < MSHR.SIZE) {
    }
}
int CACHE::probe_mshr(PACKET *packet) {
    for (uint16_t index = 0; index < MSHR_SIZE; index++) {
        if (MSHR.entry[index].address == packet->address)
            return index;
    }
    return -1;
}
int CACHE::check_mshr(PACKET *packet) {
    for (uint16_t index=0; index<MSHR_SIZE; index++) {
#ifdef BYPASS_L1_LOGIC_EQUIVALENCY_ON_ADDR_AND_BYPASS
        if (MSHR.entry[index].address == packet->address && MSHR.entry[index].bypassed_levels == packet->bypassed_levels) {
#else
        if (MSHR.entry[index].address == packet->address) { 
#endif
#ifdef BYPASS_L1_LOGIC
            if (MSHR.entry[index].bypassed_levels == 1 && packet->bypassed_levels == 0 && packet->type == LOAD) {
                MSHR.entry[index].bypassed_levels = 0;
                MSHR.entry[index].fill_level = packet->fill_level;
            }
#endif
        DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "_MSHR] " << __func__ << " EQUIVALENT new entry";
            dump_req(packet);
            cout << " MSHR";
            dump_req(MSHR.entry[index]);
            cout << endl; });
            return index;
        }
    }
    DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
    cout << "[" << NAME << "_MSHR] " << __func__ << " new addr: " << hex << packet->address;
        cout << " PACKET NOT IN MSHR";
        dump_req(packet);
        cout << endl; });
    return -1;
}
inline void CACHE::add_mshr(PACKET *packet) {
    uint16_t index = 0;
    packet->cycle_enqueued = current_core_cycle[packet->cpu];
    for (index=0; index<MSHR_SIZE; index++) {
        if (MSHR.entry[index].address == 0) {
            MSHR.entry[index]= std::move(*packet);
            MSHR.entry[index].returned = INFLIGHT;
            MSHR.occupancy++;
            DP( if ((current_core_cycle[cpu] > 63700) || warmup_complete[packet->cpu] && (NAME == "L1D" || NAME == "L2C" || NAME == "LLC")) {
            cout << "[" << NAME << "_MSHR] " << __func__;
            dump_req(packet);
            cout << " MSHR";
            dump_req(MSHR.entry[index]);
            cout << " idx: " << index << " occup: " << MSHR.occupancy << endl; });
            break;
        }
    }
#ifdef TRUE_SANITY_CHECK
    if (index == MSHR_SIZE) {
        cerr << "[" << NAME << "_MSHR] " << __func__ << " cannot find an empty entry!";
        cerr << " instr_id: " << packet->instr_id << " addr: " << hex << packet->address;
        cerr << " full_addr: " << packet->full_addr << dec << endl;
        assert(0);
    }
#endif
}