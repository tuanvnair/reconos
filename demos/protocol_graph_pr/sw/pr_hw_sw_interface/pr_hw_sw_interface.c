#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#include <linux/delay.h>

#include "reconos.h"
#include "mbox.h"


#define S2H_HWT_SLOT_NR 0  // hwt_s2h
#define H2S_HWT_SLOT_NR 1  // hwt_h2s
#define ETH_HWT_SLOT_NR 2  // hwt_ethernet_test
#define DPR_HWT_SLOT_NR 3  // dynamic partial reconfiguable (DPR) hwt

#define NR_OF_PAGES 2 //32768 (2^15)

struct reconos_resource dpr_res[2];
struct reconos_hwt dpr_hwt;
struct reconos_resource eth_res[2];
struct reconos_hwt eth_hwt;
//struct reconos_resource s_res[2];
//struct reconos_hwt s_hwt;

struct reconos_resource h2s_res[2];
struct reconos_hwt h2s_hwt;
struct reconos_resource s2h_res[2];
struct reconos_hwt s2h_hwt;

struct mbox dpr_mb_put;
struct mbox dpr_mb_get;
struct mbox eth_mb_put;
struct mbox eth_mb_get;
//struct mbox s_mb_put;
//struct mbox s_mb_get;

struct mbox h2s_mb_put;
struct mbox h2s_mb_get;
struct mbox s2h_mb_put;
struct mbox s2h_mb_get;

char * shared_mem_h2s;
char * shared_mem_s2h;

struct task_struct *receive_thread;


//static uint32_t init_data = 0xDEADBEEF;

struct config_data {
	u32 dst_idp:8,
	    src_idp:8,
	    res:6,
	    latency_critical:1,
	    direction:1,
	    priority:2,
	    global_addr:4,
	    local_addr:2;
};

struct noc_pkt {
	u8 hw_addr_switch:4,
	   hw_addr_block:2,
	   priority:2;
	u8 direction:1,
	   latency_critical:1,
	   reserved:6;
	u16 payload_len;
	u32 src_idp;
	u32 dst_idp;
	u8* payload;
};


void copy_packet(int len, int start_val, char * addr, int global, int local){
	//hwaddrglobal hwaddrlocal, priority
	struct noc_pkt pkt;
	int i = 0;	
	pkt.hw_addr_switch = global; //(1/0 -> Ethernet, 1/1 -> loop back to SW);
	pkt.hw_addr_block = local;
	pkt.priority = 1;
	pkt.direction = 0;
	pkt.latency_critical = 1;
	pkt.reserved = 0;
	pkt.payload_len = len;
	pkt.src_idp = 0xaabbccaa;
	pkt.dst_idp = 0xddeeffdd;
	memcpy(addr, &pkt, sizeof(struct noc_pkt));
	while (len - i > 0){
		addr[12 + i] = (start_val + i) % 256;
		i++;
	}
}

void print_packet(struct noc_pkt * pkt){
	printk(KERN_INFO "global addr: %d\n", pkt->hw_addr_switch);
	printk(KERN_INFO "local addr: %d\n", pkt->hw_addr_block);
	printk(KERN_INFO "priority: %d\n", pkt->priority);
	printk(KERN_INFO "direction: %d\n", pkt->direction);
	printk(KERN_INFO "latency critical: %d\n", pkt->latency_critical);
	printk(KERN_INFO "payload_len: %d\n", pkt->payload_len);
	printk(KERN_INFO "src idp: %d\n", pkt->src_idp);
	printk(KERN_INFO "dst idp: %d\n", pkt->dst_idp);
}

static int hw_sw_interface_thread_entry(void *arg)
{
	struct noc_pkt * rcv_pkt;
	unsigned char packet_content[64];
	int result, j,ret;
	while(likely(!kthread_should_stop())){
		result = mbox_get(&h2s_mb_get);
		rcv_pkt = (struct noc_pkt *)shared_mem_h2s;
		printk(KERN_INFO "[reconos-interface] packet received with len from mbox %d, from memory %d\n", result, rcv_pkt->payload_len);		
		printk(KERN_INFO "packet received\n");
		print_packet(rcv_pkt);
		for (j = 0; j < 64; j++){ 
			packet_content[j] = shared_mem_h2s[j]; 
		}
		for (j = 0; j < 8; j++){ 
			printk(KERN_INFO "%02x %02x  %02x %02x  %02x %02x  %02x %02x\n", packet_content[(j*8)+0],packet_content[(j*8)+1],packet_content[(j*8)+2],packet_content[(j*8)+3], 
				packet_content[(j*8)+4],packet_content[(j*8)+5],packet_content[(j*8)+6],packet_content[(j*8)+7]);
		}
		mbox_put(&h2s_mb_put, (unsigned int) shared_mem_h2s);

		// try this reconfiguration
		// a) redirect packets directly from eth to sw
		mbox_put(&eth_mb_put,5); // 1: -> partial block, 5: -> h2s
		ret = mbox_get(&eth_mb_get);
		ret = mbox_get(&eth_mb_get);

		mbox_put(&dpr_mb_put,4);  // 4: -> to eth
		ret = mbox_get(&dpr_mb_get);
		printk(KERN_INFO "[partial slot] PR %x\n", ret);

		// b) terminate delegate thread and hw thread
		//mbox_put(&dpr_mb_put,0xffffffff);

		// c) partial slot reset='1' 
		reconos_slot_reset(DPR_HWT_SLOT_NR,1);
		printk(KERN_INFO "[partial slot] reconfig. START\n");

		// d) reconfigure slot
		// wait for about 20 seconds		
		for (j=0;j<20;j++) msleep(1000);
		printk(KERN_INFO "[partial slot] reconfig. STOP\n");

		// e) partial slot reset='0'
		//reconos_slot_reset(DPR_HWT_SLOT_NR,0);
		//printk(KERN_INFO "[partial slot] reset = 0 (reconfig. stop)\n");
		//for (j=0;j<2;j++) msleep(1000);	

		// f) create new delegate thread for dpr hw slot
		reconos_hwt_setresources(&dpr_hwt,dpr_res,2);
		reconos_hwt_create(&dpr_hwt,DPR_HWT_SLOT_NR,NULL);

		// g) redirect packets over partial slot
		mbox_put(&dpr_mb_put,5); mbox_put(&dpr_mb_put,5);
		ret = mbox_get(&dpr_mb_get);
		printk(KERN_INFO "[partial slot] PR %x\n", ret);

		mbox_put(&eth_mb_put,1); // 1: -> partial block, 5: -> h2s
		ret = mbox_get(&eth_mb_get);
		ret = mbox_get(&eth_mb_get);

		//mbox_put(&dpr_mb_put,1);

	}
	return 0;
}

static int __init init_pr_hw_sw_interface_module(void)
{
	//int ret2,i;
	//char * shared_mem_h2s;
	//char * shared_mem_s2h;
	//long unsigned jiffies_before;
	//long unsigned jiffies_after;
	//int result,j;
	//int ret3, ret4;
	//struct noc_pkt * rcv_pkt;
	//unsigned char packet_content[64];
	int ret;
	printk(KERN_INFO "[pr_hw_sw_interface] Init.\n");

	mbox_init(&dpr_mb_put, 200);
    	mbox_init(&dpr_mb_get, 200);
	mbox_init(&eth_mb_put, 200);
    	mbox_init(&eth_mb_get, 200);
	mbox_init(&h2s_mb_put, 2);
    	mbox_init(&h2s_mb_get, 2);
	mbox_init(&s2h_mb_put, 2);
    	mbox_init(&s2h_mb_get, 2);
	printk(KERN_INFO "[pr_hw_sw_interface] mbox_init done, starting autodetect.\n");

	reconos_init_autodetect();

	printk(KERN_INFO "[pr_hw_sw_interface] Creating hw-thread.\n");
	dpr_res[0].type = RECONOS_TYPE_MBOX;
	dpr_res[0].ptr  = &dpr_mb_put;	  	
    	dpr_res[1].type = RECONOS_TYPE_MBOX;
	dpr_res[1].ptr  = &dpr_mb_get;

	eth_res[0].type = RECONOS_TYPE_MBOX;
	eth_res[0].ptr  = &eth_mb_put;	  	
    	eth_res[1].type = RECONOS_TYPE_MBOX;
	eth_res[1].ptr  = &eth_mb_get;

	//s_res[0].type = RECONOS_TYPE_MBOX;
	//s_res[0].ptr  = &s_mb_put;	  	
    	//s_res[1].type = RECONOS_TYPE_MBOX;
	//s_res[1].ptr  = &s_mb_get;

	h2s_res[0].type = RECONOS_TYPE_MBOX;
	h2s_res[0].ptr  = &h2s_mb_put;	  	
    	h2s_res[1].type = RECONOS_TYPE_MBOX;
	h2s_res[1].ptr  = &h2s_mb_get;

	s2h_res[0].type = RECONOS_TYPE_MBOX;
	s2h_res[0].ptr  = &s2h_mb_put;	  	
    	s2h_res[1].type = RECONOS_TYPE_MBOX;
	s2h_res[1].ptr  = &s2h_mb_get;


	reconos_hwt_setresources(&dpr_hwt,dpr_res,2);
	reconos_hwt_create(&dpr_hwt,DPR_HWT_SLOT_NR,NULL);

    	reconos_hwt_setresources(&eth_hwt,eth_res,2);
	reconos_hwt_create(&eth_hwt,ETH_HWT_SLOT_NR,NULL);

	reconos_hwt_setresources(&h2s_hwt,h2s_res,2);
	reconos_hwt_create(&h2s_hwt,H2S_HWT_SLOT_NR,NULL);

	reconos_hwt_setresources(&s2h_hwt,s2h_res,2);
	reconos_hwt_create(&s2h_hwt,S2H_HWT_SLOT_NR,NULL);

	// forward incoming packets (from physical ETH interface to partial reconfigurable functional block hwt_pr_0) 
	printk(KERN_INFO "[pr_hw_sw_interface] wait for 1st message.\n");
	mbox_put(&eth_mb_put,1); // 1: -> partial block, 5: -> h2s
	ret = mbox_get(&eth_mb_get);
	ret = mbox_get(&eth_mb_get);

	//setup the hw -> sw thread
	printk(KERN_INFO "[pr_hw_sw_interface] Allocate memory\n");
	shared_mem_h2s = (char *) __get_free_pages(GFP_KERNEL | __GFP_NOWARN, 4); //allocate 2² pages get_zeroed_page(GFP_KERNEL);
	printk(KERN_INFO "[pr_hw_sw_interface] h2s memory %p\n", shared_mem_h2s);
	mbox_put(&h2s_mb_put, (unsigned int) shared_mem_h2s);

	//setup the sw -> hw thread
	shared_mem_s2h = (char *) __get_free_pages(GFP_KERNEL | __GFP_NOWARN, 4); //allocate 2² pages get_zeroed_page(GFP_KERNEL);
	printk(KERN_INFO "[pr_hw_sw_interface] s2h memory %p\n", shared_mem_s2h);
	mbox_put(&s2h_mb_put, (unsigned int) shared_mem_s2h);
	printk(KERN_INFO "[pr_hw_sw_interface] HZ= %d\n", HZ);

	memset(shared_mem_s2h, 0, 3000);
	memset(shared_mem_h2s, 0, 3000);

	/*while(1){
		result = mbox_get(&h2s_mb_get);
		rcv_pkt = (struct noc_pkt *)shared_mem_h2s;
		//packet_len = *(int *)shared_mem_h2s;
		printk(KERN_INFO "[reconos-interface] packet received with len from mbox %d, from memory %d\n", result, rcv_pkt->payload_len);

		//printk(KERN_INFO "packet sent\n");
		//print_packet(snd_pkt);		
		printk(KERN_INFO "packet received\n");
		print_packet(rcv_pkt);


		for (j = 0; j < 64; j++){ 
			packet_content[j] = shared_mem_h2s[j]; 
		}
		for (j = 0; j < 8; j++){ 
			printk(KERN_INFO "%02x %02x  %02x %02x  %02x %02x  %02x %02x\n", packet_content[(j*8)+0],packet_content[(j*8)+1],packet_content[(j*8)+2],packet_content[(j*8)+3], 
				packet_content[(j*8)+4],packet_content[(j*8)+5],packet_content[(j*8)+6],packet_content[(j*8)+7]);
		}
		mbox_put(&h2s_mb_put, (unsigned int) shared_mem_h2s);
		//printk(KERN_INFO "[pr_hw_sw_interface] wait for next message\n");
		//// send generated packets to HWT Ethernet Test
		//mbox_put(&eth_mb_put,5);
		//ret3 = mbox_get(&eth_mb_get);
		//ret4 = mbox_get(&eth_mb_get);
		////printk("packets: [Smart CAM] received=%08d   sent=%08d,   [ETH] received=%08d   sent=%08d\n",ret,ret2,ret3,ret4);
		//printk("packets: [ETH] received=%08d   sent=%08d\n",ret3,ret4);
		////printk("\n");
		//msleep(1000);
	}*/

	receive_thread = kthread_create(hw_sw_interface_thread_entry, 0,"hw_sw_iface_receive_thread");
        if (IS_ERR(receive_thread)) {
                printk(KERN_ERR "[pr_hw_sw_interface] Error creating 'receive' thread!\n");
                return -EIO;
        }

        wake_up_process(receive_thread);

	return 0;
}


static void __exit cleanup_pr_hw_sw_interface_module(void)
{
	reconos_cleanup();
	kthread_stop(receive_thread);
	printk("[pr_hw_sw_interface] unloaded\n");
}

module_init(init_pr_hw_sw_interface_module);
module_exit(cleanup_pr_hw_sw_interface_module);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Ariane Keller <ariane.keller@tik.ee.ethz.ch>");
MODULE_AUTHOR("Markus Happe  <markus.happe@tik.ee.ethz.ch>");
MODULE_DESCRIPTION("EmbedNet HW/SW interface");
