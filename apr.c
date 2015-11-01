#include "pdp6.h"

/* external pulses:
 * KEY MANUAL
 * MAI-CMC ADDR ACK
 * MAI RD RS
 */

Apr apr;

void
decode_ch(void)
{
	int ir6_8;
	ir6_8 = apr.ir>>9 & 7;
	if((apr.ir & 0770000) == 0130000){
		apr.ch_inc = ((ir6_8 & 5) == 4 || ir6_8 == 3) && !apr.chf5;
		apr.ch_inc_op = apr.ch_inc && !apr.chf7;
		apr.ch_n_inc_op = (ir6_8 & 5) == 5 && !apr.chf5 ||
		                  apr.ch_inc && apr.chf7;
		apr.ch_load = (ir6_8 & 6) == 4 && apr.chf5;
		apr.ch_dep = (ir6_8 & 6) == 6 && apr.chf5;
	}else{
		apr.ch_inc =  apr.ch_inc_op = apr.ch_n_inc_op = 0;
		apr.ch_load = apr.ch_dep = 0;
	}
}

void
decode_2xx(void)
{
	int inst;
	inst = apr.ir>>9 & 0777;
	apr.fwt = (apr.ir & 0760000) == 0200000;
	if(inst >= 0244 && inst <= 0246 ||
	   (inst & 0774) == 0234)
		apr.fac2 = 1;
	apr.fc_c_acrt = (inst & 0776) == 0262;
	apr.fc_c_aclt = inst == 0251 || inst == 0267;
}

void
decodeir(void)
{
	bool iot_a, jrst_a, uuo_a;

	apr.fac2 = 0;
	decode_ch();
	decode_2xx();
	uuo_a = (apr.ir & 0700000) == 0;
	iot_a = (apr.ir & 0700000) == 0700000;
	jrst_a = (apr.ir & 0777000) == 0254000;
	// 5-13
	apr.ex_ir_uuo =
		uuo_a && apr.ex_uuo_sync ||
		iot_a && !apr.ex_pi_sync && apr.ex_user && !apr.cpa_iot_user ||
		jrst_a && (apr.ir & 0000600) && apr.ex_user;
	apr.ir_jrst = !apr.ex_ir_uuo && jrst_a;
	apr.ir_iot = !apr.ex_ir_uuo && iot_a;
}

void
set_ex_mode_sync(bool value)
{
	apr.ex_mode_sync = value;
	if(apr.ex_mode_sync)
		apr.ex_user = 1;	// 5-13
}

void
set_pi_cyc(bool value)
{
	apr.pi_cyc = value;
	if(!apr.pi_cyc)
		apr.ex_pi_sync = 1;	// 5-13
}

void
set_pir(u8 value)
{
	int chan;
	u8 req;
	// 8-3
	apr.pir = value;
	apr.pi_req = 0;
	if(apr.pi_active){
		req = apr.pir & ~apr.pih;
		for(chan = 0100; chan; chan >>= 1)
			if(req & chan){
				apr.pi_req = chan;
				break;
			}
	}
	apr.pi_rq = !!apr.pi_req;	// 8-4
}

void
set_mc_rq(bool value)
{
	apr.mc_rq = value;		// 7-9
	if(value && (apr.mc_rd || apr.mc_wr)){
		membus0 |= MEMBUS_RQ_CYC;
		wakemem();
	}else
		membus0 &= ~MEMBUS_RQ_CYC;
}

void
set_mc_wr(bool value)
{
	apr.mc_wr = value;		// 7-9
	if(value)
		membus0 |= MEMBUS_WR_RQ;
	else
		membus0 &= ~MEMBUS_WR_RQ;
	set_mc_rq(apr.mc_rq);
}

void
set_mc_rd(bool value)
{
	apr.mc_rd = value;		// 7-9
	if(value)
		membus0 |= MEMBUS_RD_RQ;
	else
		membus0 &= ~MEMBUS_RD_RQ;
	set_mc_rq(apr.mc_rq);
}

void
set_key_rim_sbr(bool value)
{
	// not sure if this is correct
	apr.key_rim_sbr = value | apr.sw_rim_maint;	// 5-2
}

bool
calcaddr(void)
{
	u8 ma18_25;
	bool ma_ok, ma_fmc_select;

	// 5-13
	apr.ex_inh_rel = !apr.ex_user || apr.ex_pi_sync ||
	                 (apr.ma & 0777760) == 0 || apr.ex_ill_op;
	// 7-4
	ma18_25 = apr.ma>>10 & 0377;
	ma_ok = ma18_25 <= apr.pr;
	// 7-2
	ma_fmc_select = !apr.key_rim_sbr && (apr.ma & 0777760) == 0;
	// 7-5
	apr.rla = ma18_25;
	if(!apr.ex_inh_rel)
		apr.rla += apr.rlr;

	membus0 &= ~0007777777761;
	membus0 |= ma_fmc_select ? MEMBUS_MA_FMC_SEL1 : MEMBUS_MA_FMC_SEL0;
	membus0 |= (apr.ma&01777) << 4;
	membus0 |= (apr.rla&017) << 14;
	membus0 |= apr.rla & 0020 ? MEMBUS_MA21_1|MEMBUS_MA21 : MEMBUS_MA21_0;
	membus0 |= apr.rla & 0040 ? MEMBUS_MA20_1 : MEMBUS_MA20_0;
	membus0 |= apr.rla & 0100 ? MEMBUS_MA19_1 : MEMBUS_MA19_0;
	membus0 |= apr.rla & 0200 ? MEMBUS_MA18_1 : MEMBUS_MA18_0;
	membus0 |= apr.ma & 01 ? MEMBUS_MA35_1 : MEMBUS_MA35_0;
	return ma_ok;
}

pulse(kt4);
pulse(it1);
pulse(at1);
pulse(at0);
pulse(iat0);
pulse(mc_wr_rs);
pulse(mc_rd_rq_pulse);

// TODO: find A LONG

pulse(pi_reset){
	printf("PI RESET\n");
	apr.pi_active = 0;	// 8-4
	apr.pih = 0;		// 8-4
	set_pir(0);		// 8-4
	apr.pio = 0;		// 8-3
	return NULL;
}

pulse(ar_flag_clr){
	printf("AR FLAG CLR\n");
	apr.pc_chg_flag = 0;	// 6-10
	apr.ar_ov_flag = 0;	// 6-10
	apr.ar_cry0_flag = 0;	// 6-10
	apr.ar_cry1_flag = 0;	// 6-10
	apr.chf7 = 0;		// 6-19
	return NULL;
}

// TODO
pulse(mp_clr){
	apr.chf5 = 0;		// 6-19
	return NULL;
}

pulse(mr_clr){
	printf("MR CLR\n");
	apr.ir = 0;	// 5-7
	apr.mq = 0;	// 6-13
	apr.sc = 0;	// 6-15
	apr.fe = 0;	// 6-15

	apr.mc_rd = 0;		// 7-9
	apr.mc_wr = 0;		// 7-9
	apr.mc_rq = 0;		// 7-9
	apr.mc_stop = 0;	// 7-9
	apr.mc_stop_sync = 0;	// 7-9
	apr.mc_split_cyc_sync = 0;	// 7-9

	set_ex_mode_sync(0);	// 5-13
	apr.ex_uuo_sync = 0;	// 5-13
	apr.ex_pi_sync = 0;	// 5-13

	apr.a_long = 0;		// nowhere to be found :(
	mp_clr();
	// TODO: DS CLR

	/* sbr flip-flops */
	apr.key_rd_wr = 0;	// 5-2
	apr.if1a = 0;		// 5-3
	apr.af0 = 0;		// 5-3
	apr.af3 = 0;		// 5-3
	apr.af3a = 0;		// 5-3
	apr.f1a = 0;		// 5-4
	apr.f4a = 0;		// 5-4
	apr.f6a = 0;		// 5-4
	apr.mc_rst1_ret = NULL;
	apr.art3_ret = NULL;
	return NULL;
}

pulse(mr_start){
	printf("MR START\n");
	// IOB RESET
	apr.cpa_iot_user = 0;	// 8-5
	apr.cpa_illeg_op = 0;	// 8-5
	apr.cpa_non_exist_mem = 0;	// 8-5
	apr.cpa_clock_en = 0;	// 8-5
	apr.cpa_clock_flag = 0;	// 8-5
	apr.cpa_pc_chg_en = 0;	// 8-5
	apr.cpa_pdl_ov = 0;	// 8-5
	apr.cpa_arov_en = 0;	// 8-5
	apr.cpa_pia33 = 0;	// 8-5
	apr.cpa_pia34 = 0;	// 8-5
	apr.cpa_pia35 = 0;	// 8-5
	apr.pi_ov = 0;		// 8-4
	set_pi_cyc(0);		// 8-4
	pi_reset();		// 8-4
	ar_flag_clr();		// 6-10
	apr.ex_user = 0;	// 5-13
	apr.ex_ill_op = 0;	// 5-13
	apr.pr = 0;		// 5-13, 7-4
	apr.rlr = 0;		// 5-13, 7-5
	apr.rla = 0;

//	apr.ex_user = 1;
//	apr.rlr = 2;
//	apr.pr = 1;
	return NULL;
}

pulse(mr_pwr_clr){
	printf("MR PWR CLR\n");
	apr.run = 0;	// 5-1
	/* order matters because of EX PI SYNC */
	mr_start();	// 5-2
	mr_clr();	// 5-2
	return NULL;
}

pulse(st7){
	printf("ST7\n");
	return NULL;
}

/*
 * AR 
 */

// very temporary

pulse(art3){
//	apr.ar_com_cont = 0;
	return apr.art3_ret;
}

pulse(ar_ast1){
	printf("AR AST1,2\n");
	// TODO
	apr.ar += apr.mb;
	return art3;
}

/*
 * Priority Interrupt
 */

pulse(pir_stb){
	printf("PIR STB\n");
	set_pir(apr.pir | apr.pio & iobus1);	// 8-3
	return NULL;
}

pulse(pi_sync){
	printf("PI SYNC\n");
	if(!apr.pi_cyc)
		pir_stb();
	// 5-3
	if(apr.pi_rq && !apr.pi_cyc)
		return iat0;
	// TODO: IA INH/AT INH
	if(apr.if1a)
		return it1;
	return at1;
}

/*
 * Fetch
 */

pulse(ft5){
	printf("FT5\n");
	apr.ma = apr.mb & RT;		// 7-3
	return NULL;
}

pulse(ft4a){
	word tmp;

	printf("FT4A\n");
	apr.f4a = 0;			// 5-4
	apr.ma = 0;			// 7-3
	tmp = apr.mb;
	apr.mb = apr.mq;		// 6-3
	apr.mq = tmp;			// 6-13
	return ft5;			// 5-4
}

pulse(ft4){
	printf("FT4\n");
	apr.mq = apr.mb;		// 6-13
	apr.f4a = 1;			// 5-4
	apr.mc_rst1_ret = ft4a;
	return mc_rd_rq_pulse;
}

pulse(ft3){
	word tmp;

	printf("FT3\n");
	apr.ma = apr.mb & RT;		// 7-3
	tmp = apr.mb;			// 6-3
	apr.mb = apr.ar;
	apr.ar = tmp;
	return ft4;			// 5-4
}

pulse(ft1a){
	word tmp;

	printf("FT1A\n");
	apr.f1a = 0;			// 5-4
	if(apr.fac2)
		apr.ma = apr.ma+1 & 017;	// 7-1, 7-5
	else
		apr.ma = 0;		// 7-3
	if(!(apr.fc_c_aclt || apr.fc_c_acrt)){
		tmp = apr.mb;		// 6-3
		apr.mb = apr.ar;
		apr.ar = tmp;
	}
	if(apr.fc_c_aclt)
		apr.mb = apr.mb<<18 & LT | apr.mb>>18 & RT;	// 6-3
	if(apr.fac2)
		return ft4;		// 5-4
	if(apr.fc_c_aclt || apr.fc_c_acrt)
		return ft3;		// 5-4
	return ft5;			// 5-4
}

pulse(ft1){
	printf("FT1\n");
	apr.ma = apr.ir>>5 & 017;	// 7-3
	apr.f1a = 1;			// 5-4
	apr.mc_rst1_ret = ft1a;
	return mc_rd_rq_pulse;
}

pulse(ft0){
	bool fac_inh;

	printf("FT0\n");
	// 5-4
	fac_inh = apr.ch_inc_op || apr.ch_n_inc_op || apr.ch_load ||
		apr.ex_ir_uuo || apr.fwt && (apr.ir & 0003000) != 0002000 ||
		(apr.ir & 0703000) == 0503000 || apr.ir_iot ||
		(apr.ir & 0774000) == 0254000 ||	/* 254-257 */
		(apr.ir & 0776000) == 0264000 ||	/* JSR, JSP */
		(apr.ir & 0700000) == 0300000 && (apr.ir & 0060000); /* MEMAC */
	if(fac_inh)
		return ft5;		// 5-4
	return ft1;			// 5-4
}

/*
 * Address
 */

pulse(at5){
	printf("AT5\n");
	apr.a_long = 1;			// nowhere to be found :(
	apr.af0 = 1;			// 5-3
	apr.ma = apr.mb & RT;		// 7-3
	apr.ir &= ~037;			// 5-7
	apr.mc_rst1_ret = at0;
	return mc_rd_rq_pulse;
}

pulse(at4){
	printf("AT4\n");
	apr.ar &= ~LT;			// 6-8
	// TODO: what is MC DR SPLIT?
	if(apr.sw_addr_stop || apr.key_mem_stop)
		apr.mc_split_cyc_sync = 1;	// 7-9
	if(apr.ir & 020)
		return at5;		// 5-3
	return ft0;			// 5-4
}

pulse(at3a){
	printf("AT3A\n");
	apr.af3a = 0;			// 5-3
	apr.mb = apr.ar;		// 6-3
	return at4;			// 5-3
}

pulse(at3){
	printf("AT3\n");
	apr.af3 = 0;			// 5-3
	apr.ma = 0;			// 7-3
	apr.af3a = 1;			// 5-3
	apr.art3_ret = at3a;
	return ar_ast1;
}

pulse(at2){
	printf("AT2\n");
	apr.a_long = 1;			// nowhere to be found :(
	apr.ma = apr.ir & 017;		// 7-3
	apr.af3 = 1;			// 5-3
	apr.mc_rst1_ret = at3;
	return mc_rd_rq_pulse;
}

pulse(at1){
	printf("AT1\n");
	apr.ex_uuo_sync = 1;		// 5-13
	if((apr.ir & 017) == 0)
		return at4;		// 5-3
	return at2;			// 5-3
}

pulse(at0){
	printf("AT0\n");
	apr.ar &= ~RT;			// 6-8
	apr.ar |= apr.mb & RT;		// 6-8
	apr.ir |= apr.mb>>18 & 037;	// 5-7
	decodeir();
	apr.ma = 0;			// 7-3
	apr.af0 = 0;			// 5-3
	return pi_sync;			// 8-4
}

/*
 * Instruction
 */

pulse(iat0){
	printf("IAT0\n");
	mr_clr();
	set_pi_cyc(1);
	return it1;
}

pulse(it1a){
	printf("IT1A\n");
	apr.if1a = 0;
	apr.ir |= apr.mb>>18 & 0777740;	// 5-7
	if(apr.ma & 0777760)
		set_key_rim_sbr(0);	// 5-2
	return at0;
}

pulse(it1){
	printf("IT1\n");
	hword n;
	u8 r;
	if(apr.pi_cyc){
		// 7-3
		r = apr.pi_req;
		for(n = 7; !(r & 1); n--, r >>= 1);
		apr.ma = 040 | n<<1;
	}else
		apr.ma = apr.pc;	// 7-3
	if(apr.pi_ov)
		apr.ma = (apr.ma+1)&RT;	// 7-3
	apr.if1a = 1;
	apr.mc_rst1_ret = it1a;
	return mc_rd_rq_pulse;
}

pulse(it0){
	printf("IT0\n");
	apr.ma = 0;
	mr_clr();
	apr.if1a = 1;		// 5-3
	return pi_sync;		// 8-4
}

/*
 * Memory Control
 */

pulse(mc_rs_t1){
	printf("MC RS T1\n");
	set_mc_rd(0);
	if(apr.key_ex_next || apr.key_dep_next)
		apr.mi = apr.mb;	// 7-7
	return apr.mc_rst1_ret;
}

pulse(mc_rs_t0){
	printf("MC RS T0\n");
	apr.mc_stop = 0;
	return mc_rs_t1;
}

pulse(mc_wr_rs){
	printf("MC WR RS\n");
	if(apr.ma == apr.mas)
		apr.mi = apr.mb;	// 7-7
	membus1 = apr.mb;
	membus0 |= MEMBUS_MAI_WR_RS;
	wakemem();
	if(!apr.mc_stop)
		return mc_rs_t0;
	return NULL;
}

pulse(mc_addr_ack){
	printf("MC ADDR ACK\n");
	set_mc_rq(0);
	if(!apr.mc_rd && apr.mc_wr)
		return mc_wr_rs;
	return NULL;
}

pulse(mc_non_exist_rd){
	printf("MC NON EXIST RD\n");
	if(!apr.mc_stop)
		return mc_rs_t0;
	return NULL;
}

pulse(mc_non_exist_mem_rst){
	printf("MC NON EXIST MEM RST\n");
	if(apr.mc_rd){
		/* call directly - no other pulses after it in this case */
		mc_addr_ack();
		return mc_non_exist_rd;
	}else
		return mc_addr_ack();
	return NULL;
}

pulse(mc_non_exist_mem){
	printf("MC NON EXIST MEM\n");
	apr.cpa_non_exist_mem = 1;
	// TODO: IOB PI REQ CPA PIA
	if(!apr.sw_mem_disable)
		return mc_non_exist_mem_rst;
	return NULL;
}

pulse(mc_illeg_address){
	printf("MC ILLEG ADDRESS\n");
	apr.cpa_illeg_op = 1;
	// TODO: IOB PI REQ CPA PIA
	return st7;
}

pulse(mc_rq_pulse){
	printf("MC RQ PULSE\n");
	apr.mc_stop = 0;		// 7-9
	/* catch non-existent memory */
	apr.extpulse |= 4;
	if(calcaddr() == 0 && !apr.ex_inh_rel)
		return mc_illeg_address;
	set_mc_rq(1);
	if(apr.key_mem_stop || apr.ma == apr.mas && apr.sw_addr_stop){
		apr.mc_stop = 1;	// 7-9
		// TODO: what is this? does it make any sense?
		if(apr.key_mem_cont)
			return kt4;
	}
	return NULL;
}

pulse(mc_rdwr_rq_pulse){
	printf("MC RD/RW RQ PULSE\n");
	set_mc_rd(1);			// 7-9
	set_mc_wr(1);			// 7-9
	apr.mb = 0;
	apr.mc_stop_sync = 1;		// 7-9
	return mc_rq_pulse;
}

pulse(mc_rd_rq_pulse){
	printf("MC RD RQ PULSE\n");
	set_mc_rd(1);			// 7-9
	set_mc_wr(0);			// 7-9
	apr.mb = 0;
	return mc_rq_pulse;
}

pulse(mc_split_rd_rq){
	printf("MC SPLIT RD RQ\n");
	return mc_rd_rq_pulse;
}

pulse(mc_wr_rq_pulse){
	printf("MC WR RQ PULSE\n");
	set_mc_rd(0);			// 7-9
	set_mc_wr(1);			// 7-9
	return mc_rq_pulse;
}

pulse(mc_split_wr_rq){
	printf("MC SPLIT WR RQ\n");
	return mc_wr_rq_pulse;
}

pulse(mc_rd_wr_rs_pulse){
	printf("MC RD/WR RS PULSE\n");
	return apr.mc_split_cyc_sync ? mc_split_wr_rq : mc_wr_rs;
}

/*
 * Keys
 */

pulse(key_rd_wr_ret){
	printf("KEY RD WR RET\n");
	apr.key_rd_wr = 0;	// 5-2
//	apr.ex_ill_op = 0;	// ?? not found on 5-13
	return kt4;		// 5-2
}

pulse(key_rd){
	printf("KEY RD\n");
	apr.key_rd_wr = 1;	// 5-2
	apr.mc_rst1_ret = key_rd_wr_ret;
	return mc_rd_rq_pulse;
}

pulse(key_wr){
	printf("KEY WR\n");
	apr.key_rd_wr = 1;	// 5-2
	apr.mb = apr.ar;	// 6-3
	apr.mc_rst1_ret = key_rd_wr_ret;
	return mc_wr_rq_pulse;
}

pulse(key_go){
	printf("KEY GO\n");
	apr.run = 1;
	apr.key_ex_st = 0;
	apr.key_dep_st = 0;
	apr.key_ex_sync = 0;
	apr.key_dep_sync = 0;
	return it0;
}

pulse(kt4){
	printf("KT4\n");
	if(apr.run &&
	   (apr.key_ex || apr.key_ex_next || apr.key_dep || apr.key_dep_next))
		return key_go; // 5-2
	// TODO check repeat switch
	return NULL;
}

pulse(kt3){
	printf("KT3\n");
	if(apr.key_readin || apr.key_start)
		apr.pc = apr.ma;	// 5-12
	if(apr.key_execute && !apr.run){
		apr.mb = apr.ar;	// 6-3
		// TODO: go to KT4 to check repeat (when processor is idle)
		return it1a;		// 5-3
	}
	if(apr.key_ex || apr.key_ex_next)
		return key_rd;		// 5-2
	if(apr.key_dep || apr.key_dep_next)
		return key_wr;		// 5-2
	if(apr.key_start || apr.key_readin || apr.key_inst_cont)
		return key_go;		// 5-4
	return NULL;
}

#define KEY_MANUAL (apr.key_readin || apr.key_start || apr.key_inst_cont ||\
                    apr.key_mem_cont || apr.key_ex || apr.key_dep ||\
                    apr.key_ex_next || apr.key_dep_next || apr.key_execute ||\
                    apr.key_io_reset)

pulse(kt12){
	printf("KT1,2\n");
	if(apr.key_io_reset)
		mr_start();	// 5-2
	if(KEY_MANUAL && !apr.key_mem_cont)
		mr_clr();	// 5-2
	if(!(apr.key_readin || apr.key_inst_cont || apr.key_mem_cont))
		set_key_rim_sbr(0);	// 5-2

	if(apr.key_readin){
		set_key_rim_sbr(1);	// 5-2
		apr.ma = apr.mas;
	}
	if(apr.key_start)
		apr.ma = apr.mas;
	if(apr.key_execute && !apr.run)
		apr.ar = apr.data;
	if(apr.key_ex)
		apr.ma = apr.mas;
	if(apr.key_ex_next)
		apr.ma = (apr.ma+1)&RT;
	if(apr.key_dep){
		apr.ma = apr.mas;
		apr.ar = apr.data;
	}
	if(apr.key_dep_next){
		apr.ma = (apr.ma+1)&RT;
		apr.ar = apr.data;
	}

	if(apr.key_mem_cont && apr.mc_stop)
		return mc_rs_t0;
	if(KEY_MANUAL && apr.mc_stop && apr.mc_stop_sync && !apr.key_mem_cont){
		/* Finish rd/wr which should stop the processor.
		 * Set flag to continue at KT3 */
		apr.extpulse |= 2;
		return mc_wr_rs;
	}
	return kt3;	// 5-2
}

pulse(kt0a){
	printf("KT0A\n");
	apr.key_ex_st = 0;	// 5-1
	apr.key_dep_st = 0;	// 5-1
	apr.key_ex_sync = apr.key_ex || apr.key_ex_next;	// 5-1
	apr.key_dep_sync = apr.key_dep || apr.key_dep_next;	// 5-1
	if(!apr.run || apr.key_mem_cont)
		return kt12;	// 5-2
	return NULL;
}

pulse(kt0){
	printf("KT0\n");
	return kt0a;		// 5-2
}

pulse(key_manual){
	printf("KEY MANUAL\n");
	return kt0;		// 5-2
}

pulse(mai_addr_ack){
	printf("MAI ADDR ACK\n");
	return mc_addr_ack;
}

pulse(mai_rd_rs){
	printf("MAI RD RS\n");
	apr.mb = membus1;
	if(apr.ma == apr.mas)
		apr.mi = apr.mb;	// 7-7
	if(!apr.mc_stop)
		return mc_rs_t0;
	return NULL;
}

void*
aprmain(void *p)
{
	mr_pwr_clr();
	while(apr.sw_power){
		if(apr.extpulse & 1){
			if(apr.nextpulse)
				printf("whaa: cpu wasn't idle\n");
			apr.extpulse &= ~1;
			apr.nextpulse = key_manual;
		}
		if(apr.nextpulse)
			apr.nextpulse = apr.nextpulse();
		else if(membus0 & MEMBUS_MAI_ADDR_ACK){
			membus0 &= ~MEMBUS_MAI_ADDR_ACK;
			apr.extpulse &= ~4;
			apr.nextpulse = mai_addr_ack;
		}else if(membus0 & MEMBUS_MAI_RD_RS){
			membus0 &= ~MEMBUS_MAI_RD_RS;
			apr.nextpulse = mai_rd_rs;
		}else if(apr.extpulse & 2){
			/* continue at KT3 after finishing rd/wr from a stop */
			apr.extpulse &= ~2;
			apr.nextpulse = kt3;
		}else if(apr.extpulse & 4){
			apr.extpulse &= ~4;
			if(apr.mc_rq && !apr.mc_stop)
				apr.nextpulse = mc_non_exist_mem;
		}
	}
	printf("power off\n");
	return NULL;
}
