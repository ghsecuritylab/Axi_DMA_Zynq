/*
 * Copyright (C) 2017 - 2019 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include "xparameters.h"
#include "netif/xadapter.h"
#include "platform.h"
#include "platform_config.h"
#include "lwipopts.h"
#include "xil_printf.h"
#include "sleep.h"
#include "lwip/priv/tcp_priv.h"
#include "lwip/init.h"
#include "lwip/inet.h"
#include "xil_cache.h"
#include "xil_io.h"
#include "xscugic.h"
#include "ps7_init.h"
//#include "udp_perf_client.h"

#if LWIP_DHCP==1
#include "lwip/dhcp.h"
extern volatile int dhcp_timoutcntr;
#endif

extern volatile int TcpFastTmrFlag;
extern volatile int TcpSlowTmrFlag;

#define DEFAULT_IP_ADDRESS	"192.168.1.10"
#define DEFAULT_IP_MASK		"255.255.255.0"
#define DEFAULT_GW_ADDRESS	"192.168.1.1"

void platform_enable_interrupts(void);
void start_application(void);
void transfer_data(void);
void print_app_header(void);

#if defined (__arm__) && !defined (ARMR5)
#if XPAR_GIGE_PCS_PMA_SGMII_CORE_PRESENT == 1 || \
		 XPAR_GIGE_PCS_PMA_1000BASEX_CORE_PRESENT == 1
int ProgramSi5324(void);
int ProgramSfpPhy(void);
#endif
#endif

#ifdef XPS_BOARD_ZCU102
#ifdef XPAR_XIICPS_0_DEVICE_ID
int IicPhyReset(void);
#endif
#endif


// ddr address with data
#define DDR_DATA_ADDRESS 0x0a000000

struct netif server_netif;

// **************************************************************************************************
// interrupt variables
XScuGic InterruptController;
static XScuGic_Config *GicConfig;
//u32 *global_counter_ptr = 0x0a000028;
static volatile u32 global_counter = 0;
static volatile int state = 0;

void StartDMATransfer( unsigned int dstAddress, unsigned int len);

void InterruptHandler (void)
{
	//xil_printf("DMA ISR");
	//xil_printf("inside the DMA ISR");
	u32 tmpValue;

	tmpValue = Xil_In32( XPAR_AXI_DMA_0_BASEADDR + 0x34 );
	tmpValue = tmpValue | 0x1000;
	Xil_Out32( XPAR_AXI_DMA_0_BASEADDR + 0x34, tmpValue);

//	if (state == 0)
//	{
//		state++;
//		StartDMATransfer( DDR_DATA_ADDRESS, 256 );
//	}

	return;

//	if (global_counter == 1)
//	{
//		//xil_printf("Int ret");
//		return;
//	}
//
//	else
//	{
//		//xil_printf("Int contd");
//		//*(global_counter_ptr) = *(global_counter_ptr) + 1;
//		global_counter++;
//		StartDMATransfer( DDR_DATA_ADDRESS, 256 );
//	}
//
//	if ( *(global_counter_ptr) > 3 )
//	{
//		xil_printf( "Frame number : %d \n\r", global_counter);
//		return;
//	}


}

int initializeAXIDma(void)
{
	unsigned int tmpVal;

	tmpVal = Xil_In32( XPAR_AXI_DMA_0_BASEADDR + 0x30 );
	tmpVal = tmpVal | 0x1001;
	Xil_Out32( XPAR_AXI_DMA_0_BASEADDR + 0x30, tmpVal );
	tmpVal = Xil_In32( XPAR_AXI_DMA_0_BASEADDR + 0x30 );
	xil_printf( "value for dma control register : %x\n\r", tmpVal );

	return 0;
}

int InitInterruptSystem( int deviceID )
{
	int Status;

	/*
	 * Initialize the interrupt controller driver so that it is ready to
	 * use.
	 */
	GicConfig = XScuGic_LookupConfig( deviceID );
	if (NULL == GicConfig) {
		return XST_FAILURE;
	}

	Status = XScuGic_CfgInitialize( &InterruptController, GicConfig,
				       GicConfig->CpuBaseAddress);
	if (Status != XST_SUCCESS) {
		return XST_FAILURE;
	}

	Xil_ExceptionRegisterHandler(XIL_EXCEPTION_ID_INT,
			     (Xil_ExceptionHandler)XScuGic_InterruptHandler,
			     NULL);

	Xil_ExceptionEnable();

	Status = XScuGic_Connect( &InterruptController,
				 XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR,
				 (Xil_ExceptionHandler)InterruptHandler,
				 NULL);
	if (Status != XST_SUCCESS)
	{
		return XST_FAILURE;
	}

	XScuGic_Enable( &InterruptController,
			XPAR_FABRIC_AXI_DMA_0_S2MM_INTROUT_INTR);

	return XST_SUCCESS;

}

void StartDMATransfer( unsigned int dstAddress, unsigned int len)
{
	// write destination address to S2MM_DA register
	Xil_Out32 ( XPAR_AXI_DMA_0_BASEADDR + 0x48, dstAddress );

	// write length to s2mm_length register
	Xil_Out32 ( XPAR_AXI_DMA_0_BASEADDR + 0x58, len );
}

// **************************************************************************************************

static void print_ip(char *msg, ip_addr_t *ip)
{
	print(msg);
	xil_printf("%d.%d.%d.%d\r\n", ip4_addr1(ip), ip4_addr2(ip),
			ip4_addr3(ip), ip4_addr4(ip));
}

static void print_ip_settings(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{
	print_ip("Board IP:       ", ip);
	print_ip("Netmask :       ", mask);
	print_ip("Gateway :       ", gw);
}

static void assign_default_ip(ip_addr_t *ip, ip_addr_t *mask, ip_addr_t *gw)
{
	int err;

	xil_printf("Configuring default IP %s \r\n", DEFAULT_IP_ADDRESS);

	err = inet_aton(DEFAULT_IP_ADDRESS, ip);
	if (!err)
		xil_printf("Invalid default IP address: %d\r\n", err);

	err = inet_aton(DEFAULT_IP_MASK, mask);
	if (!err)
		xil_printf("Invalid default IP MASK: %d\r\n", err);

	err = inet_aton(DEFAULT_GW_ADDRESS, gw);
	if (!err)
		xil_printf("Invalid default gateway address: %d\r\n", err);
}

int main(void)
{
	struct netif *netif;

	/* the mac address of the board. this should be unique per board */
	unsigned char mac_ethernet_address[] = {
		0x00, 0x0a, 0x35, 0x00, 0x01, 0x02 };

	netif = &server_netif;
#if defined (__arm__) && !defined (ARMR5)
#if XPAR_GIGE_PCS_PMA_SGMII_CORE_PRESENT == 1 || \
		XPAR_GIGE_PCS_PMA_1000BASEX_CORE_PRESENT == 1
	ProgramSi5324();
	ProgramSfpPhy();
#endif
#endif

	/* Define this board specific macro in order perform PHY reset
	 * on ZCU102
	 */
#ifdef XPS_BOARD_ZCU102
	IicPhyReset();
#endif

	init_platform();

	// enable pl
	ps7_post_config();

	// init dma
	initializeAXIDma();

	// enable interrupt
	//xil_printf( "enabling the interrupt handling system... \n\r");
	InitInterruptSystem( XPAR_PS7_SCUGIC_0_DEVICE_ID );

	// start the dma transfer
	//xil_printf( "starting dma... \n\r");
	StartDMATransfer( DDR_DATA_ADDRESS, 256 );

	//xil_printf("\r\n\r\n");
	//xil_printf("-----lwIP RAW Mode UDP Client Application-----\r\n");

	/* initialize lwIP */
	lwip_init();

	/* Add network interface to the netif_list, and set it as default */
	if (!xemac_add(netif, NULL, NULL, NULL, mac_ethernet_address,
				PLATFORM_EMAC_BASEADDR)) {
		//xil_printf("Error adding N/W interface\r\n");
		return -1;
	}
	netif_set_default(netif);

	/* now enable interrupts */
	platform_enable_interrupts();

	/* specify that the network if is up */
	netif_set_up(netif);

#if (LWIP_DHCP==1)
	/* Create a new DHCP client for this interface.
	 * Note: you must call dhcp_fine_tmr() and dhcp_coarse_tmr() at
	 * the predefined regular intervals after starting the client.
	 */
	dhcp_start(netif);
	dhcp_timoutcntr = 24;
	while (((netif->ip_addr.addr) == 0) && (dhcp_timoutcntr > 0))
		xemacif_input(netif);

	if (dhcp_timoutcntr <= 0) {
		if ((netif->ip_addr.addr) == 0) {
			xil_printf("ERROR: DHCP request timed out\r\n");
			assign_default_ip(&(netif->ip_addr),
					&(netif->netmask), &(netif->gw));
		}
	}

	/* print IP address, netmask and gateway */
#else
	assign_default_ip(&(netif->ip_addr), &(netif->netmask), &(netif->gw));
#endif
	print_ip_settings(&(netif->ip_addr), &(netif->netmask), &(netif->gw));

	//xil_printf("\r\n");

	/* print app header */
	print_app_header();

	/* start the application*/
	start_application();
	//xil_printf("\r\n");

	while (1) {
		if (TcpFastTmrFlag) {
			tcp_fasttmr();
			TcpFastTmrFlag = 0;
		}
		if (TcpSlowTmrFlag) {
			tcp_slowtmr();
			TcpSlowTmrFlag = 0;
		}
		xemacif_input(netif);
		transfer_data();
	}

	/* never reached */
	//cleanup_platform();

	return 0;
}