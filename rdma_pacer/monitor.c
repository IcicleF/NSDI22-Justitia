#include "monitor.h"
#include "pingpong.h"
#include "p2.h"
#include "get_clock.h"
#include "countmin.h"
#include "pacer.h"

void monitor_latency(void *arg) {
    printf("starting monitor_latency...\n");

    // struct p2_meta_data p2;
    CMH_type *cmh;
    struct pingpong_context *ctx;
    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge sge;
    struct ibv_wc wc;
    cycles_t start_cycle, end_cycle;
    unsigned int tail_99, tail_999, lat, base_tail_99, base_tail_999; // in nanosecond
    int cpu_mhz, no_cpu_freq_warn = 1;
    long long seq = 0;

    const char *servername = ((struct monitor_param *)arg)->addr;
    int isclient = ((struct monitor_param *)arg)->isclient;
    int num_comp;
    uint32_t temp;
    // int i;
    // uint32_t chunk_size;
    
    ctx = init_monitor_chan(servername, isclient);
    if (!ctx) {
        fprintf(stderr, "exiting monitor_latency\n");
        return;
    }

    // init_p2_meta_data(&p2);
    cmh = CMH_Init(32768, 16, 32, 1);
    if (!cmh) {
        printf("Failed to allocate hierarchical countmin sketches\n");
        return;
    }
    printf("The size of countmin sketches is %d Bytes\n", CMH_Size(cmh));

    cpu_mhz = get_cpu_mhz(no_cpu_freq_warn);

    /* WR */
    memset(&wr, 0, sizeof wr);
    wr.opcode = IBV_WR_RDMA_WRITE;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = (IBV_SEND_SIGNALED | IBV_SEND_INLINE);
    wr.wr_id = seq;
    seq++;
    wr.wr.rdma.rkey = ctx->rem_dest->rkey;
    wr.wr.rdma.remote_addr = ctx->rem_dest->vaddr;

    sge.addr = (uintptr_t)ctx->send_buf;
    sge.length = BUF_SIZE;
    sge.lkey = ctx->send_mr->lkey;

    /* monitor loop */
    while (1) {
        start_cycle = get_cycles();
        if(ibv_post_send(ctx->qp, &wr, &bad_wr)) {
            fprintf(stderr, "Failed to send wr\n");
        } else {
            do {
                num_comp = ibv_poll_cq(ctx->cq, 1, &wc);
            } while (num_comp == 0);
            if (wc.status == IBV_WC_SUCCESS) {
                end_cycle = get_cycles();
                lat = (end_cycle - start_cycle) * 1000 / cpu_mhz;

                // tail = query_tail_p2(lat, &p2);
                // printf("@@@latency = %u\n", lat);
                // printf("@@@tail = %u\n", tail);

                
                CMH_Update(cmh, lat, 1);
                // fprintf(stderr, "DEBUG1\n");
                tail_99 = CMH_Quantile(cmh, 0.99);
                tail_999 = CMH_Quantile(cmh, 0.999);  

                if (__atomic_load_n(&cb.num_active_big_flows, __ATOMIC_RELAXED)) {
                    if (__atomic_load_n(&cb.num_active_small_flows, __ATOMIC_RELAXED)) {
                        if (tail_99 > base_tail_99 * 2 || tail_999 > base_tail_999 * 2) {
                            /* Multiplicative Decrease */
                            temp = __atomic_load_n(&cb.virtual_link_cap, __ATOMIC_RELAXED) / 2;
                            __atomic_store_n(&cb.virtual_link_cap, temp, __ATOMIC_RELAXED);
                        } else {
                            /* Additive Increase */
                            __atomic_fetch_add(&cb.virtual_link_cap, 1, __ATOMIC_RELAXED);
                        }
                    }
                } else {
                    base_tail_99 = tail_99;
                    base_tail_999 = tail_999;
                }
            } else {
                perror("ibv_poll_cq");
            }
        }
    }
}
