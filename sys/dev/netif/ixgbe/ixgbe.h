/******************************************************************************

  Copyright (c) 2001-2012, Intel Corporation 
  All rights reserved.
  
  Redistribution and use in source and binary forms, with or without 
  modification, are permitted provided that the following conditions are met:
  
   1. Redistributions of source code must retain the above copyright notice, 
      this list of conditions and the following disclaimer.
  
   2. Redistributions in binary form must reproduce the above copyright 
      notice, this list of conditions and the following disclaimer in the 
      documentation and/or other materials provided with the distribution.
  
   3. Neither the name of the Intel Corporation nor the names of its 
      contributors may be used to endorse or promote products derived from 
      this software without specific prior written permission.
  
  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE 
  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF 
  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) 
  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
  POSSIBILITY OF SUCH DAMAGE.

******************************************************************************/
/*$FreeBSD: src/sys/dev/ixgbe/ixgbe.h,v 1.26 2012/04/23 22:05:09 bz Exp $*/


#ifndef _IXGBE_H_
#define _IXGBE_H_


#include <sys/param.h>
#include <sys/systm.h>
#include <sys/mbuf.h>
#include <sys/protosw.h>
#include <sys/socket.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/ifq_var.h>
#include <net/bpf.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net/bpf.h>
#include <net/if_types.h>
#include <net/vlan/if_vlan_var.h>
#include <net/vlan/if_vlan_ether.h>

#include <netinet/in_systm.h>
#include <netinet/in.h>
#include <netinet/if_ether.h>
#include <netinet/ip.h>
#include <netinet/ip6.h>
#include <netinet/tcp.h>
#ifdef NET_LRO
#include <netinet/tcp_lro.h>	/* XXX: IPv4 only */
#endif
#include <netinet/udp.h>

#include <sys/in_cksum.h>

#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/resource.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/clock.h>
#include <bus/pci/pcivar.h>
#include <bus/pci/pcireg.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/endian.h>
#include <sys/taskqueue.h>
#include <machine/smp.h>

#include <sys/spinlock.h>
#include <sys/spinlock2.h>

#ifdef IXGBE_IEEE1588
#include <sys/ieee1588.h>
#endif

#include "ixgbe_api.h"
#include "ixgbe_defines.h"

/* Tunables */

/*
 * TxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of transmit descriptors allocated by the driver. Increasing this
 * value allows the driver to queue more transmits. Each descriptor is 16
 * bytes. Performance tests have show the 2K value to be optimal for top
 * performance.
 */
#define DEFAULT_TXD	1024
#define PERFORM_TXD	2048
#define MAX_TXD		4096
#define MIN_TXD		64

/*
 * RxDescriptors Valid Range: 64-4096 Default Value: 256 This value is the
 * number of receive descriptors allocated for each RX queue. Increasing this
 * value allows the driver to buffer more incoming packets. Each descriptor
 * is 16 bytes.  A receive buffer is also allocated for each descriptor. 
 * 
 * Note: with 8 rings and a dual port card, it is possible to bump up 
 *	against the system mbuf pool limit, you can tune nmbclusters
 *	to adjust for this.
 */
#define DEFAULT_RXD	1024
#define PERFORM_RXD	2048
#define MAX_RXD		4096
#define MIN_RXD		64

/* Alignment for rings */
#define DBA_ALIGN	128

/*
 * This parameter controls the maximum no of times the driver will loop in
 * the isr. Minimum Value = 1
 */
#define MAX_LOOP	10

/*
 * This is the max watchdog interval, ie. the time that can
 * pass between any two TX clean operations, such only happening
 * when the TX hardware is functioning.
 */
#define IXGBE_WATCHDOG                   (10 * hz)

/*
 * This parameters control when the driver calls the routine to reclaim
 * transmit descriptors.
 */
#define IXGBE_TX_CLEANUP_THRESHOLD	(adapter->num_tx_desc / 8)
#define IXGBE_TX_OP_THRESHOLD		(adapter->num_tx_desc / 32)

#define IXGBE_MAX_FRAME_SIZE	0x3F00

/* Flow control constants */
#define IXGBE_FC_PAUSE		0xFFFF
#define IXGBE_FC_HI		0x20000
#define IXGBE_FC_LO		0x10000

/* Keep older OS drivers building... */
#if !defined(SYSCTL_ADD_UQUAD)
#define SYSCTL_ADD_UQUAD SYSCTL_ADD_QUAD
#endif

/* Defines for printing debug information */
#define DEBUG_INIT  0
#define DEBUG_IOCTL 0
#define DEBUG_HW    0

#define INIT_DEBUGOUT(S)            if (DEBUG_INIT)  kprintf(S "\n")
#define INIT_DEBUGOUT1(S, A)        if (DEBUG_INIT)  kprintf(S "\n", A)
#define INIT_DEBUGOUT2(S, A, B)     if (DEBUG_INIT)  kprintf(S "\n", A, B)
#define IOCTL_DEBUGOUT(S)           if (DEBUG_IOCTL) kprintf(S "\n")
#define IOCTL_DEBUGOUT1(S, A)       if (DEBUG_IOCTL) kprintf(S "\n", A)
#define IOCTL_DEBUGOUT2(S, A, B)    if (DEBUG_IOCTL) kprintf(S "\n", A, B)
#define HW_DEBUGOUT(S)              if (DEBUG_HW) kprintf(S "\n")
#define HW_DEBUGOUT1(S, A)          if (DEBUG_HW) kprintf(S "\n", A)
#define HW_DEBUGOUT2(S, A, B)       if (DEBUG_HW) kprintf(S "\n", A, B)

#define MAX_NUM_MULTICAST_ADDRESSES     128
#define IXGBE_82598_SCATTER		100
#define IXGBE_82599_SCATTER		32
#define MSIX_82598_BAR			3
#define MSIX_82599_BAR			4
#define IXGBE_TSO_SIZE			262140
#define IXGBE_TX_BUFFER_SIZE		((u32) 1514)
#define IXGBE_RX_HDR			128
#define IXGBE_VFTA_SIZE			128
#define IXGBE_BR_SIZE			4096
#define IXGBE_QUEUE_MIN_FREE		32
#define IXGBE_QUEUE_IDLE		1
#define IXGBE_QUEUE_WORKING		2
#define IXGBE_QUEUE_HUNG		4
#define IXGBE_QUEUE_DEPLETED		8

/* Offload bits in mbuf flag */
#define CSUM_OFFLOAD		(CSUM_IP|CSUM_TCP|CSUM_UDP)

/* One for TX csum offloading desc, the other 2 are reserved */
#define IXGBE_TX_RESERVED		3

/*
 * Interrupt Moderation parameters 
 */
#define IXGBE_LOW_LATENCY	128
#define IXGBE_AVE_LATENCY	400
#define IXGBE_BULK_LATENCY	1200
#define IXGBE_LINK_ITR		2000

#define IXGBE_INTR_RATE		8000
#define IXGBE_EITR_INTVL_MASK	0x7ffc
#define IXGBE_EITR_INTVL_SHIFT	2

/*
 *****************************************************************************
 * vendor_info_array
 * 
 * This array contains the list of Subvendor/Subdevice IDs on which the driver
 * should load.
 * 
 *****************************************************************************
 */
typedef struct _ixgbe_vendor_info_t {
	unsigned int    vendor_id;
	unsigned int    device_id;
	unsigned int    subvendor_id;
	unsigned int    subdevice_id;
	unsigned int    index;
} ixgbe_vendor_info_t;


struct ixgbe_tx_buf {
	u32		eop_index;
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};

struct ixgbe_rx_buf {
	struct mbuf	*m_head;
	struct mbuf	*m_pack;
	struct mbuf	*fmp;
	bus_dmamap_t	hmap;
	bus_dmamap_t	pmap;
};

/*
 * Bus dma allocation structure used by ixgbe_dma_malloc and ixgbe_dma_free.
 */
struct ixgbe_dma_alloc {
	bus_addr_t		dma_paddr;
	caddr_t			dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

/*
** Driver queue struct: this is the interrupt container
**  for the associated tx and rx ring.
*/
struct ix_queue {
	struct adapter		*adapter;
	u32			msix;           /* This queue's MSIX vector */
	u32			eims;           /* This queue's EIMS bit */
	u32			eitr_setting;
	struct resource		*res;
	void			*tag;
	struct tx_ring		*txr;
	struct rx_ring		*rxr;
	u64			irqs;
	struct lwkt_serialize	serializer;
};

/*
 * The transmit ring, one per queue
 */
struct tx_ring {
        struct adapter		*adapter;
	struct lwkt_serialize	tx_serialize;
	u32			me;
	int			queue_status;
	int			watchdog_time;
	union ixgbe_adv_tx_desc	*tx_base;
	struct ixgbe_dma_alloc	txdma;
	u32			next_avail_desc;
	u32			next_to_clean;
	struct ixgbe_tx_buf	*tx_buffers;
	volatile u16		tx_avail;
	u32			txd_cmd;
	bus_dma_tag_t		txtag;
	char			lock_name[16];
#ifdef IFNET_BUF_RING
	struct buf_ring		*br;
#endif
#ifdef IXGBE_FDIR
	u16			atr_sample;
	u16			atr_count;
#endif
	u32			bytes;  /* used for AIM */
	u32			packets;
	/* Soft Stats */
	u64			no_desc_avail;
	u64			total_packets;
};


/*
 * The Receive ring, one per rx queue
 */
struct rx_ring {
        struct adapter		*adapter;
	struct lock		rx_lock;
	u32			me;
	union ixgbe_adv_rx_desc	*rx_base;
	struct ixgbe_dma_alloc	rxdma;
#ifdef NET_LRO
	struct lro_ctrl		lro;
#endif
	bool			lro_enabled;
	bool			hdr_split;
	bool			hw_rsc;
	bool			discard;
	bool			vtag_strip;
        u32			next_to_refresh;
        u32 			next_to_check;
	char			lock_name[16];
	struct ixgbe_rx_buf	*rx_buffers;
	bus_dma_tag_t		htag;
	bus_dma_tag_t		ptag;

	u32			bytes; /* Used for AIM calc */
	u32			packets;

	/* Soft stats */
	u64			rx_irq;
	u64			rx_split_packets;
	u64			rx_packets;
	u64 			rx_bytes;
	u64 			rx_discarded;
	u64 			rsc_num;
#ifdef IXGBE_FDIR
	u64			flm;
#endif
};

/* Our adapter structure */
struct adapter {
	struct ifnet		*ifp;
	struct ixgbe_hw		hw;

	struct ixgbe_osdep	osdep;
	struct device		*dev;

	struct resource		*pci_mem;
	struct resource		*msix_mem;

	/*
	 * Interrupt resources: this set is
	 * either used for legacy, or for Link
	 * when doing MSIX
	 */
	void			*tag;
	struct resource 	*res;
	int			intr_type;

	struct ifmedia		media;
	struct callout		timer;
	int			msix;
	int			if_flags;

	struct lock		core_lock;

	eventhandler_tag 	vlan_attach;
	eventhandler_tag 	vlan_detach;

	u16			num_vlans;
	u16			num_queues;

	/*
	** Shadow VFTA table, this is needed because
	** the real vlan filter table gets cleared during
	** a soft reset and the driver needs to be able
	** to repopulate it.
	*/
	u32			shadow_vfta[IXGBE_VFTA_SIZE];

	/* Info about the interface */
	u32			optics;
	u32			fc; /* local flow ctrl setting */
	int			advertise;  /* link speeds */
	bool			link_active;
	u16			max_frame_size;
	u16			num_segs;
	u32			link_speed;
	bool			link_up;
	u32 			linkvec;

	/* Mbuf cluster size */
	u32			rx_mbuf_sz;

	/* Support for pluggable optics */
	bool			sfp_probe;
	struct task     	mod_task;   /* SFP tasklet */
	struct task     	msf_task;   /* Multispeed Fiber */
#ifdef IXGBE_FDIR
	int			fdir_reinit;
	struct task     	fdir_task;
#endif
	struct taskqueue	*tq;

	/*
	** Queues: 
	**   This is the irq holder, it has
	**   and RX/TX pair or rings associated
	**   with it.
	*/
	struct ix_queue		*queues;

	/*
	 * Transmit rings:
	 *	Allocated at run time, an array of rings.
	 */
	struct tx_ring		*tx_rings;
	int			num_tx_desc;

	/*
	 * Receive rings:
	 *	Allocated at run time, an array of rings.
	 */
	struct rx_ring		*rx_rings;
	int			num_rx_desc;
	u64			que_mask;
	u32			rx_process_limit;

	/* Multicast array memory */
	u8			*mta;

	int			intr_rate;

	/* Misc stats maintained by the driver */
	unsigned long   	dropped_pkts;
	unsigned long   	mbuf_defrag_failed;
	unsigned long   	mbuf_header_failed;
	unsigned long   	mbuf_packet_failed;
	unsigned long   	no_tx_map_avail;
	unsigned long   	no_tx_dma_setup;
	unsigned long   	watchdog_events;
	unsigned long   	tso_tx;
	unsigned long		link_irq;

	struct ixgbe_hw_stats 	stats;

	struct lwkt_serialize	serializer;
	struct sysctl_ctx_list	sysctl_ctx;
	struct sysctl_oid	*sysctl_tree;
};

/* Precision Time Sync (IEEE 1588) defines */
#define ETHERTYPE_IEEE1588      0x88F7
#define PICOSECS_PER_TICK       20833
#define TSYNC_UDP_PORT          319 /* UDP port for the protocol */
#define IXGBE_ADVTXD_TSTAMP	0x00080000


#define IXGBE_CORE_LOCK_INIT(_sc, _name) \
        lockinit(&(_sc)->core_lock, _name, 0, LK_CANRECURSE)
#define IXGBE_TX_LOCK_INIT(_sc)		  lwkt_serialize_init(&(_sc)->tx_serialize)
#define IXGBE_CORE_LOCK_DESTROY(_sc)      lockuninit(&(_sc)->core_lock)
#define IXGBE_TX_LOCK_DESTROY(_sc)
#define IXGBE_RX_LOCK_DESTROY(_sc)        lockuninit(&(_sc)->rx_lock)
#define IXGBE_CORE_LOCK(_sc)              lockmgr(&(_sc)->core_lock, LK_EXCLUSIVE)
#define IXGBE_TX_LOCK(_sc)                lwkt_serialize_enter(&(_sc)->tx_serialize)
#define IXGBE_TX_TRYLOCK(_sc)             lwkt_serialize_try(&(_sc)->tx_serialize)
#define IXGBE_RX_LOCK(_sc)                lockmgr(&(_sc)->rx_lock, LK_EXCLUSIVE)
#define IXGBE_CORE_UNLOCK(_sc)            lockmgr(&(_sc)->core_lock, LK_RELEASE)
#define IXGBE_TX_UNLOCK(_sc)              lwkt_serialize_exit(&(_sc)->tx_serialize)
#define IXGBE_RX_UNLOCK(_sc)              lockmgr(&(_sc)->rx_lock, LK_RELEASE)
#define IXGBE_CORE_LOCK_ASSERT(_sc)       KKASSERT(lockstatus(&(_sc)->core_lock, curthread) !=0)
#define IXGBE_TX_LOCK_ASSERT(_sc)         ASSERT_SERIALIZED(&(_sc)->tx_serialize)


static inline bool
ixgbe_is_sfp(struct ixgbe_hw *hw)
{
	switch (hw->phy.type) {
	case ixgbe_phy_sfp_avago:
	case ixgbe_phy_sfp_ftl:
	case ixgbe_phy_sfp_intel:
	case ixgbe_phy_sfp_unknown:
	case ixgbe_phy_sfp_passive_tyco:
	case ixgbe_phy_sfp_passive_unknown:
		return TRUE;
	default:
		return FALSE;
	}
}

/*
** Find the number of unrefreshed RX descriptors
*/
static inline u16
ixgbe_rx_unrefreshed(struct rx_ring *rxr)
{       
	struct adapter  *adapter = rxr->adapter;
        
	if (rxr->next_to_check > rxr->next_to_refresh)
		return (rxr->next_to_check - rxr->next_to_refresh - 1);
	else
		return ((adapter->num_rx_desc + rxr->next_to_check) -
		    rxr->next_to_refresh - 1);
}       

#endif /* _IXGBE_H_ */
