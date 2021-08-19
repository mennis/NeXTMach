/*	@(#)km.c	1.0	10/12/87	(c) 1987 NeXT	*/

/* 
 * HISTORY
 * 22-Mar-90  Mike Paquette (mpaque) at NeXT
 *	Reworked bitmap console support to be table driven, added support for
 *	configuring and loading the table from a ROM on a NextBus frame buffer,
 *	and added the ROM p-code interpreter to support initialization and control
 *	of frame buffers in a relatively flexible fashion.
 *
 * 06-Mar-90  Gregg Kellogg (gk) at NeXT
 *	changed csw_check() to csw_needed(thread, processor)
 *
 * 28-Feb-90  John Seamons (jks) at NeXT
 *	Set DMA limit register, not csr, to RETRACE_LIMIT to restart animation
 *	in kmrestore().
 *
 * 07-Jul-89	Doug Mitchell at NeXT
 *	Used KB_POLL_SETUP for MON_KM_POLL command.
 *
 * 21-Mar-89  John Seamons (jks) at NeXT
 *	Initialized cached value of brightness (curBright) and other fixes
 *	to be compatible with new special key autorepeat code in ev_kbd.c
 *
 * 12-Dec-88  Leo Hourvitz (leo) at NeXT
 *	Moved special_key_process into this file due to ev rewrite
 *
 *  3-Jun-88  Gregg Kellogg (gk) at NeXT
 *	Consolidated special key process in ev.c(special_key_process()).
 *
 * 14-Mar-88  Gregg Kellogg (gk) at NeXT
 *	Added support for sound keys.
 *
 * 17-Jan-88  John Seamons (jks) at NeXT
 *	Added autorepeat.
 *
 * 30-Dec-87  Leo Hourvitz (leo) at NeXT
 *	Transferring more stuff over to new ev driver.
 *
 * 29-Dec-87  Leo Hourvitz (leo) at NeXT
 *	Whack kmintr to call mouse_process on mouse events.
 *
 * 12-Oct-87  John Seamons (jks) at NeXT
 *	Created.
 *
 */ 

/*
 * TODO:
 *	- support arbitrary sized fonts?
 *	- poll for multiple devices on the kybd bus
 *	- shift lock w/ LEDs
 *	- audible bell
 */

#import <sys/types.h>
#import <sys/param.h>
#import <sys/user.h>
#import <sys/ioctl.h>
#import <sys/tty.h>
#import <sys/systm.h>
#import <sys/kernel.h>
#import <sys/buf.h>
#import <sys/uio.h>
#import <sys/proc.h>
#import <sys/conf.h>
#import <sys/msgbuf.h>
#import <sys/callout.h>
#import <kern/thread.h>
#import <sys/fcntl.h>
#import <sys/reboot.h>
#import <sys/syslog.h>
#import <kern/xpr.h>
#import <sys/printf.h>
#import <vm/vm_page.h>
#import <next/cpu.h>
#import <next/psl.h>
#import <next/scr.h>
#import <next/bmap.h>
#import <next/trap.h>
#import <next/eventc.h>
#import <next/cons.h>
#import <next/event_meter.h>
#import <nextdev/monreg.h>
#import <nextdev/kmreg.h>
#import <nextdev/video.h>
#import <nextdev/nbicreg.h>
#import <nextdev/keycodes.h>
#import <nextdev/ohlfs12.h>
#import <nextdev/td.h>
#import <nextdev/dma.h>
#import <nextdev/event.h>
#import <nextdev/ev_vars.h>
#import <nextdev/busvar.h>
#import <machine/spl.h>
#import <mon/global.h>
#import <vm/vm_kern.h>

#define	NKMCHAN		2	/* number of channels (keyboards & mice) */
#define	KYBD(unit)	(((unit) & 1) == 0)
#define	MOUSE(unit)	((unit) & 1)
#define	SCROLL_RATE	(hz/30)
#define	AUTORPT_INITIAL	(hz/2)	/* 1/2 sec initial delay */
#define	AUTORPT_SUBSEQ	(hz/20)	/* 20 cps autorepeat rate */
#define	TAB_SIZE	8

#define	NC_W_FULL	120	/* full window when nothing else on screen */
#define	NC_H_FULL	60
#define	NC_W_BIG	80	/* big window when requested */
#define	NC_H_BIG	40
#define	NC_W_NOM	50	/* nominal window that fits in off-screen memory */
#define	NC_H_NOM	15
#define	X_OVHD		3	/* = 2 for font8x10 */
#define	Y_OVHD		26
#define	Y_BOT		2
#define	TITLEBAR_OFF	-17
#define	WIN_SLOP	4
/* Get window size in bytes */
#define	WIN_SIZE(w, h)	(((((w) + X_OVHD) * sizeof (int) * CHAR_W) / KM_NPPW) * \
			((h) * CHAR_H + Y_OVHD + WIN_SLOP))
#define WIN_SIZE_NOM	WIN_SIZE(NC_W_NOM,NC_H_NOM)


int		kmstart(), kmoutput(), kmintr();
int		reconnect = 1, recon_poll, sound_active;
static int	km_access_stack = 0;
int		km_color[4];
char		*mach_title = "NeXT Mach Operating System";

kminit()
{
	struct nvram_info ni;

#if	XPR_DEBUG
	/*
	 * Temporarily set xprflags to search for the bug where the console
	 * window stays up.
	 */
	xprflags |= XPR_KM;   
#endif	XPR_DEBUG

	/* Probe and configure console device */
	km_select_console();

	kminit2();

	km.nc_w = NC_W_NOM;
	km.nc_h = NC_H_NOM;
	km.nc_lm = (KM_VIDEO_W - (km.nc_w * CHAR_W)) / 2 / CHAR_W;

	/* Set colors as specified by console device. */
	km_color[0] = KM_WHITE;
	km_color[1] = KM_LT_GRAY;
	km_color[2] = KM_DK_GRAY;
	km_color[3] = KM_BLACK;
	
	/* Leo 29Dec87 TEMP Call evinit routine for cursor */
	evinit();
	
	/* set cached value of brightness */
	nvram_check(&ni);
	curBright = ni.ni_brightness;
	km.flags |= KMF_INIT;
	km.save = 0;
	km.showcount = 0;
	km.store = 0;
	km.store2 = 0;
}

kminit2()
{
	int	i;

	/* monitor should have already setup video & brightness */
	km.bg = KM_WHITE;
	km.fg = KM_BLACK;
	km.x = km.y = 0;
	km.cp = &km.p[1];
	for (i = 0; i < KM_NP; i++)
		km.p[i] = 0;
}

kmopen (dev, flag)
	dev_t dev;
{
	register int unit;
	register struct tty *tp;

	/*
	 *  Allows single user shell to always have a window.
	 *  The O_POPUP flag is set by /etc/init when opening
	 *  /dev/console for the shell.  O_ALERT is used by
	 *  configuration programs that need to inform
	 *  the user of some configuration problems.
	 */
	if (flag & (O_POPUP|O_ALERT)) {
		XPR(XPR_KM, ("kmopen: flag 0x%x showcount %d\n", flag, km.showcount));
		if ((km.flags & KMF_SEE_MSGS) == 0) {
			/*
			 * We are the first to ask for a window. Bring
			 * it up, and make km.showcount non-zero so we
			 * know to take it away, when we close.
			 */
			XPR(XPR_KM, ("kmopen: SEE_MSGS == 0\n"));
			if (flag & O_POPUP) {
				kmpopup (mach_title, POPUP_NOM, 0, 0, 0);
				kmioctl (0, KMIOCDUMPLOG, 0, 0);
			} else {
				/*
				 * We only want to allow this for
				 * programs during system startup (eg.
				 * they must be run as root, and PostScript
				 * must not be running.
				 */
				if (!suser()) {
					return (EACCESS);
				}
				if (eventsOpen) {
					return (EBUSY);
				}
				alert_lock_screen (1);
				kmpopup (mach_title, POPUP_ALERT, 60, 8, 1);
			}
			km.showcount++;
			XPR(XPR_KM, ("kmopen: showcount1 %d\n", km.showcount));
		} else if (km.showcount) {
			/*
			 * Non-zero km.showcount means we are going to take
			 * the window away when everyone is done.  Bump it
			 * to account for us.
			 */
			km.showcount++;
			XPR(XPR_KM, ("kmopen: showcount2 %d\n", km.showcount));
		}
	}

#if	CS_TTYLOC
	static tlcinit = 0;

	/*
	 *  Initialize terminal locations on first call to driver.
	 */
	if (tlcinit == 0)
	{
		tlcinit++;
		for (unit=0; unit<(NKMCHAN>>1); unit++)
		{
			tp = &cons;
			tp->t_ttyloc.tlc_hostid = TLC_MYHOST;
			tp->t_ttyloc.tlc_ttyid = unit;
		}
	}
#endif	CS_TTYLOC

	/* can't do this with pseudo-inits because it's needed before then */
	if ((km.flags & KMF_INIT) == 0)
		kminit();
	unit = minor(dev);
	if (unit >= NKMCHAN)
		return (ENXIO);
	tp = &cons;
	tp->t_addr = 0;
	tp->t_oproc = kmstart;
	tp->t_line = NTTYDISC;
	if ((tp->t_state & TS_ISOPEN) == 0) {
		ttychars (tp);
                tp->t_flags = EVENP | ODDP | ECHO | CRMOD |
			CRTBS | CTLECH | CRTERA | CRTKIL | PRTERA;
		tp->t_chars.tc_erase = (char) 0x7f;
                tp->t_ispeed = tp->t_ospeed = B9600;
                tp->t_state = TS_ISOPEN | TS_CARR_ON;
	} else if ((tp->t_state&TS_XCLUDE) && u.u_uid != 0)
		return (EBUSY);
	return ((*linesw[tp->t_line].l_open)(dev, tp));
}

kmclose(dev, flag)
	dev_t dev;
{
	register struct tty *tp;
	register int unit;

	unit = minor(dev);
	tp = &cons;
	(*linesw[tp->t_line].l_close)(tp);
	ttyclose(tp);
	return (0);
}
 
kmread(dev, uio)
	dev_t dev;
	struct uio *uio;
{
	register struct tty *tp;
 
	tp = &cons;
	return ((*linesw[tp->t_line].l_read)(tp, uio));
}
 
kmwrite(dev, uio)
	dev_t dev;
	struct uio *uio;
{
	register struct tty *tp;
 
	tp = &cons;
	return ((*linesw[tp->t_line].l_write)(tp, uio));
}

kmselect(dev, rw)
	dev_t dev;
	int rw;
{
	struct tty *tp;
 
	tp = &cons;
	return ((*linesw[tp->t_line].l_select)(tp, rw));
}

kmstart(tp)
        register struct tty *tp;
{
	register int c;
	register int cc = 0;

	if (tp->t_state & (TS_TIMEOUT | TS_BUSY | TS_TTSTOP))
		goto out;
	if (tp->t_outq.c_cc == 0)
		goto out;
	tp->t_state |= TS_BUSY;
	if (tp->t_outq.c_cc > TTLOWAT(tp))
		softint_sched (0, kmoutput, tp);
	else
		timeout (kmoutput, tp, SCROLL_RATE);
	return;
out:
	if (tp->t_outq.c_cc <= TTLOWAT(tp)) {
		if (tp->t_state & TS_ASLEEP) {
			tp->t_state &= ~TS_ASLEEP;
			wakeup((caddr_t) &tp->t_outq);
		}
		if (tp->t_wsel) {
			selwakeup(tp->t_wsel, tp->t_state & TS_WCOLL);
			tp->t_wsel = 0;
			tp->t_state &= ~TS_WCOLL;
		}
	}
}

kmoutput (tp)
	register struct tty *tp;
{
	char buf[80];
	register char *cp;
	register int cc = -1;
	extern int ttrstrt();

	while (tp->t_outq.c_cc > 0) {
		if ((tp->t_flags&(RAW|LITOUT)))
			cc = ndqb(&tp->t_outq, 0);
		else
			cc = ndqb(&tp->t_outq, 0200);
		if (cc == 0)
			break;
		cc = MIN(cc, sizeof buf);
		(void) q_to_b(&tp->t_outq, buf, cc);
		for (cp = buf; cp < &buf[cc]; cp++)
			kmpaint (*cp & 0x7f);
	}
        if (cc == 0) {
                cc = getc(&tp->t_outq);
                timeout(ttrstrt, (caddr_t)tp, cc&0x7f);
                tp->t_state |= TS_TIMEOUT;
        } else if (tp->t_outq.c_cc > 0)
		softint_sched (0, kmoutput, tp);
	tp->t_state &= ~TS_BUSY;
        if (tp->t_outq.c_cc <= TTLOWAT(tp)) {
		if (tp->t_state & TS_ASLEEP) {
			tp->t_state &= ~TS_ASLEEP;
			wakeup ((caddr_t) &tp->t_outq);
		}
		if (tp->t_wsel) {
			selwakeup (tp->t_wsel, tp->t_state & TS_WCOLL);
			tp->t_wsel = 0;
			tp->t_state &= ~TS_WCOLL;
		}
	}
}

kmpopup (title, flag, w, h, allocmem)
	char		*title;
{
	register int vmem, sp, dp, x, y;
	int bg, bg2, bg3, i;
	char c;
	int text_bg, text_fg, title_bg, title_fg,
		title_l, title_r, title_t, title_b;
	extern struct mon_global *mon_global;
	struct mon_global *mg = mon_global;

	/* save previous cons_tp & KMF_SEE_MSGS */
	km.save_flags[km.save] = km.flags;
	km.save_tty[km.save] = cons_tp;
	km.save_flags[km.save] |= (*MG (char*, MG_flags) &
			MGF_ANIM_RUN)? KMF_ANIM_RUN : 0;
	XPR(XPR_KM, ("kmpopup: save %d: flags 0x%x tty 0x%x\n", km.save,
		km.flags, cons_tp));
	km.save++;

	/* stop any ongoing animation */
	if (*MG (char*, MG_flags) & MGF_ANIM_RUN) {
		vidSuspendAnimation();
	}
		
	/* allocate popup window backing store (if any) */
	if (!eventsOpen && !allocmem &&
	    !(km.save_flags[km.save-1] & KMF_ANIM_RUN)) {
		km.nc_w = w? w : NC_W_FULL;
		km.nc_h = h? h : NC_H_FULL;
		km.store = 0;
	} else if (km.flags & KMF_PERMANENT) {
		/* already setup */
		if (km.store == 0) {
			/* but someone popped up a non-stored window (above) */
			km.store = km.store2;
		}
		km.nc_w = MIN(km.nc_w2, w? w : NC_W_BIG);
		km.nc_h = MIN(km.nc_h2, h? h : NC_H_BIG);
	} else {
		km.nc_w = w? w : NC_W_NOM;
		km.nc_h = h? h : NC_H_NOM;
		i = WIN_SIZE(km.nc_w, km.nc_h);
		if (i > KM_BACKING_SIZE && mb_map && (km.store =
					kmem_mb_alloc (mb_map, i))) {
			/* Save the parameters of the permanent memory */
			km.store2 = km.store;
			km.nc_w2 = km.nc_w;
			km.nc_h2 = km.nc_h;
			km.flags |= KMF_PERMANENT;
		} else {
			/* Shrink win to fit backing store in offscreen 2 bit VRAM */
			if (i > KM_BACKING_SIZE) {
			    int bytes_per_line;
			    
			    /* heuristic tweak to shrink width, too */
			    if ( i > (3 * KM_BACKING_SIZE) )
			    	km.nc_w = (km.nc_w * 3) / 5;
			    bytes_per_line = ((km.nc_w + X_OVHD)*CHAR_W*sizeof (int))/
			    			KM_NPPW;    /* bytes per scan frag */
			    bytes_per_line *= CHAR_H;	    /* bytes per char line */
			    km.nc_h = KM_BACKING_SIZE / bytes_per_line;
			}
			km.store = KM_BACKING_STORE;
		}
	}
	km.nc_lm = (KM_VIDEO_W - (km.nc_w * CHAR_W)) /
		2 / CHAR_W;
	km.nc_tm = (KM_VIDEO_H - (km.nc_h * CHAR_H - Y_OVHD)) /
		2 / CHAR_H;
	km.flags |= KMF_SEE_MSGS;
	cons_tp = &cons;

	/* save obscured bits, create new window */
	if (flag == POPUP_ALERT) {
		text_bg = KM_LT_GRAY;
		text_fg = KM_BLACK;
		title_bg = KM_BLACK;
		title_fg = KM_WHITE;
		title_l = KM_LT_GRAY;
		title_r = KM_DK_GRAY;
		title_t = KM_LT_GRAY;
		title_b = KM_DK_GRAY;
	} else {	/* POPUP_NOM, POPUP_FULL */
		text_bg = KM_WHITE;
		text_fg = KM_BLACK;
		title_bg = KM_BLACK;
		title_fg = KM_WHITE;
		title_l = KM_LT_GRAY;
		title_r = KM_DK_GRAY;
		title_t = KM_LT_GRAY;
		title_b = KM_DK_GRAY;
	}
	km_begin_access();
	if (flag == POPUP_FULL)
		km_clear_screen();
	/* Indent to left margin, row 0, less 1/2 of X_OVHD characters. */
	vmem = KM_P_VIDEOMEM + (km.nc_lm * CHAR_W * sizeof(int) / KM_NPPW) - 
	       (X_OVHD * CHAR_W * sizeof(int) / (KM_NPPW * 2) );
	/* vmem = KM_P_VIDEOMEM + (km.nc_lm << 1) - X_OVHD; */
	dp = km.store;
	for (y = 0; y < Y_OVHD + km.nc_h * CHAR_H; y++) {
		/* Set color scheme for this line. */
		if (y == 0 || y == 22 ||	/* Top and bottom of titlebar */
		    y == Y_OVHD + km.nc_h * CHAR_H - 1)	/* Bottom of window */
			bg = bg2 = bg3 = KM_BLACK;
		else
			if (y >= 2 && y <= 20)
				bg = title_bg, bg2 = title_l, bg3 = title_r;
			else if (y == 1)
				bg = bg2 = bg3 = title_t;
			else if (y == 21)
				bg = bg2 = bg3 = title_b;
			else
				bg = bg2 = bg3 = text_bg;
		/* Set start of scanline. */
		sp = vmem + (km.nc_tm * CHAR_H + y - Y_OVHD + Y_BOT) *
			KM_VIDEO_NBPL;
		/*
		 * Fill in a scanline.  Skip the first 6 pixels, set the 7th
		 * pixel to black, the 8th pixel to bg2, and most of the rest
		 * to bg.  Set the 2nd to last pixel to bg3, and the last to black.
		 *
		 * This is pretty horrible code.
		 */
		switch ( KM_NPPW )	/* Switch on the number of pixels per word */
		{
		    case 16:	/* 2 bit pixels */
			if (dp)
				*((int*)dp)++ = *(int*)sp;
			*(int*)sp =  *(int*)sp & 0xfff00000 | (0x000c0000 & KM_BLACK) |
				(bg & 0x0000ffff) | (bg2 & 0x00030000);
			((int*)sp)++;
			for (x = 1; x < (((km.nc_w + X_OVHD) >> 1) +
					 (X_OVHD & 1)) - 1; x++) {
				if (dp)
					*((int*)dp)++ = *(int*)sp;
				*((int*)sp)++ = bg;
			}
			if (dp)
				*((int*)dp)++ = *(int*)sp;
#if	(X_OVHD & 1)
			*(int*)sp =  *(int*)sp & 0x0fffffff | (0x30000000 & KM_BLACK) |
				(bg3 & 0xc0000000);
#else
			*(int*)sp =  *(int*)sp & 0x00000fff | (0x00003000 & KM_BLACK) |
				(bg & 0xffff0000) | (bg3 & 0x0000c000);
#endif
			((int*)sp)++;
			break;
		    case 4:	/* Byte size pixels */
		    	((char*)sp) += 7;	/* Bump into our X_OVHD margin */
		    	if ( dp ){
				*((char*)dp)++ = *((char*)sp);
				*((char*)sp)++ = KM_BLACK & 0xFF; /* Pixel 7 */
				*((char*)dp)++ = *((char*)sp);
				*((char*)sp)++ = bg2 & 0xFF;	  /* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i ){
					*((char*)dp)++ = *(char*)sp;
					*((char*)sp)++ = bg & 0xFF;
				}
				*((char*)dp)++ = *((char*)sp);
				*((char*)sp)++ = bg3 & 0xFF; 	 /* 2nd to last */
				*((char*)dp)++ = *((char*)sp);
				*((char*)sp)++ = KM_BLACK & 0xFF; /* last pixel */
			} else {
				*((char*)sp)++ = KM_BLACK & 0xFF; /* Pixel 7 */
				*((char*)sp)++ = bg2 & 0xFF;	  /* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i )
					*((char*)sp)++ = bg & 0xFF;
				*((char*)sp)++ = bg3 & 0xFF;	 /* 2nd to last */
				*((char*)sp)++ = KM_BLACK & 0xFF; /* last pixel */
			}
			break;
		    case 2:	/* Word size pixels */
		    	((short*)sp) += 7;	/* Bump into our X_OVHD margin */
		    	if ( dp ){
				*((short*)dp)++ = *((short*)sp);
				*((short*)sp)++ = KM_BLACK & 0xFFFF; /* Pixel 7 */
				*((short*)dp)++ = *((short*)sp);
				*((short*)sp)++ = bg2 & 0xFFFF;	  /* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i ){
					*((short*)dp)++ = *(short*)sp;
					*((short*)sp)++ = bg & 0xFFFF;
				}
				*((short*)dp)++ = *((short*)sp);
				*((short*)sp)++ = bg3 & 0xFFFF;      /* 2nd to last */
				*((short*)dp)++ = *((short*)sp);
				*((short*)sp)++ = KM_BLACK & 0xFFFF; /* last pixel */
			} else {
				*((short*)sp)++ = KM_BLACK & 0xFFFF; /* Pixel 7 */
				*((short*)sp)++ = bg2 & 0xFFFF;	     /* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i )
					*((short*)sp)++ = bg & 0xFFFF;
				*((short*)sp)++ = bg3 & 0xFFFF;	     /* 2nd to last */
				*((short*)sp)++ = KM_BLACK & 0xFFFF; /* last pixel */
			}
			break;
		    case 1:	/* longword sized pixels */
		    	((int*)sp) += 7;	/* Bump into our X_OVHD margin */
		    	if ( dp ){
				*((int*)dp)++ = *((int*)sp);
				*((int*)sp)++ = KM_BLACK;	/* Pixel 7 */
				*((int*)dp)++ = *((int*)sp);
				*((int*)sp)++ = bg2;		/* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i ){
					*((int*)dp)++ = *(int*)sp;
					*((int*)sp)++ = bg;
				}
				*((int*)dp)++ = *((int*)sp);
				*((int*)sp)++ = bg3;		/* 2nd to last */
				*((int*)dp)++ = *((int*)sp);
				*((int*)sp)++ = KM_BLACK;	/* last pixel */
			} else {
				*((int*)sp)++ = KM_BLACK;	/* Pixel 7 */
				*((int*)sp)++ = bg2;		/* Pixel 8 */
				for ( i = 9; i < ((km.nc_w + X_OVHD)*CHAR_W)-9; ++i )
					*((int*)sp)++ = bg;
				*((int*)sp)++ = bg3;		/* 2nd to last */
				*((int*)sp)++ = KM_BLACK;	/* last pixel */
			}
		    	break;
		}
	}
	km.bg = title_bg;
	km.fg = title_fg;
	km.x = km.nc_w / 2 - strlen (title) / 2;
	km.y = TITLEBAR_OFF;
	for (i = 0; c = ((char*)title)[i]; i++)	
		kmpaint (c);
	km_flip_cursor();
	km_end_access();
	km.bg = text_bg;
	km.fg = text_fg;
	km.x = km.y = 0;
	km.flags &= ~KMF_CURSOR;
	return (0);
}

/*
 *  Called from softint_run() to allocate enough memory for a big window
 *  because memory can't be allocated within the NMI routine.
 */
km_big() {
	int i;
	
	if (km.flags & KMF_PERMANENT)
		return;
	km.nc_w2 = NC_W_BIG;
	km.nc_h2 = NC_H_BIG;
	i = WIN_SIZE(km.nc_w2, km.nc_h2);
	km.store2 = kmem_alloc(kernel_map, i);
	km.flags |= KMF_PERMANENT;
	km.store = 0;
}

kmrestore()
{
	register int vmem, dp, sp, x, y;
	extern struct mon_global *mon_global;
	struct mon_global *mg = mon_global;

	if ((km.flags & KMF_SEE_MSGS) == 0)
		return (0);

	/* restore obscured bits */
	if (km.store) {
		km_begin_access();
		/* Indent to left margin, row 0, less 1/2 of X_OVHD characters. */
		vmem = KM_P_VIDEOMEM + (km.nc_lm * CHAR_W * sizeof(int) / KM_NPPW) - 
		       (X_OVHD * CHAR_W * sizeof(int) / (KM_NPPW * 2) );
		sp = km.store;

		for (y = 0; y < Y_OVHD + km.nc_h * CHAR_H; y++) {
			/* Set start of scanline. */
			dp = vmem + (km.nc_tm * CHAR_H + y - Y_OVHD + Y_BOT) *
				KM_VIDEO_NBPL;
			switch( KM_NPPW )  /* Switch on number of pixels per word. */
			{
			    case 16:	/* 2 bit pixels */
				for (x = 0; x < (((km.nc_w + X_OVHD) >> 1) +
						 (X_OVHD & 1)); x++)
					*((int*)dp)++ = *((int*)sp)++;
				break;
			    case 4:	/* 8 bit pixels */
			    	((char*)dp) += 7;    /* Bump into our X_OVHD margin */
				for ( x = 7; x < ((km.nc_w + X_OVHD)*CHAR_W)-7; ++x )
					*((char*)dp)++ = *((char*)sp)++;
			    	break;
			    case 2:	/* 16 bit pixels */
			    	((short*)dp) += 7;    /* Bump into our X_OVHD margin */
				for ( x = 7; x < ((km.nc_w + X_OVHD)*CHAR_W)-7; ++x )
					*((short*)dp)++ = *((short*)sp)++;
			    	break;
			    case 1:	/* 32 bit pixels */
			    	((int*)dp) += 7;    /* Bump into our X_OVHD margin */
				for ( x = 7; x < ((km.nc_w + X_OVHD)*CHAR_W)-7; ++x )
					*((int*)dp)++ = *((int*)sp)++;
			        break;
			}
		}
		km_end_access();
	}

	/* restore previous cons_tp & KMF_SEE_MSGS */
	km.save--;
	XPR(XPR_KM, ("kmrestore: save %d: flags 0x%x tty 0x%x\n", km.save,
		km.flags, cons_tp));
	cons_tp = km.save_tty[km.save];
	km.flags &= ~KMF_SEE_MSGS;
	km.flags |= km.save_flags[km.save] & KMF_SEE_MSGS;
	if (km.save_flags[km.save] & KMF_ANIM_RUN)
		vidResumeAnimation();
	return (0);
}

kmioctl (dev, cmd, data, flag)
	dev_t dev;
	caddr_t data;
{
	int unit, error;
	char *mp;
	register struct tty *tp;

	unit = minor(dev);
	tp = &cons;
	switch (cmd) {

	case KMIOSLEDSTATES:
		km_send(MON_KM_USRREQ, KM_SET_LEDS(0, *(int *)data));
		return(0);
	case KMIOCRESTORE:
		/*
		 * Remove the popup window.  Non-zero km.showcount
		 * means that we are to restore the screen when all
		 * alerts and pop-ups have closed.
		 */
		XPR(XPR_KM, ("KMIOCRESTORE: showcount %d flag 0x%x\n",
			km.showcount, flag));
		if (km.showcount && (flag & (O_POPUP|O_ALERT)) &&
		    --km.showcount == 0) {
			kmrestore();
			alert_lock_screen (0);
		}
		return(0);
	
	case KMIOCDUMPLOG:

		/* FIXME: just print last km.nc_h lines? */
		if (pmsgbuf->msg_magic != MSG_MAGIC)
			return (0);
		mp = &pmsgbuf->msg_bufc[pmsgbuf->msg_bufx];
		do {
			if (*mp) {
				if (*mp == '\n')
					kmpaint ('\r');
				kmpaint (*mp);
			}
			if (++mp >= &pmsgbuf->msg_bufc[MSG_BSIZE])
				mp = pmsgbuf->msg_bufc;
		} while (mp != &pmsgbuf->msg_bufc[pmsgbuf->msg_bufx]);
		return (0);
		
	case KMIOCGFLAGS:
		if (!suser())
			return (EACCESS);
		*(int*)data = km.flags;
		return (0);
	
	/* Destructively image a rectangle onto the screen. */
	case KMIOCDRAWRECT:
		return( km_drawrect( (struct km_drawrect *)data ) );
	/* Clear a rectangle on the screen. */
	case KMIOCERASERECT:
		return( km_eraserect( (struct km_drawrect *)data ) );
	}

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag);
	if (error >= 0)
		return (error);
	error = ttioctl (tp, cmd, data, flag);
	if (error >= 0)
		return (error);
	return (ENOTTY);
}

/* l_rint can be slower than kybd events, causing overruns, so use softints */
km_input (c)
{
	register struct tty *tp = &cons;

	(*linesw[tp->t_line].l_rint) (c, tp);
}

km_autorepeat()
{
	register int c;

	if (km.flags & KMF_AUTOREPEAT) {
		c = kybd_process (&km.autorepeat_event);
        	if ((cons.t_state & TS_ISOPEN) && c < inv)
			softint_sched (0, km_input, c);
		timeout (km_autorepeat, 0, AUTORPT_SUBSEQ);
	}
}

reconpoll()
{
	/* repoll monitor in case it has just been plugged back in */
	if (!sound_active)
		mon_send (MON_KM_POLL, KB_POLL_SETUP);
	timeout (reconpoll, 0, hz*3);
}

#ifdef NOEVDRIVER
kmintr()
{
	register volatile struct monitor *mon =
		(volatile struct monitor*) P_MON;
	register union kybd_event *ke = &km.kybd_event;
	register int c, dev;
	struct mon_status csr;

	csr = mon->mon_csr;
	ke->data = mon->mon_km_data;	/* clears interrupt */

	/* reenable keyboard if it is not responding */
	if ((curEvent.data & MON_NO_RESP) &&
	    (curEvent.data & MON_MASTER) == 0 && !sound_active && reconnect) {
		km_send (MON_KM_USRREQ, KM_SET_ADRS(0));
		return;
	}

	dev = MON_DEV_ADRS (ke->data);
	if (csr.km_dav == 0) {
		printf ("spurious kybd/mouse interrupt\n");
		return;
	}
	if (csr.km_ovr) {
		mon->mon_csr.km_ovr = 1;		/* clear it */
	}
	if (dev & 1)		/* odd device addresses are mice */
	{
		mouse_process(&km.kybd_event);
		return;
	}
	if (km.flags & KMF_AUTOREPEAT) {
		km.flags &= ~KMF_AUTOREPEAT;
		untimeout (km_autorepeat, 0);
	}
	if ((c = kybd_process (&km.kybd_event)) == inv)
		return;
	if (ke->k.up_down == KM_DOWN) {
		km.autorepeat_event = km.kybd_event;
		km.flags |= KMF_AUTOREPEAT;
		timeout (km_autorepeat, 0, AUTORPT_INITIAL);
	}
        if ((cons.t_state & TS_ISOPEN) && c < inv)
		softint_sched (0, km_input, c);
	if (!recon_poll && reconnect && callfree) {
		timeout (reconpoll, 0, hz*3);
		recon_poll = 1;
	}
}
#else NOEVDRIVER
kmintr_process(ke)
register union kybd_event *ke;
{
	register int c;

	if (km.flags & KMF_AUTOREPEAT) {
		km.flags &= ~KMF_AUTOREPEAT;
		untimeout (km_autorepeat, 0);
	}
	if ((c = kybd_process (ke)) == inv)
		return;

	/* check for key event during alert popups */
	if ((ke->k.up_down == KM_DOWN) && (km.flags & KMF_ALERT_KEY)) {
		alert_key = c;
		return;
	}
	if (ke->k.up_down == KM_DOWN) {
		km.autorepeat_event = *ke;
		km.flags |= KMF_AUTOREPEAT;
		timeout (km_autorepeat, 0, AUTORPT_INITIAL);
	}
        if ((cons.t_state & TS_ISOPEN) && c < inv)
		softint_sched (0, km_input, c);
} /* kmintr_process */
#endif NOEVDRIVER

kmputc (dev, c)
	dev_t dev;
	register int c;
{
	/* unseen kernel msgs will be caught in message log via logchar() */
	if ((km.flags & KMF_SEE_MSGS) == 0)
		return;
	if ((km.flags & KMF_INIT) == 0)
		kminit();
	if (c == '\n')
		kmpaint ('\r');
	kmpaint (c);
}

kmgetc (dev)
	dev_t dev;
{
	register volatile struct monitor *mon =
		(volatile struct monitor*) P_MON;
	register int s, c;
	struct mon_status csr;

	/*
	 *  Make sure we loop at or above the console interrupt priority
	 *  otherwise we can get into an infinite loop processing console
	 *  receive interrupts which will never be handled because the
	 *  interrupt routine is short-circuited while we are doing direct
	 *  input.
	 */
	s = splhigh();
	do {
		do {
			csr = mon->mon_csr;
		} while (csr.km_dav == 0);
		km.kybd_event.data = mon->mon_km_data;
		c = kybd_process (&km.kybd_event);
	} while (c >= inv);
	(void) splx(s);
	if (c == '\r')
		c = '\n';
	cnputc (c);
	return (c);
}

kmtrygetc()
{
	register volatile struct monitor *mon =
		(volatile struct monitor*) P_MON;
	register int c;
	struct mon_status csr;

	csr = mon->mon_csr;
	if (csr.km_dav == 0)
		return (-1);
	km.kybd_event.data = mon->mon_km_data;
	c = kybd_process (&km.kybd_event);
	if (c >= inv)
		return (-1);
	return (c);
}

static void special_key_process(data, keyup)
int data;
{
    union kybd_event ke;
    
    ke.data = data;
    /* Have to synthesize the flags argument for DoSpecialKey(from ev_kbd.c) */
    DoSpecialKey(ke.k.key_code, !keyup,
    	(ke.k.alt_right ? (NX_ALTERNATEMASK|NX_NEXTLALTKEYMASK) : 0) |
    	(ke.k.alt_left ? (NX_ALTERNATEMASK|NX_NEXTRALTKEYMASK) : 0) |
    	(ke.k.command_right ? (NX_COMMANDMASK|NX_NEXTRCMDKEYMASK) : 0) |
    	(ke.k.command_left ? (NX_COMMANDMASK|NX_NEXTLCMDKEYMASK) : 0) |
    	(ke.k.shift_right ? (NX_SHIFTMASK|NX_NEXTRSHIFTKEYMASK) : 0) |
    	(ke.k.shift_left ? (NX_SHIFTMASK|NX_NEXTLSHIFTKEYMASK) : 0) |
    	(ke.k.control ? (NX_CONTROLMASK|NX_NEXTCTLKEYMASK) : 0) );
} /* special_key_process */


kybd_process (ke)
	register union kybd_event *ke;
{
	register int shift, control, alt, command, both_command, c, keyup;
	int level;
	struct nvram_info ni;

	/* ignore invalid events */
	if (ke->k.valid == 0 || MON_DEV_ADRS (ke->data))
		return (inv);
	keyup = ke->k.up_down == KM_UP;
	shift = ke->k.shift_right || ke->k.shift_left;
	control = ke->k.control? 162 : 0;
	c = ascii[((ke->k.key_code << 1) | shift) + control];
	alt = (ke->k.alt_right || ke->k.alt_left)? 0x80 : 0;
	command = (ke->k.command_right || ke->k.command_left)? 1 : 0;
	both_command = (ke->k.command_right && ke->k.command_left)? 1 : 0;

	switch (c) {

	case dim:
	case bright:
	case loud:
	case quiet:
		special_key_process (ke->data, keyup);
		break;

	case up:
#if	EVENTMETER
		if (!keyup && both_command)
			event_run (EM_KEY_UP, shift);
#endif	EVENTMETER
		break;

	case down:
#if	EVENTMETER
		if (!keyup && both_command)
			event_run (EM_KEY_DOWN, shift);
#endif	EVENTMETER
		break;

	default:
		c |= alt;
		break;
	}

	/* ignore up events */
	if (keyup)
		return (inv);
	return (c);
}

/* send a command to the keyboard/mouse bus and wait for reply packet */
km_send (mon_cmd, cmd)
{
	register volatile struct monitor *mon =
		(volatile struct monitor*) P_MON;
	register int i = 100000;

	mon_send (mon_cmd, cmd);

	/* wait for reply packet */
	while (i && mon->mon_csr.km_dav == 0)
		i--;
	if (i == 0)
		return (MON_NO_RESP);
	return (0);
}

/*
 * Given a 2 bit raster, convert it to the format appropriate for the 
 * display.
 *
 * The co-ordinates passed in by these ioctls assume an 1132x832 screen
 *(VIDEO_WxVIDEO_H).  All screen sizes are assumed to share a common center point.
 * We scale the X and Y values to maintain a constant offset from the center point,
 * so that boot animations, popup windows, and related goodies don't break with
 * screens larger or smaller than the 'default' screen.
 */
km_drawrect( rect )
    struct km_drawrect *rect;
{
	int pixel;	/* Starting pixel in screen */
	int i, x, y;
	unsigned char *data;
	unsigned char *buffer;
	int *table = &km_coni.color[0];
	unsigned char value;
	long size;
	
	/* Correct the x and y values for screen size */
	rect->x += (KM_VIDEO_W - VIDEO_W)/2;
	rect->y += (KM_VIDEO_H - VIDEO_H)/2;

	rect->x &= ~3;	/* Trunc to 4 pixel bound */
	rect->width = (rect->width + 3) & ~3;	/* Round up to 4 pixel bound */
	/* Sanity check */
	if((rect->x + rect->width > KM_VIDEO_W)||(rect->y + rect->height) > KM_VIDEO_H)
		return( EINVAL );
	pixel = rect->x + (rect->y * KM_VIDEO_MW);
	size = (rect->width >> 2) * rect->height;	/* size in bytes. */
	if ( size <= 0 )
		return( EINVAL );
	data = (unsigned char *)kalloc( size );
	buffer = data;
	if (copyin(rect->data.bits, data, size))
	{
	    kfree( data, size );
	    return(EINVAL);
	}
	
	switch ( KM_NPPW )
	{
	    case 16:	/* 2 bit pixels */
	    {
	    	unsigned char *fb_start = (unsigned char *)(KM_P_VIDEOMEM+(pixel>>2));
		unsigned char *fb;
		unsigned char final_value;
		
		for ( y = 0; y < rect->height; ++y ) {
			fb = fb_start;
			for ( x = 0; x < rect->width; x += 4) {
				value = *data++;
				final_value = 0;
				for ( i = 0; i < 8; i += 2 ) {
				    final_value |= ((table[(value>>i)&3] & 3) << i);
				}
				*fb++ = final_value;
			}
			fb_start += KM_VIDEO_NBPL;
		}
	    	break;
	    }
	    case 4:	/* 8 bit pixels */
	    {
	    	unsigned char *fb_start = (unsigned char *) KM_P_VIDEOMEM;
		unsigned char *fb;
		fb_start += pixel;
		for ( y = 0; y < rect->height; ++y ) {
			fb = fb_start;
			for ( x = 0; x < rect->width; x += 4) {
				value = *data++;
				*fb++ = table[(value >> 6) & 3];
				*fb++ = table[(value >> 4) & 3];
				*fb++ = table[(value >> 2) & 3];
				*fb++ = table[value & 3];
			}
			fb_start += KM_VIDEO_NBPL;
		}	    	
	    	break;
	    }
	    case 2:	/* 16 bit pixels */
	    {
	    	unsigned short *fb_start = (unsigned short *) KM_P_VIDEOMEM ;
		unsigned short *fb;
		fb_start += pixel;
		for ( y = 0; y < rect->height; ++y ) {
			fb = fb_start;
			for ( x = 0; x < rect->width; x += 4) {
				value = *data++;
				*fb++ = table[(value >> 6) & 3];
				*fb++ = table[(value >> 4) & 3];
				*fb++ = table[(value >> 2) & 3];
				*fb++ = table[value & 3];
			}
			fb_start = (unsigned short *)(((char*)fb_start)+KM_VIDEO_NBPL);
		}	    	
	    	break;
	    }
	    case 1:	/* 32 bit pixels */
	    {
	    	unsigned int *fb_start = (unsigned int *) KM_P_VIDEOMEM ;
		unsigned int *fb;
		fb_start += pixel;
		for ( y = 0; y < rect->height; ++y ) {
			fb = fb_start;
			for ( x = 0; x < rect->width; x += 4) {
				value = *data++;
				*fb++ = table[(value >> 6) & 3];
				*fb++ = table[(value >> 4) & 3];
				*fb++ = table[(value >> 2) & 3];
				*fb++ = table[value & 3];
			}
			fb_start = (unsigned int *)(((char*)fb_start) + KM_VIDEO_NBPL);
		}	    	
	    	break;
	    }
	}
	kfree( buffer, size );
	return 0;
}

km_eraserect( rect )
    struct km_drawrect *rect;
{
	int pixel;	/* Starting pixel in screen */
	int i, x, y;
	unsigned int value = km_coni.color[rect->data.fill & 3];
	
	/* Correct the x and y values for screen size */
	rect->x += (KM_VIDEO_W - VIDEO_W)/2;
	rect->y += (KM_VIDEO_H - VIDEO_H)/2;

	rect->x &= ~3;	/* Trunc to 4 pixel bound */
	rect->width = (rect->width + 3) & ~3;	/* Round up to 4 pixel bound */
	/* Sanity check */
	if((rect->x + rect->width > KM_VIDEO_W)||(rect->y + rect->height) > KM_VIDEO_H)
		return( EINVAL );
	pixel = rect->x + (rect->y * KM_VIDEO_MW);
	if ( KM_NPPW == 16 ) { /* 2 bit frame buffer */
	    unsigned char *start_fb = (unsigned char *)(KM_P_VIDEOMEM + (pixel>>2));
	    unsigned char *fb;
	    for ( y = 0; y < rect->height; ++y ) {
	    	fb = start_fb;
		for ( x = 0; x < rect->width; x += 4 )
			*fb++ = value;
		start_fb += KM_VIDEO_NBPL;
	    }
	} else {
	    unsigned int *start_fb = (unsigned int *)(KM_P_VIDEOMEM + pixel);
	    unsigned int *fb;
	    for ( y = 0; y < rect->height; ++y ) {
	    	fb = start_fb;
		for ( x = 0; x < rect->width; x += KM_NPPW )
			*fb++ = value;
		start_fb = (unsigned int *)((char *)start_fb + KM_VIDEO_NBPL);
	    }
	}
	return 0;
}

km_clear_win()
{
	register int vmem;
	register int i, x, y;

	/* clear screen */
	km_begin_access();
	for (y = CHAR_H*km.nc_tm; y < CHAR_H*km.nc_tm + CHAR_H*km.nc_h; y++) {
		/* Place vmem at left end of scanline to be cleared. */
		vmem = KM_P_VIDEOMEM + (y * KM_VIDEO_NBPL) + 
			((km.nc_lm + km.x) * CHAR_W * sizeof (int))/KM_NPPW;
		for(x = (km.nc_lm + km.x) * CHAR_W;
		    x < ((km.nc_lm + km.nc_w) * CHAR_W);
		    x += KM_NPPW ) {
			*((int *)vmem)++ = km.bg;
		}
	}
	km_end_access();
}

km_clear_screen()
{
	register int i;
	register int * vmem = (int *)KM_P_VIDEOMEM;
	register int size;
	
	km_begin_access();
	size = (KM_VIDEO_NBPL * KM_VIDEO_H) / sizeof(int);
	for ( i = 0; i < size; ++i )
		*vmem++ = KM_DK_GRAY;
	km_end_access();
}

kmpaint (c)
	register u_char c;
{
	register int i, j, y, vp, vmem;
	int longs, repeat, pattern, bpl, chunk;
	register int *s, *d;

	/* catch unseen user output in message log */
	if ((km.flags & KMF_SEE_MSGS) == 0) {
		putchar (c);	/* This winds up going TOLOG */
		return;
	}
	if (km.ansi == 1 && c == '[') {
		km.ansi = 2;
		return;
	}
	
	km_begin_access();
	if (km.ansi == 2) {
		if (c >= '0' && c <= '9') {
			*km.cp = *km.cp * 10 + (c - '0');
			km_end_access();
			return;
		} else
		if (c == ';') {
			if (km.cp < &km.p[KM_NP])
				km.cp++;
			km_end_access();
			return;
		} else {
			for (i = 0; i < KM_NP; i++)
				if (km.p[i] == 0)
					km.p[i] = 1;
			repeat = *km.cp;
			km_flip_cursor();

			switch (c) {

			case 'A':
				while (repeat--)
					km.y--;
				break;

			case 'B':
				while (repeat--)
					km.y++;
				break;

			case 'C':
				while (repeat--)
					km.x++;
				break;

			case 'D':
				while (repeat--)
					km.x--;
				break;

			case 'E':
				while (repeat--) {
					km.x = 0;
					km.y++;
				}
				break;

			case 'H':
			case 'f':
				km.x = *km.cp - 1;
				km.cp--;
				km.y = *km.cp - 1;
				km.cp--;
				break;

			case 'K':
				km_clear_eol();
				break;

			case 'm':
				km.bg = km_color[(*km.cp - 1) & 3];
				km.cp--;
				km.fg = km_color[(*km.cp - 1) & 3];
				km.cp--;

				/* prevent washout */
				if (km.bg == km.fg) {
					km.bg = WHITE;
					km.bg = BLACK;
				}
				break;
			}
		}
		km.cp = &km.p[1];
		for (i = 0; i < KM_NP; i++)
			km.p[i] = 0;
		km.ansi = 0;
	} else {
	km_flip_cursor();

	switch (c) {

	case '\r':
		if ((km.flags & KMF_CURSOR) == 0) {
			km.flags |= KMF_CURSOR;
			km_flip_cursor();
		}
		km.x = 0;
		break;

	case '\n':
		km.y++;
		break;

	case '\b':
		if (km.x == 0)
			break;
		km.x--;
		break;

	case '\t':
		j = km.x;
		for (i = 0; i < TAB_SIZE - (j % TAB_SIZE); i++) {
			km_flip_cursor();
			kmpaint (' ');
		}
		km_flip_cursor();
		break;

	case np:
		km.x = km.y = 0;
		km_clear_win();
		break;

	case bel:
		/* FIXME: send bell to speaker! */
		break;

	case esc:
		km.ansi = 1;
		break;

	default:
		c &= 0x7f;
		if (c < ' ')		/* skip other control chars */
			break;
		if ((km.flags & KMF_CURSOR) == 0)
			km.flags |= KMF_CURSOR;
		c -= ' ';	/* font tables start at space char */
		/* Set vmem at upper left corner of char to be rendered. */
		if (km.x >= 0){
			vmem = KM_P_VIDEOMEM +
				(((km.nc_lm + km.x) * CHAR_W * sizeof (int))/KM_NPPW) +
				(km.nc_tm * CHAR_H * KM_VIDEO_NBPL);
		} else {	/* Treat km.x as a pixel offset into frame buffer? */
			vmem = KM_P_VIDEOMEM + (((-km.x) * sizeof(int)) / KM_NPPW);
		}
		/*
		 * Render each 8 pixel scanline fragment from the font.
		 */
		for (y = 0; y < CHAR_H; y++) {
			if (km.y >= 0)
				vp = km.y * CHAR_H;
			else
			if (km.x >= 0)
				vp = km.y;
			else
				vp = -km.y;
			vp = vmem + (vp + y) * KM_VIDEO_NBPL;
			i = ohlfs12[c][y];	/* i gets 1 scanline frag from font. */
			pattern = 0;
			switch ( KM_NPPW )
			{
			    case 16:	/* 2 bit pixels */
				for (j = 0; j < CHAR_W; ++j)
					pattern |= ((((i >> j) & 1)?
						km.fg : km.bg) & 3) << (j << 1);
				*(short*) vp = pattern;
				break;
			    case 4:	/* 8 bit pixels */
			        for ( j = CHAR_W - 1; j >= 0; --j )
				   *((char*)vp)++ = (((i >> j)&1)?km.fg:km.bg) & 0xFF;
				break;
			    case 2:	/* 16 bit pixels */
			        for ( j = CHAR_W - 1; j >= 0; --j )
				   *((short*)vp)++ = (((i >> j)&1)?km.fg:km.bg)&0xFFFF;
				break;
			    case 1:	/* 32 bit pixels */
			        for ( j = CHAR_W - 1; j >= 0; --j )
				   *((int*)vp)++ = (((i >> j)&1)?km.fg:km.bg);
				break;
			}
		}
		if (km.x >= 0)
			km.x++;
		else
			km.x -= CHAR_W;
		break;
	}
	}
	if (km.x >= km.nc_w)		/* wrap */
		km.x = 0,  km.y++;
	if (km.y >= km.nc_h) {		/* scroll screen */
		km.y = km.nc_h - 1;
		bpl = KM_VIDEO_NBPL;
		/* Indent to left margin, scanline 0 */
		vmem = KM_P_VIDEOMEM + (km.nc_lm * CHAR_W * sizeof(int) / KM_NPPW);
		chunk = CHAR_H * bpl;	/* Bytes in a full row of characters */
		longs = (km.nc_w * CHAR_W) / KM_NPPW;	/* longs per window scanline */
		for (y = CHAR_H*km.nc_tm; y < CHAR_H*km.nc_tm +
		    CHAR_H*(km.nc_h - 1); y++) {
			d = (int*) (vmem + bpl*y);
			s = (int*) ((int) d + chunk);
			LOOP32 (longs, *d++ = *s++);
		}
		km_clear_eol();
	}
	km_flip_cursor();
	km_end_access();
}

/*
 * Negating the pixel value may do weird things on some LUT based pseudo-color systems.
 * We will have to see what happens...
 */
km_flip_cursor()
{
	register int vmem, vp, y, x;

	if (km.x < 0)
		return;
	km_begin_access();
	/* Set vmem to upper left corner of cursor rect, on 1st line in window. */
	vmem = KM_P_VIDEOMEM + (((km.nc_lm + km.x) * CHAR_W * sizeof (int))/KM_NPPW) +
		(km.nc_tm * CHAR_H * KM_VIDEO_NBPL);
	for (y = 0; y < CHAR_H; y++) {
		if (km.y >= 0)			/* Bump down to current text line */
			vp = km.y * CHAR_H;
		else
			vp = km.y;
		vp = vmem + (vp + y) * KM_VIDEO_NBPL;	/* Bump to next scanline */
		/* Zap 8 pixels along the scanline */
		if ( KM_NPPW == 16 ) {
			*(short*) vp = ~*(short*) vp;
		} else {
			for ( x = 0; x < 8; x += KM_NPPW ) {
				*((int*)vp) = ~*((int*)vp);
				++((int*)vp);
			}
		}
	}
	km_end_access();
}

km_clear_eol()
{
	register int x, y, vmem, nchars;

	km_begin_access();
	nchars = km.nc_w - km.x;	/* Number of characters to clear. */
	for (y = 0; y < CHAR_H; y++) {
		vmem = KM_P_VIDEOMEM
			+ (((km.nc_tm + km.y) * CHAR_H + y) * KM_VIDEO_NBPL)
			+ ((km.nc_lm + km.x) * CHAR_W * sizeof (int))/KM_NPPW;
		if ( KM_NPPW == 16 ) {	/* 2 bit display */
		    /* Hidden assumption: CHAR_W == 8 */
		    for (x = 0; x < nchars; x++)
			    *((short*)vmem)++ = km.bg;
		} else {
		    /* Use x as a word index into the scanline */
		    for ( x = 0; x < (nchars * CHAR_W) / KM_NPPW; ++x )
		    	*((int*)vmem)++ = km.bg;
		}
	}
	km_end_access();
}

alert (w, h, title, msg, p1, p2, p3, p4, p5, p6, p7, p8)
	char *title, *msg;
{
	char str[256];

	/* handle back-to-back alerts */
	XPR(XPR_KM, ("alert: km.flags 0x%x cons_tp 0x%x &cons 0x%x showcount %d\n",
		km.flags, cons_tp, &cons, km.showcount));
	if (km.flags & KMF_ALERT)
		alert_done();
	if (cons_tp != &cons || (km.flags & KMF_SEE_MSGS) == 0) {
		/*
		 * We assume that we are the first to need the window,
		 * so pop it up and set km.showcount to non-zero.
		 */
		alert_lock_screen (1);
		kmpopup (title, POPUP_ALERT, w, h, 1);
		km.showcount++;
		XPR(XPR_KM, ("alert: showcount1 %d\n", km.showcount));
	} else if (km.showcount) {
		/*
		 * Non-zero km.showcount means we are going to take
		 * the window away when everyone is done.  Bump it
		 * to account for us.
		 */
		km.showcount++;
		XPR(XPR_KM, ("alert: showcount2 %d\n", km.showcount));
	}
	km.flags |= KMF_ALERT;
	alert_key = 0;
	km.flags |= KMF_ALERT_KEY;
	strcpy(str, "%L");
	strcat(str, msg);
	printf(str, p1, p2, p3, p4, p5, p6, p7, p8);
}

alert_done()
{
	XPR(XPR_KM, ("alert_done: km.flags 0x%x showcount %d\n",
		km.flags, km.showcount));
	km.flags &= ~KMF_ALERT_KEY;
	alert_key = 0;
	if ((km.flags & KMF_ALERT) == 0)
		return;
	km.flags &= ~KMF_ALERT;
	if (km.showcount && --km.showcount == 0) {
		kmrestore();
		alert_lock_screen (0);
	}
}

alert_lock_screen (lock)
{
	thread_t		th;
	int			s;
	
	if (!eventsOpen) {
	    return;
	}
	/* lock cursor in event driver */
	if (lock) {
		s = splx(ipltospl(I_IPL(I_VIDEO)));
		while (evp->cursorSema == 1) {
			if (csw_needed(current_thread(), current_processor()))
				thread_block();
		}
		evp->cursorSema = 1;
		splx(s);
	} else {
		evp->cursorSema = 0;
	}
	
	if (eventTask == 0)
		return;
	th = (thread_t) queue_first(&eventTask->thread_list);
	while (!queue_end(&eventTask->thread_list, (queue_entry_t) th)) {
		if (lock)
			thread_suspend(th);
		else
			thread_resume(th);
		th = (thread_t) queue_next(&th->thread_list);
	}
}

km_power_down()
{
	int bit, c;
	extern int force_power_down;
	extern volatile int *intrstat;
	extern struct mon_global *mon_global;
	struct mon_global *mg = mon_global;

	if (km.flags & KMF_SEE_MSGS) {
		printf("\nReally power off?  Type y to power off, "
			"type n to cancel.\n");
		do {
			c = kmtrygetc();
		} while (c == -1);
		if (c != 'y') {
			bit = I_BIT(I_POWER);
			while (*intrstat & bit)
				rtc_intr();		/* spin????? */
			*intrmask |= bit;
			intr_mask |= bit;
			return;
		}
		printf("Shut down in progress.\n");
	}
	force_power_down = 1;	/* in case we get stuck */

	/* turn off animation so ROM monitor doesn't power off before we do */
	vidStopAnimation();	/* Signal the ROM */
	vidSuspendAnimation();	/* Mask retrace interrupts */
	reboot_mach(RB_POWERDOWN | RB_EJECT);
}


/*
 *	Called from startup(), in next/machdep.c, after VM is running and before the
 *	first console printf.  This code must guarantee that the console device is
 *	mapped in and some form of backing store is available for kmpopup()
 *	to use.
 *
 *	Current NeXT frame buffers provide a space for the backing store in the
 *	'hidden' scanlines between line 832 and line 910.  If no such store is provided
 *	by the frame buffer vendor, we steal a chunk of memory permanently, and set the 
 *	km structure to reflect this and inhibit further 'thefts'.
 */
km_switch_to_vm()
{
	int i;
	static int km_vm_is_up = 0;
	extern caddr_t map_addr();
	extern struct mon_global *mon_global;
	struct mon_global *mg = mon_global;
	
	if ( ! km_vm_is_up && (km_coni.flag_bits & KM_CON_ON_NEXTBUS) ){
		km_vm_is_up = 1;
		/* Map in whatever the frame buffer ROM says to... */
		for ( i = 0; i < KM_CON_MAP_ENTRIES; ++i ) {
			if ( km_coni.map_addr[i].size == 0 )
				continue;
			km_coni.map_addr[i].virt_addr =
				(int)map_addr(  km_coni.map_addr[i].phys_addr,
						km_coni.map_addr[i].size);
		}

		/*
		    * If the ROMs support console devices on the NextBus, we have to
		    * fix up the hardware addresses in the monitor globals, too,
		    * once we have remapped the hardware. This avoids a panic from
		    * the animation code, and lets the 'mon' command from the NMI
		    * box work properly.
		    */
		if ( *MG(short*, MG_seq) >= 44 ) {
		    *MG(char**, MG_con_map_vaddr0) =
		    			(char *)km_coni.map_addr[0].virt_addr;
		    *MG(char**, MG_con_map_vaddr1) =
		    			(char *)km_coni.map_addr[1].virt_addr;
		    *MG(char**, MG_con_map_vaddr2) =
		    			(char *)km_coni.map_addr[2].virt_addr;
		    *MG(char**, MG_con_map_vaddr3) =
		    			(char *)km_coni.map_addr[3].virt_addr;
		    *MG(char**, MG_con_map_vaddr4) =
		    			(char *)km_coni.map_addr[4].virt_addr;
		    *MG(char**, MG_con_map_vaddr5) =
		    			(char *)km_coni.map_addr[5].virt_addr;
		}
		/*
		 * If there is no backing store, obtain one.  Frame buffers
		 * built by NeXT all provide a backing store.  This code just 
		 * helps out on strange configurations from 3rd parties.
		 */
		if ( KM_BACKING_SIZE == 0 ){ /* Non-zero indicates a backing store */
			/* Sigh... Wire it down. This won't happen on NeXT boards. */
			KM_BACKING_STORE = (int) kmem_alloc(kernel_map, WIN_SIZE_NOM);
			if ( KM_BACKING_STORE ) {	/* And don't steal any more! */
				KM_BACKING_SIZE = WIN_SIZE_NOM;
				km.nc_w2 = NC_W_NOM;
				km.nc_h2 = NC_H_NOM;
				km.store2 = KM_BACKING_STORE;
				km.flags |= KMF_PERMANENT;
			}
		}
	}
}

#include "nextdev/km_pcode.h"

/*
 * Read the requested word from the p-code table.
 *
 * The byte lane ID value tells us where the data is found.
 * A value of 1 indicates that the data lies in a single 
 * contiguous word.  A value of 4 indicates that the data lies in the 
 * most significant bytes of 4 sequential words.  A value of 8 indicates that
 * the data lies in the most significant bytes of 4 sequential 64 bit
 * longwords, viewed as pairs of 32 bit words over the NextBus.
 *
 * The 'item' is a logical index into the p-code table.
 */
 static unsigned int
km_read_pcode( item )
    int item;
{
	register unsigned int *ip;
	register unsigned int result;
	
	ip = ((unsigned int *) km_coni.map_addr[KM_CON_PCODE].virt_addr) + 
				(item * km_coni.byte_lane_id);
	
	/* Only values of 1, 4, or 8 are valid here. */
	switch( km_coni.byte_lane_id )
	{
		case ID_ALL:
			result = *ip;
			break;
		case ID_MSB32:
			result = *ip++ & 0xFF000000;
			result |= (*ip++ & 0xFF000000) >> 8;
			result |= (*ip++ & 0xFF000000) >> 16;
			result |= (*ip & 0xFF000000) >> 24;
			break;
		case ID_MSB64:
			result = *ip & 0xFF000000; ip += 2;
			result |= (*ip & 0xFF000000) >> 8; ip += 2;
			result |= (*ip & 0xFF000000) >> 16; ip += 2;
			result |= (*ip & 0xFF000000) >> 24;
			break;
		default:
			return 0;	/* Bogus byte lane ID value. */
	}
	return result;
}

/*
 * Select the console display device.
 * Default to the standard 2 bit display.
 *
 *	This routine is called before vm is running, so no address mapping need
 *	be done.
 */

km_select_console()
{
	int slot, fb_num, i;
	extern struct mon_global *mon_global;
	struct mon_global *mg = mon_global;
	volatile int *scr2 = (volatile int *)P_SCR2;
	
	if ( SLOT_ID == 0 )	/* Gain access to NextBus */
	{
		*scr2 |= SCR2_OVERLAY;
		if ( bmap_chip )
			bmap_chip->bm_lo = 0;
	}

	/*
	 *	If the ROM monitor says it found a board on the bus,
	 *	try that slot first.
	 *
	 *	ROMs with a major number less than 2, or a sequence number
	 *	less than 44 don't support finding a console.  For these, we
	 *	stick with the old behavior and just use the 2 bit display as
	 *	the console.
	 */
	if ( *MG(short*, MG_seq) >= 44 ) {
	    slot = (int) *MG(char*, MG_con_slot);
	    fb_num = (int) *MG(char*, MG_con_fbnum);

	    if ( slot != SLOT_ID ) {	/* CPU board uses defaults, below. */
		if ( km_try_slot( slot, fb_num ) == 1 )
		    return;
	    }
	}
	/* Check for which built-in frame buffer is on this CPU board. */
	bzero( &km_coni, sizeof (struct km_console_info) );
	if ( (fb_num = vidProbeForFB()) != -1 )
	{
		km_coni.slot_num = SLOT_ID;
		km_coni.fb_num = fb_num;
		vidGetConsoleInfo(&km_coni);
	}
}
/*
 * Check out the slot specified by the ROM monitor, and load in the 
 * needed configuration tables.
 */
km_try_slot( slot, fb_num )
    int slot;
    int fb_num;
{
	int * addr;
	unsigned int tmp;
	int nfb, pc, i, s;

	bzero( &km_coni, sizeof (struct km_console_info) );
	addr = (int *) KM_SLOT_ID_REG(slot);

	if ( (*addr & KM_SLOT_CONSOLE_BITS) != KM_SLOT_CONSOLE_BITS )
		return 0;

	km_coni.slot_num = slot;
	km_coni.fb_num = fb_num;
	/*
	 * At this point we have a board which claims to be a console device.
	 */
	addr = (int *)KM_CONVERT_ADDR(slot,KM_SIGNATURE_ADDR);
	if ( (*addr & KM_SIGNATURE_MASK) != KM_SIGNATURE_BITS )
		return 0;		/* Nope. Bad signature byte */
		
	addr = (int *)KM_CONVERT_ADDR(slot, KM_BYTE_LANE_ID_ADDR);
	km_coni.byte_lane_id = *addr >> 24;
	
	tmp = KM_READ_SLOT_BYTE(slot, KM_PCODE_ADDR3) << 24;
	tmp |= KM_READ_SLOT_BYTE(slot, KM_PCODE_ADDR2) << 16;
	tmp |= KM_READ_SLOT_BYTE(slot, KM_PCODE_ADDR1) << 8;
	tmp |= KM_READ_SLOT_BYTE(slot, KM_PCODE_ADDR0);
	km_coni.map_addr[KM_CON_PCODE].virt_addr = KM_CONVERT_ADDR(slot,tmp);
	km_coni.map_addr[KM_CON_PCODE].phys_addr = KM_CONVERT_ADDR(slot,tmp);
	/* Read the first 32 bits from the P-code ROM.  These give the size */
	/* Convert the p-code size into the number of bytes we need to map. */
	km_coni.map_addr[KM_CON_PCODE].size =
			km_read_pcode(KMPC_SIZE) * sizeof (int) * km_coni.byte_lane_id;
	/* Now read in the 'number_of_frame_buffers' value. */
	nfb = km_read_pcode( KMPC_NUM_FBS );
	if ( fb_num >= nfb || fb_num < 0 )	/* Sanity check. */
		return 0;

	/* Move the P-code PC up to the start of the FB description table */
	pc = KMPC_TABLE_START + (fb_num * KMPC_TABLE_INCR);
	/* A console device in a ready to run state has been detected. */
	km_coni.start_access_pfunc =
			km_read_pcode(pc + KMPC_START_ACCESS_OFF );
	km_coni.end_access_pfunc =
			km_read_pcode(pc + KMPC_END_ACCESS_OFF );
	pc = km_read_pcode(pc + KMPC_FB_LAYOUT_OFF);
	
	/* Read in the frame buffer layout structure */
	km_coni.pixels_per_word = km_read_pcode( pc++ );
	km_coni.bytes_per_scanline = km_read_pcode( pc++ );
	km_coni.dspy_w = km_read_pcode( pc++ );
	km_coni.dspy_max_w = km_read_pcode( pc++ );
	km_coni.dspy_h = km_read_pcode( pc++ );
	km_coni.flag_bits = km_read_pcode( pc++ );
	km_coni.color[0] = km_read_pcode( pc++ );
	km_coni.color[1] = km_read_pcode( pc++ );
	km_coni.color[2] = km_read_pcode( pc++ );
	km_coni.color[3] = km_read_pcode( pc++ );
	
	/* Read in the 5 map_addr structures. */
	for ( i = KM_CON_FRAMEBUFFER; i < KM_CON_MAP_ENTRIES; ++i ) {
	    tmp = km_read_pcode( pc++ );
	    km_coni.map_addr[i].phys_addr = KM_CONVERT_ADDR(slot,tmp);
	    km_coni.map_addr[i].virt_addr = KM_CONVERT_ADDR(slot,tmp);
	    km_coni.map_addr[i].size = km_read_pcode( pc++ );
	}
	km_coni.flag_bits |= KM_CON_ON_NEXTBUS;
	return 1;
}

/*
 * km_begin_access does any pre-conditioning that may be needed for a particular
 * console frame buffer.
 */
km_begin_access()
{
	if ( km_access_stack++ )
		return;
	if ( km_coni.start_access_pfunc )
		km_run_pcode( km_coni.start_access_pfunc );
}

/*
 * km_end_access does any post-access conditioning that may be needed for a particular
 * console frame buffer.
 */
km_end_access()
{
	if ( --km_access_stack > 0 )
		return;
	km_access_stack = 0;	/* Avoid underflow errors */
	
	if ( km_coni.end_access_pfunc )
		km_run_pcode( km_coni.end_access_pfunc );
		
	switch (machine_type) {
	
	case NeXT_WARP9C:
		/*
		 * The Warp 9C has a cached frame buffer.
		 * We need to flush the data cache to get the NMI
		 * window to behave cleanly.
		 */
		asm(".word 0xf478 | cpusha      dc");
		break;
	case NeXT_CUBE:
	case NeXT_WARP9:
	case NeXT_X15:
		break;
	}
	return FALSE;

}
/*
 * validate and convert the specified physical address into the correct 
 * virtual address.  This is support glue for the p-code machine.
 */
 vm_offset_t
km_convert_addr( addr )
    vm_offset_t addr;
{
	int i;
	int delta;
	for ( i = 0; i < KM_CON_MAP_ENTRIES; ++i ) {
	    if ( km_coni.map_addr[i].size == 0 )	/* empty entry */
		    continue;
	    if ( addr >= km_coni.map_addr[i].phys_addr
		&& addr < (km_coni.map_addr[i].phys_addr + km_coni.map_addr[i].size) ){
		    delta = addr - km_coni.map_addr[i].phys_addr;
		    return( (vm_offset_t)(delta + km_coni.map_addr[i].virt_addr) );
	    }
	}
	/* A bad address was found in the console p-code.  We are hosed! */
	log( LOG_ERR, "km_convert_addr(0x%x): Bad p-code ROM address (slot %d)\n",
		addr, km_coni.slot_num );
	panic( "km_convert_addr() hit invalid phys addr in p-code" );
}
/*
 * This function implements the actual p-machine which interprets the 
 * pseudocode in the extended ROM.
 *
 * The p-machine is called with the requested operation, which is really
 * the starting PC for the p-machine for the desired operation.  A value of zero
 * means that no p-machine operation is needed, and the p-machine should return
 * a value of 0.
 *
 * Once a starting PC has been determined, the execution loop is entered.
 * The byte lane format in use is examined, and an instruction is fetched.
 * The opcode is extracted.  If the opcode indicates that an immediate value
 * is needed, than that is also fetched. The pc is incremented.
 * The extracted opcode is processed through a case switch to the code
 * which implements the specified operation.
 *
 * An END opcode causes the p-machine to stop execution, and the p_machine
 * procedure returns the value in reg[0] to the caller.
 */
km_run_pcode( pc )
    int pc;
{
	int32 reg[P_NREGS];	/* P-machine register set */
	int32 immediate;	/* 32 bit immediate value for some insts. */
	unsigned int32 inst;	/* The current instruction */
	unsigned int opcode;	/* The OPCODE field from the inst. */
	unsigned int slot;
	int zero, positive, negative;	/* The condition codes... */
	vm_offset_t	addr;	/* An address, as found in the p-code. */
	
	if ( pc == 0 )	/* No procedure for this op? */
		return( 0 );
	
	slot = km_coni.slot_num;
	for(;;)
	{
		inst = km_read_pcode( pc++ );
		if ( IMMEDIATE_FOLLOWS_OPCODE( inst ) )
			immediate = km_read_pcode( pc++ );
		switch( OPCODE( inst ) )
		{
			case LOAD_CR:
			    reg[REG2(inst)] = immediate;
			    break;
				
			case LOAD_AR:
			    addr = (vm_offset_t)KM_CONVERT_ADDR(slot,immediate);
			    reg[REG2(inst)] = *((int *)addr);
			    break;
				
			case LOAD_IR:
			    addr = (vm_offset_t)KM_CONVERT_ADDR(slot,reg[REG1(inst)]);
			    addr = km_convert_addr(addr);
			    reg[REG2(inst)]=*((int *)addr);
			    break;
				
			case STORE_RA:
			    addr = (vm_offset_t)KM_CONVERT_ADDR(slot,immediate);
			    addr = km_convert_addr(addr);
			    *((int *)addr) = reg[REG1(inst)]; 
			    break;
							    
			case STORE_RI:
			    addr = (vm_offset_t)KM_CONVERT_ADDR(slot,reg[REG2(inst)]);
			    addr = km_convert_addr(addr);
			    *((int *)addr) = reg[REG1(inst)]; 
			    break;
			    
			case STOREV:
				{
				    int32 addr;
				    int32 counter = BRANCH_DEST(inst);
				    
				    do
				    {
					    immediate = km_read_pcode(pc++);
					    addr = (vm_offset_t)km_read_pcode(pc++);
					    addr =
					      km_convert_addr(KM_CONVERT_ADDR(slot,addr));
					    *((int *)addr) = immediate;
				    } while( --counter );
				}
				break;
			   
			case ADD_CRR:
			    reg[REG2(inst)] = immediate + reg[REG1(inst)];
			    break;
			    
			case ADD_RRR:
			    reg[REG3(inst)]=reg[REG1(inst)]+reg[REG2(inst)];
			    break;
			    
			case SUB_RCR:
			    reg[REG2(inst)]=reg[REG1(inst)]-immediate;
			    break;
			    
			case SUB_CRR:
			    reg[REG2(inst)]=immediate - reg[REG1(inst)];
			    break;

			case SUB_RRR:
			    reg[REG3(inst)]=reg[REG1(inst)] - reg[REG2(inst)];
			    break;
			    
			case AND_CRR:
			    reg[REG2(inst)] = immediate & reg[REG1(inst)];
			    break;
			    
			case AND_RRR:
			    reg[REG3(inst)]=reg[REG1(inst)] & reg[REG2(inst)];
			    break;
			    
			case OR_CRR:
			    reg[REG2(inst)] = immediate | reg[REG1(inst)];
			    break;
			    
			case OR_RRR:
			    reg[REG3(inst)]=reg[REG1(inst)] | reg[REG2(inst)];
			    break;
			
			case XOR_CRR:
			    reg[REG2(inst)] = immediate ^ reg[REG1(inst)];
			    break;
			    
			case XOR_RRR:
			    reg[REG3(inst)]=reg[REG1(inst)] ^ reg[REG2(inst)];
			    break;

			case ASL_RCR:
			    reg[REG3(inst)] = reg[REG1(inst)] << FIELD2(inst);
			    break;

			case ASR_RCR:
			    reg[REG3(inst)] = reg[REG1(inst)] >> FIELD2(inst);
			    break;

			case MOVE_RR:
			    reg[REG2(inst)] = reg[REG1(inst)];
			    break;
			    
			case TEST_R:
				zero = 0;
				positive = 0;
				negative = 0;
				
				immediate = reg[REG1(inst)];
				if ( immediate == 0 )
				{
					zero = 1;
					break;
				}
				if ( immediate > 0 )
				{
					positive = 1;
					break;
				}
				negative = 1;
				break;
				
			case BR:
				pc = BRANCH_DEST( inst );
				break;
				
			case BPOS:
				if ( positive )
					pc = BRANCH_DEST( inst );
				break;
				
			case BNEG:
				if ( negative )
					pc = BRANCH_DEST( inst );
				break;

			case BZERO:
				if ( zero )
					pc = BRANCH_DEST( inst );
				break;

			case BNPOS:
				if ( ! positive )
					pc = BRANCH_DEST( inst );
				break;

			case BNNEG:
				if ( ! negative )
					pc = BRANCH_DEST( inst );
				break;

			case BNZERO:
				if ( ! zero )
					pc = BRANCH_DEST( inst );
				break;

			case END:
				return( reg[0] );
		}
	}
}





