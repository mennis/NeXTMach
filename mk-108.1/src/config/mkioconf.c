/* 
 * Mach Operating System
 * Copyright (c) 1987 Carnegie-Mellon University
 * All rights reserved.  The CMU software License Agreement specifies
 * the terms and conditions for use and redistribution.
 */

/*
 **********************************************************************
 * HISTORY
 *  9-Oct-87  Richard Sanzi (sanzi) at Carnegie-Mellon University
 *	Added change from ACIS to support wildcarding the hdc
 *	controlllers.
 *
 * 08-Jan-87  Robert Beck (beck) at Sequent Computer Systems, Inc.
 *	Add MACHINE_SQT cases.
 *
 * 04-Nov-86  John Seamons (jks) at NeXT
 *	Added machine type NeXT.
 *
 * 27-Oct-86  Jonathan J. Chew (jjc) at Carnegie-Mellon University
 *	Merged in David Black's changes for the Multimax.
 *
 * 05-Jun-86  Jonathan J. Chew (jjc) at Carnegie-Mellon University
 *	Merged in changes for Sun 2 and 3.
 *
 **********************************************************************
 */ 
 
/*
 * Copyright (c) 1980 Regents of the University of California.
 * All rights reserved.  The Berkeley software License Agreement
 * specifies the terms and conditions for redistribution.
 */

#ifndef lint
static char sccsid[] = "@(#)mkioconf.c	5.1 (Berkeley) 5/8/85";
#endif not lint

#include <stdio.h>
#include "y.tab.h"
#include "config.h"

/*
 * build the ioconf.c file
 */
char	*qu();
char	*intv();
#if	MACHINE_NeXT
char	*dn();
#endif	MACHINE_NeXT

#if MACHINE_VAX
vax_ioconf()
{
	register struct device *dp, *mp, *np;
	register int uba_n, slave;
	FILE *fp;

	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"machine/pte.h\"\n");
	fprintf(fp, "#include \"sys/param.h\"\n");
	fprintf(fp, "#include \"sys/buf.h\"\n");
	fprintf(fp, "#include \"sys/vm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include \"vaxmba/mbavar.h\"\n");
	fprintf(fp, "#include \"vaxuba/ubavar.h\"\n\n");
	fprintf(fp, "\n");
	fprintf(fp, "#define C (caddr_t)\n\n");
	/*
	 * First print the mba initialization structures
	 */
	if (seen_mba) {
		for (dp = dtab; dp != 0; dp = dp->d_next) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			    !eq(mp->d_name, "mba"))
				continue;
			fprintf(fp, "extern struct mba_driver %sdriver;\n",
			    dp->d_name);
		}
		fprintf(fp, "\nstruct mba_device mbdinit[] = {\n");
		fprintf(fp, "\t/* Device,  Unit, Mba, Drive, Dk */\n");
		for (dp = dtab; dp != 0; dp = dp->d_next) {
			mp = dp->d_conn;
			if (dp->d_unit == QUES || mp == 0 ||
			    mp == TO_NEXUS || !eq(mp->d_name, "mba"))
				continue;
			if (dp->d_addr) {
				printf("can't specify csr address on mba for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_vec != 0) {
				printf("can't specify vector for %s%d on mba\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive == UNKNOWN) {
				printf("drive not specified for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_slave != UNKNOWN) {
				printf("can't specify slave number for %s%d\n", 
				    dp->d_name, dp->d_unit);
				continue;
			}
			fprintf(fp, "\t{ &%sdriver, %d,   %s,",
				dp->d_name, dp->d_unit, qu(mp->d_unit));
			fprintf(fp, "  %s,  %d },\n",
				qu(dp->d_drive), dp->d_dk);
		}
		fprintf(fp, "\t0\n};\n\n");
		/*
		 * Print the mbsinit structure
		 * Driver Controller Unit Slave
		 */
		fprintf(fp, "struct mba_slave mbsinit [] = {\n");
		fprintf(fp, "\t/* Driver,  Ctlr, Unit, Slave */\n");
		for (dp = dtab; dp != 0; dp = dp->d_next) {
			/*
			 * All slaves are connected to something which
			 * is connected to the massbus.
			 */
			if ((mp = dp->d_conn) == 0 || mp == TO_NEXUS)
				continue;
			np = mp->d_conn;
			if (np == 0 || np == TO_NEXUS ||
			    !eq(np->d_name, "mba"))
				continue;
			fprintf(fp, "\t{ &%sdriver, %s",
			    mp->d_name, qu(mp->d_unit));
			fprintf(fp, ",  %2d,    %s },\n",
			    dp->d_unit, qu(dp->d_slave));
		}
		fprintf(fp, "\t0\n};\n\n");
	}
	/*
	 * Now generate interrupt vectors for the unibus
	 */
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_vec != 0) {
			struct idlst *ip;
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			    !eq(mp->d_name, "uba"))
				continue;
			fprintf(fp,
			    "extern struct uba_driver %sdriver;\n",
			    dp->d_name);
			fprintf(fp, "extern ");
			ip = dp->d_vec;
			for (;;) {
				fprintf(fp, "X%s%d()", ip->id, dp->d_unit);
				ip = ip->id_next;
				if (ip == 0)
					break;
				fprintf(fp, ", ");
			}
			fprintf(fp, ";\n");
			fprintf(fp, "int\t (*%sint%d[])() = { ", dp->d_name,
			    dp->d_unit, dp->d_unit);
			ip = dp->d_vec;
			for (;;) {
				fprintf(fp, "X%s%d", ip->id, dp->d_unit);
				ip = ip->id_next;
				if (ip == 0)
					break;
				fprintf(fp, ", ");
			}
			fprintf(fp, ", 0 } ;\n");
		}
	}
	fprintf(fp, "\nstruct uba_ctlr ubminit[] = {\n");
	fprintf(fp, "/*\t driver,\tctlr,\tubanum,\talive,\tintr,\taddr */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_type != CONTROLLER || mp == TO_NEXUS || mp == 0 ||
		    !eq(mp->d_name, "uba"))
			continue;
		if (dp->d_vec == 0) {
			printf("must specify vector for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_addr == 0) {
			printf("must specify csr address for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
			printf("drives need their own entries; dont ");
			printf("specify drive or slave for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_flags) {
			printf("controllers (e.g. %s%d) ",
			    dp->d_name, dp->d_unit);
			printf("don't have flags, only devices do\n");
			continue;
		}
		fprintf(fp,
		    "\t{ &%sdriver,\t%d,\t%s,\t0,\t%sint%d, C 0%o },\n",
		    dp->d_name, dp->d_unit, qu(mp->d_unit),
		    dp->d_name, dp->d_unit, dp->d_addr);
	}
	fprintf(fp, "\t0\n};\n");
/* unibus devices */
	fprintf(fp, "\nstruct uba_device ubdinit[] = {\n");
	fprintf(fp,
"\t/* driver,  unit, ctlr,  ubanum, slave,   intr,    addr,    dk, flags*/\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_unit == QUES || dp->d_type != DEVICE || mp == 0 ||
		    mp == TO_NEXUS || mp->d_type == MASTER ||
		    eq(mp->d_name, "mba"))
			continue;
		np = mp->d_conn;
		if (np != 0 && np != TO_NEXUS && eq(np->d_name, "mba"))
			continue;
		np = 0;
		if (eq(mp->d_name, "uba")) {
			if (dp->d_vec == 0) {
				printf("must specify vector for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr == 0) {
				printf("must specify csr for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
				printf("drives/slaves can be specified ");
				printf("only for controllers, ");
				printf("not for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			uba_n = mp->d_unit;
			slave = QUES;
		} else {
			if ((np = mp->d_conn) == 0) {
				printf("%s%d isn't connected to anything ",
				    mp->d_name, mp->d_unit);
				printf(", so %s%d is unattached\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			uba_n = np->d_unit;
			if (dp->d_drive == UNKNOWN) {
				printf("must specify ``drive number'' ");
				printf("for %s%d\n", dp->d_name, dp->d_unit);
				continue;
			}
			/* NOTE THAT ON THE UNIBUS ``drive'' IS STORED IN */
			/* ``SLAVE'' AND WE DON'T WANT A SLAVE SPECIFIED */
			if (dp->d_slave != UNKNOWN) {
				printf("slave numbers should be given only ");
				printf("for massbus tapes, not for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_vec != 0) {
				printf("interrupt vectors should not be ");
				printf("given for drive %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr != 0) {
				printf("csr addresses should be given only ");
				printf("on controllers, not on %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = dp->d_drive;
		}
		fprintf(fp, "\t{ &%sdriver,  %2d,   %s,",
		    eq(mp->d_name, "uba") ? dp->d_name : mp->d_name, dp->d_unit,
		    eq(mp->d_name, "uba") ? " -1" : qu(mp->d_unit));
		fprintf(fp, "  %s,    %2d,   %s, C 0%-6o,  %d,  0x%x },\n",
		    qu(uba_n), slave, intv(dp), dp->d_addr, dp->d_dk,
		    dp->d_flags);
	}
	fprintf(fp, "\t0\n};\n");
	(void) fclose(fp);
}
#endif

#if MACHINE_SUN
#ifdef	CMU
#define	VEC_LO	64
#define	VEC_HI	255

check_vector(vec)
	register struct idlst *vec;
{

	if (vec->id_vec == 0)
		fprintf(stderr, "vector number for %s not given\n", vec->id);
	else if (vec->id_vec < VEC_LO || vec->id_vec > VEC_HI)
		fprintf(stderr,
			"vector number %d for %s is not between %d and %d\n",
			vec->id_vec, vec->id, VEC_LO, VEC_HI);
}

sun_ioconf()
{
	register struct device *dp, *mp;
	register int slave;
	register struct idlst *vp;
	FILE *fp;

	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"sys/param.h\"\n");
	fprintf(fp, "#include \"sys/buf.h\"\n");
	fprintf(fp, "#include \"sys/vm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include \"sundev/mbvar.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#define C (caddr_t)\n\n");
	fprintf(fp, "\n");

	/*
	 * Now generate interrupt vectors for the Mainbus
	 */
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (mp == TO_NEXUS || mp == 0 || mp->d_conn != TO_NEXUS)
			continue;
		fprintf(fp, "extern struct mb_driver %sdriver;\n",
			    dp->d_name);
		if (dp->d_vec != 0) {
			if (dp->d_pri == 0)
				fprintf(stderr,
				    "no priority specified for %s%d\n",
				    dp->d_name, dp->d_unit);
			fprintf(fp, "extern ");
			for (vp = dp->d_vec;;) {
				fprintf(fp, "X%s%d()", vp->id, dp->d_unit);
				vp = vp->id_next;
				if (vp == 0)
					break;
				fprintf(fp, ", ");
			}
			fprintf(fp, ";\n");

			for (vp = dp->d_vec; vp; vp = vp->id_next) {
				fprintf(fp, "int V%s%d = %d;\n",
				    vp->id, dp->d_unit, dp->d_unit);
			}

			fprintf(fp, "struct vec %s[] = { ", intv(dp));
			for (vp = dp->d_vec; vp != 0; vp = vp->id_next) {
				fprintf(fp, "{ X%s%d, %d, &V%s%d }, ",
					vp->id, dp->d_unit, vp->id_vec,
					vp->id, dp->d_unit);
				check_vector(vp);
			}
			fprintf(fp, "0 };\n");
		}
	}

	/*
	 * Now spew forth the mb_ctlr structures
	 */
	fprintf(fp, "\nstruct mb_ctlr mbcinit[] = {\n");
	fprintf(fp,
"/* driver,\tctlr,\talive,\taddress,\tintpri,\t intr,\tspace */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_type != CONTROLLER || mp == TO_NEXUS || mp == 0 ||
		    mp->d_conn != TO_NEXUS)
			continue;
		if (dp->d_addr == UNKNOWN) {
			printf("must specify csr address for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
			printf("drives need their own entries; ");
			printf("don't specify drive or slave for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_flags) {
			printf("controllers (e.g. %s%d) don't have flags, ");
			printf("only devices do\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		fprintf(fp,
		"{ &%sdriver,\t%d,\t0,\tC 0x%08x,\t%d,\t%s, 0x%x },\n",
		    dp->d_name, dp->d_unit, dp->d_addr,
		    dp->d_pri, intv(dp), ((dp->d_mach << 16) | dp->d_bus));
	}
	fprintf(fp, "\t0\n};\n");

	/*
	 * Now we go for the mb_device stuff
	 */
	fprintf(fp, "\nstruct mb_device mbdinit[] = {\n");
	fprintf(fp,
"/* driver,\tunit, ctlr, slave, address,      pri, dk, flags, intr, space */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_unit == QUES || dp->d_type != DEVICE || mp == 0 ||
		    mp == TO_NEXUS || mp->d_type == MASTER)
			continue;
		if (mp->d_conn == TO_NEXUS) {
			if (dp->d_addr == UNKNOWN) {
				printf("must specify csr for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
				printf("drives/slaves can be specified only ");
				printf("for controllers, not for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = QUES;
		} else {
			if (mp->d_conn == 0) {
				printf("%s%d isn't connected to anything, ",
				    mp->d_name, mp->d_unit);
				printf("so %s%d is unattached\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive == UNKNOWN) {
				printf("must specify ``drive number'' for %s%d\n",
				   dp->d_name, dp->d_unit);
				continue;
			}
			/* NOTE THAT ON THE UNIBUS ``drive'' IS STORED IN */
			/* ``SLAVE'' AND WE DON'T WANT A SLAVE SPECIFIED */
			if (dp->d_slave != UNKNOWN) {
				printf("slave numbers should be given only ");
				printf("for massbus tapes, not for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_pri != 0) {
				printf("interrupt priority should not be ");
				printf("given for drive %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr != UNKNOWN) {
				printf("csr addresses should be given only");
				printf(" on controllers, not on %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = dp->d_drive;
		}
		fprintf(fp,
"{ &%sdriver,\t%d,  %s,   %2d,     C 0x%08x, %d,   %d, 0x%x, %s, 0x%x },\n",
		    mp->d_conn == TO_NEXUS? dp->d_name : mp->d_name, dp->d_unit,
		    mp->d_conn == TO_NEXUS? " -1" : qu(mp->d_unit),
		    slave,
		    dp->d_addr == UNKNOWN? 0 : dp->d_addr,
		    dp->d_pri, dp->d_dk, dp->d_flags, intv(dp),
		    ((dp->d_mach << 16) | dp->d_bus));
	}
	fprintf(fp, "\t0\n};\n");
	pseudo_inits(fp);
	(void) fclose(fp);
}

pseudo_inits(fp)
	FILE *fp;
{
	register struct device *dp;
	int count;

#ifdef	notdef
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type != PSEUDO_DEVICE || dp->d_init == 0)
			continue;
		fprintf(fp, "extern %s();\n", dp->d_init);
	}
#endif	notdef
	fprintf(fp, "struct pseudo_init {\n");
	fprintf(fp, "\tint\tps_count;\n\tint\t(*ps_func)();\n");
	fprintf(fp, "} pseudo_inits[] = {\n");
#ifdef	notdef
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type != PSEUDO_DEVICE || dp->d_init == 0)
			continue;
		count = dp->d_slave;
		if (count <= 0)
			count = 1;
		fprintf(fp, "\t%d,\t%s,\n", count, dp->d_init);
	}
#endif	notdef
	fprintf(fp, "\t0,\t0,\n};\n");
}
#else	CMU
sun_ioconf()
{
	register struct device *dp, *mp;
	register int slave;
	FILE *fp;

	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"sys/param.h\"\n");
	fprintf(fp, "#include \"sys/buf.h\"\n");
	fprintf(fp, "#include \"sys/vm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include \"sundev/mbvar.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#define C (caddr_t)\n\n");
	fprintf(fp, "\n");
	/*
	 * Now generate interrupt vectors for the Multibus
	 */
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_pri != 0) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			    !eq(mp->d_name, "mb"))
				continue;
			fprintf(fp, "extern struct mb_driver %sdriver;\n",
			    dp->d_name);
		}
	}
	/*
	 * Now spew forth the mb_cinfo structure
	 */
	fprintf(fp, "\nstruct mb_ctlr mbcinit[] = {\n");
	fprintf(fp, "/*\t driver,\tctlr,\talive,\taddr,\tintpri */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_type != CONTROLLER || mp == TO_NEXUS || mp == 0 ||
		    !eq(mp->d_name, "mb"))
			continue;
		if (dp->d_pri == 0) {
			printf("must specify priority for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_addr == 0) {
			printf("must specify csr address for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
			printf("drives need their own entries; ");
			printf("dont specify drive or slave for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_flags) {
			printf("controllers (e.g. %s%d) don't have flags, ");
			printf("only devices do\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		fprintf(fp, "\t{ &%sdriver,\t%d,\t0,\tC 0x%x,\t%d },\n",
		    dp->d_name, dp->d_unit, dp->d_addr, dp->d_pri);
	}
	fprintf(fp, "\t0\n};\n");
	/*
	 * Now we go for the mb_device stuff
	 */
	fprintf(fp, "\nstruct mb_device mbdinit[] = {\n");
	fprintf(fp,
"\t/* driver,  unit, ctlr,  slave,   addr,    pri,    dk, flags*/\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_unit == QUES || dp->d_type != DEVICE || mp == 0 ||
		    mp == TO_NEXUS || mp->d_type == MASTER ||
		    eq(mp->d_name, "mba"))
			continue;
		if (eq(mp->d_name, "mb")) {
			if (dp->d_pri == 0) {
				printf("must specify vector for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr == 0) {
				printf("must specify csr for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
				printf("drives/slaves can be specified only ");
				printf("for controllers, not for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = QUES;
		} else {
			if (mp->d_conn == 0) {
				printf("%s%d isn't connected to anything, ",
				    mp->d_name, mp->d_unit);
				printf("so %s%d is unattached\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive == UNKNOWN) {
				printf("must specify ``drive number'' for %s%d\n",
				   dp->d_name, dp->d_unit);
				continue;
			}
			/* NOTE THAT ON THE UNIBUS ``drive'' IS STORED IN */
			/* ``SLAVE'' AND WE DON'T WANT A SLAVE SPECIFIED */
			if (dp->d_slave != UNKNOWN) {
				printf("slave numbers should be given only ");
				printf("for massbus tapes, not for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_pri != 0) {
				printf("interrupt priority should not be ");
				printf("given for drive %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr != 0) {
				printf("csr addresses should be given only");
				printf("on controllers, not on %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = dp->d_drive;
		}
		fprintf(fp,
"\t{ &%sdriver,  %2d,   %s,    %2d,   C 0x%x, %d,  %d,  0x%x },\n",
		    eq(mp->d_name, "mb") ? dp->d_name : mp->d_name, dp->d_unit,
		    eq(mp->d_name, "mb") ? " -1" : qu(mp->d_unit),
		    slave, dp->d_addr, dp->d_pri, dp->d_dk, dp->d_flags);
	}
	fprintf(fp, "\t0\n};\n");
	(void) fclose(fp);
}
#endif	CMU
#endif

#if MACHINE_ROMP
romp_ioconf()
{
	register struct device *dp, *mp;
	register int slave;
	FILE *fp;

	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"sys/param.h\"\n");
	fprintf(fp, "#include \"sys/buf.h\"\n");
	fprintf(fp, "#include \"sys/vm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include \"caio/ioccvar.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#define C (caddr_t)\n\n");
	fprintf(fp, "\n");

	fprintf (fp, "struct     iocc_hd iocc_hd[] = {{C 0xF0000000,}};\n");
	/*
	 * Now generate interrupt vectors for the  Winnerbus
	 */
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_pri != 0) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			    !eq(mp->d_name, "iocc"))
				continue;
			fprintf(fp, "extern struct iocc_driver %sdriver;\n",
			    dp->d_name);
		}
	}
	/*
	 * Now spew forth the iocc_cinfo structure
	 */
	fprintf(fp, "\nstruct iocc_ctlr iocccinit[] = {\n");
	fprintf(fp, "/*\t driver,\tctlr,\talive,\taddr,\tintpri */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_type != CONTROLLER || mp == TO_NEXUS || mp == 0 ||
		    !eq(mp->d_name, "iocc") || (dp->d_unit == QUES &&
		    				eq(dp->d_name,"hdc")))
			continue;
		if (dp->d_pri == 0) {
			printf("must specify priority for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_addr == 0) {
			printf("must specify csr address for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
			printf("drives need their own entries; ");
			printf("dont specify drive or slave for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_flags) {
			printf("controllers (e.g. %s%d) don't have flags, ");
			printf("only devices do\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		fprintf(fp, "\t{ &%sdriver,\t%d,\t0,\tC 0x%x,\t%d },\n",
		    dp->d_name, dp->d_unit, dp->d_addr, dp->d_pri);
	}
	fprintf(fp, "\t0\n};\n");
	/*
	 * Now we go for the iocc_device stuff
	 */
	fprintf(fp, "\nstruct iocc_device ioccdinit[] = {\n");
	fprintf(fp,
"\t/* driver,  unit, ctlr,  slave,   addr,    pri,    dk, flags*/\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_unit == QUES || dp->d_type != DEVICE || mp == 0 ||
		    mp == TO_NEXUS || mp->d_type == MASTER ||
		    eq(mp->d_name, "iocca"))
			continue;
		if (eq(mp->d_name, "iocc")) {
			if (dp->d_pri == 0) {
				printf("must specify vector for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr == 0) {
				printf("must specify csr for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
				printf("drives/slaves can be specified only ");
				printf("for controllers, not for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = QUES;
		} else {
			if (mp->d_conn == 0) {
				printf("%s%d isn't connected to anything, ",
				    mp->d_name, mp->d_unit);
				printf("so %s%d is unattached\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive == UNKNOWN) {
				printf("must specify ``drive number'' for %s%d\n",
				   dp->d_name, dp->d_unit);
				continue;
			}
			/* NOTE THAT ON THE UNIBUS ``drive'' IS STORED IN */
			/* ``SLAVE'' AND WE DON'T WANT A SLAVE SPECIFIED */
			if (dp->d_slave != UNKNOWN) {
				printf("slave numbers should be given only ");
				printf("for massbus tapes, not for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_pri != 0) {
				printf("interrupt priority should not be ");
				printf("given for drive %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr != 0) {
				printf("csr addresses should be given only");
				printf("on controllers, not on %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = dp->d_drive;
		}
		fprintf(fp,
"\t{ &%sdriver,  %2d,   %s,    %2d,   C 0x%x, %d,  %d,  0x%x },\n",
		    eq(mp->d_name, "iocc") ? dp->d_name : mp->d_name, dp->d_unit,
		    eq(mp->d_name, "iocc") ? " -1" : qu(mp->d_unit),
 		    slave, dp->d_addr, dp->d_pri, dp->d_dk, dp->d_flags);
 	}
 	fprintf(fp, "\t0\n};\n");
 	(void) fclose(fp);
} 

#endif	MACHINE_ROMP

#if	MACHINE_MMAX
mmax_ioconf()
{
	register struct device *dp, *dp1, *mp;
	register int slave;
	FILE *fp;
	int	unit;

	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"mmaxio/io.h\"\n\n");

	/*
	 *	Multimax code is a little messy because we have to
	 * 	scan the entire list for each device to generate the
	 * 	structures correctly.  We cheat and use the d->d_pri
	 *	field to avoid doing anything twice.  -1000 is an obvious
	 *	bogus value for this field.
	 */

	for (dp1 = dtab; dp1 != 0; dp1 = dp1->d_next) {
	    /* 
	     *	If pri is not -1000, then haven't seen device yet.
	     */
	    if (dp1->d_pri != -1000) switch (dp1->d_type) {

	    case CONTROLLER:
		fprintf(fp,"struct devaddr %s_devaddr[] = {\n",
			dp1->d_name);
		/*
		 *	Now scan entire list and get all of them.  Use
		 *	unit to make sure unit numbers are right.
		 */
		unit = 0;
		for (dp = dp1; dp != 0; dp = dp->d_next) {
			if (!strcmp(dp->d_name, dp1->d_name)) {
				mp = dp->d_conn;
				if (mp != TO_SLOT) {
		printf("%s%d: controller must be connected to slot.\n",
						dp->d_name, dp->d_unit);
					exit(1);
				}
				if (dp->d_vec != 0) {
		printf("%s%d: cannot configure multimax interrupt vectors.\n",
						dp->d_name, dp->d_unit);
				}
				if (dp->d_pri != 0) {
		printf("%s%d: interrupt priority is nonsense on multimax.\n",
						dp->d_name, dp->d_unit);
				}
				if ((dp->d_drive != UNKNOWN) ||
					(dp->d_slave !=UNKNOWN)) {
		printf("%s%d: don't specify drive or slave for controller.\n",
						dp->d_name, dp->d_unit);
				}
				/*
				 *	Fix unit number if bogus
				 */
				if(dp->d_unit != unit) {
	printf("Warning: %s%d configured as %s%d -- fix config file.\n",
		dp->d_name,dp->d_unit,dp->d_name,unit);
					dp->d_unit = unit;
				}
				unit++;
				fprintf(fp,"\t{ %d, 0, 0},\n",dp->d_addr);
				dp->d_pri = -1000; /* done this one */
			}
		}
		fprintf(fp,"} ;\n\n");
		break;

	    case DEVICE:
		fprintf(fp,"struct subdevaddr %s_subdevaddr[] = {\n",
			dp1->d_name);
		/*
		 *	Now scan entire list and get all of them.  Use
		 *	unit to make sure unit numbers are right.
		 */
		unit = 0;
		for (dp = dp1; dp != 0; dp = dp->d_next) {
			if (!strcmp(dp->d_name, dp1->d_name)) {
				mp = dp->d_conn;
				if ( (mp == 0) || (mp == TO_SLOT) ||
					(mp->d_type != CONTROLLER)) {
				printf("%s%d: device has no controller.\n",
						dp->d_name, dp->d_unit);
					exit(1);
				}
				if (dp->d_vec != 0) {
		printf("%s%d: cannot configure multimax interrupt vectors.\n",
						dp->d_name, dp->d_unit);
				}
				if (dp->d_pri != 0) {
		printf("%s%d: interrupt priority is nonsense on multimax.\n",
						dp->d_name, dp->d_unit);
				}
				if ((dp->d_drive != UNKNOWN) ||
					(dp->d_slave !=UNKNOWN)) {
		printf("%s%d: use 'unit' instead of 'drive' or 'slave'.\n",
						dp->d_name, dp->d_unit);
				}
				/*
				 *	Fix unit number if bogus
				 */
				if(dp->d_unit != unit) {
	printf("Warning: %s%d configured as %s%d -- fix config file.\n",
				dp->d_name,dp->d_unit,dp->d_name,unit);
					dp->d_unit = unit;
				}
				unit++;
				if((dp->d_addr == 0) || (dp->d_addr == QUES)){
			printf("%s%d: must specify logical unit number.\n",
					dp->d_name,dp->d_unit);
					exit(1);
				}
				fprintf(fp,"\t{ %d, %d, 0},\n",mp->d_unit,
					dp->d_addr);
				dp->d_pri = -1000; /* don't do this again */
			}
		}
		fprintf(fp,"} ;\n\n");
		break;

	    case PSEUDO_DEVICE:
		/*
		 *	Doesn't exist as far as ioconf.c is concerned.
		 */
		break;

	    default:
		printf("Bogus device type for %s\n", dp1->d_name);
		exit(1);
		break;
	    }
	}
	
	(void) fclose(fp);
}

#endif	MACHINE_MMAX

#if	MACHINE_SQT

/*
 * Define prototype device spec lines.
 *
 * For now, have static set of controller prototypes.  This should be
 * upgraded to using (eg) controllers.balance (ala Sequent /etc/config)
 * to support custom boards without need to edit this file.
 */

/*
 *  flags for indicating presence of upper and lower bound values
 */

#define	P_LB	1
#define	P_UB	2

struct p_entry {
	char 	*p_name;			/* name of field */
	long	p_def;				/* default value */
	long 	p_lb;				/* lower bound for field */
	long	p_ub;				/* upper bound of field */ 
	char	p_flags;			/* bound valid flags */
};

struct proto {
	char	*p_name;			/* name of controller type */
	struct  p_entry	p_fields[NFIELDS];	/* ordered list of fields */
	int	p_seen;				/* any seen? */
};

/*
 * MULTIBUS Adapter:
 *	type mbad  index csr flags maps[0,256] bin[0,7] intr[0,7]
 */

static	struct	proto	mbad_proto = {
	"mbad",
	{
	{ "index",	0,	0,	0,	0 },
	{ "csr",	0,	0,	0,	0 },
	{ "flags",	0,	0,	0,	0 },
	{ "maps",	0,	0,	256,	P_LB|P_UB },
	{ "bin",	0,	0,	7,	P_LB|P_UB },
	{ "intr",	0,	0,	7,	P_LB|P_UB },
	}
};

/*
 * SCSI/Ether Controller:
 *	type sec   flags bin[0,7] req doneq index target[0,7]=-1 unit
 */

static	struct	proto	sec_proto = {
	"sec",
	{
	{ "flags",	0,	0,	0,	0 },
	{ "bin",	0,	0,	7,	P_LB|P_UB } ,
	{ "req",	0,	0,	0,	0 },
	{ "doneq",	0,	0,	0,	0 },
	{ "index",	0,	0,	0,	0 },
	{ "target",	-1,	0,	7,	P_LB|P_UB },
	{ "unit",	0,	0,	0,	0 },
	}
};

/*
 * "Zeke" (FAST) Disk Controller (Dual-Channel Disk Controller):
 *	type zdc index[0,31] drive[-1,7] drive_type[-1,1]
 *
 * Levgal values for drive_type:
 *	M2333K = 0	(swallow)
 *	M2351A = 1	(eagle)
 *	wildcard = -1	(run-time determined)
 */

static	struct	proto	zdc_proto = {
	"zdc",
	{
	{ "index",	0,	0,	31,	P_LB|P_UB },
	{ "drive",	0,	-1,	7,	P_LB|P_UB },
	{ "drive_type",	0,	-1,	1,	P_LB|P_UB },
	}
};

static	struct	proto	*ptab[] = {
	&mbad_proto,
	&sec_proto,
	&zdc_proto,
	(struct proto *) 0
};

/*
 * locate a prototype structure in the queue of such structures.
 * return NULL if not found.
 */

static struct proto *
find_proto(str)
	register char *str;
{
	register struct proto *ptp;
	register int	ptbx;

	for (ptbx = 0; (ptp = ptab[ptbx]) != NULL; ptbx++) {
		if (eq(str, ptp->p_name))
			return(ptp);
	}
	return(NULL);
}

dev_param(dp, str, num)
	register struct device *dp;
	register char *str;
	long	num;
{
	register struct p_entry *entry;
	register struct proto *ptp;

	ptp = find_proto(dp->d_conn->d_name);
	if (ptp == NULL) {
		fprintf(stderr,"dev %s cont %s", dp->d_name, dp->d_conn->d_name);
		yyerror("invalid controller");
		return;
	}

	for (entry = ptp->p_fields; entry->p_name != NULL; entry++) {
		if (eq(entry->p_name, str)) {
			if ((entry->p_flags & P_LB) && (num < entry->p_lb)) {
				yyerror("parameter below range");
				return;
			}
			if ((entry->p_flags & P_UB) && (num > entry->p_ub)) {
				yyerror("parameter above range");
				return;
			}
			dp->d_fields[entry-ptp->p_fields] = num;
			return;
		}
	}

	yyerror("invalid parameter");
}

sqt_ioconf()
{
	register struct device *dp, *mp;
	register int count;
	register char *namep;
	register struct proto *ptp;
	register struct p_entry *entry;
	FILE	*fp;
	int	bin_table[8];
	int	ptbx;
	int	found;

	for (count = 0; count < 8; count++)
		bin_table[count] = 0;
	fp = fopen(path("ioconf.c"), "w");
	if (fp == NULL) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include	\"sys/param.h\"\n");
	fprintf(fp, "#include	\"sys/systm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include	\"machine/ioconf.h\"\n");

	fprintf(fp, "\nu_long\tMBAd_IOwindow =\t\t3*256*1024;\t/* top 1/4 Meg */\n\n");

	for (ptbx = 0; (ptp = ptab[ptbx]) != NULL; ptbx++) {

		fprintf(fp, "/*\n");
		fprintf(fp, " * %s device configuration.\n", ptp->p_name);
		fprintf(fp, " */\n\n");
		fprintf(fp, "\n");
		fprintf(fp, "#include	\"sqt%s/ioconf.h\"\n", ptp->p_name);
		fprintf(fp, "\n");

		/*
		 * Generate dev structures for this controller
		 */
		for (dp = dtab, namep = NULL; dp != 0; dp = dp->d_next) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			   !eq(mp->d_name, ptp->p_name) ||
			   (namep != NULL && eq(dp->d_name, namep)) )
				continue;
			fprintf(fp, "extern\tstruct\t%s_driver\t%s_driver;\n",
			    ptp->p_name, namep = dp->d_name);
			ptp->p_seen = 1;
		}

		found = 0;
		for (dp = dtab, namep = NULL; dp != 0; dp = dp->d_next) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			   !eq(mp->d_name, ptp->p_name))
				continue;
			if (namep == NULL || !eq(namep, dp->d_name)) {
				count = 0;
				if (namep != NULL) 
					fprintf(fp, "};\n");
				found = 1;
				fprintf(fp, "\nstruct\t%s_dev %s_%s[] = {\n",
						ptp->p_name,
						ptp->p_name,
						namep = dp->d_name);
				fprintf(fp, "/*");
				entry = ptp->p_fields;
				for (; entry->p_name != NULL; entry++)
					fprintf(fp, "\t%s",entry->p_name);
				fprintf(fp, " */\n");
			}
			if (dp->d_bin != UNKNOWN)
				bin_table[dp->d_bin]++;
			fprintf(fp, "{");
			for (entry = ptp->p_fields; entry->p_name != NULL; entry++) {
				if (eq(entry->p_name,"index"))
					fprintf(fp, "\t%d,", mp->d_unit);
				else
					fprintf(fp, "\t%d,",
						dp->d_fields[entry-ptp->p_fields]);
			}
			fprintf(fp, "\t},\t/* %s%d */\n", dp->d_name, count++);
		}
		if (found)
			fprintf(fp, "};\n\n");

		/*
	 	* Generate conf array
	 	*/
		fprintf(fp, "/*\n");
		fprintf(fp, " * %s_conf array collects all %s devices\n", 
			ptp->p_name, ptp->p_name);
		fprintf(fp, " */\n\n");
		fprintf(fp, "struct\t%s_conf %s_conf[] = {\n", 
			ptp->p_name, ptp->p_name);
		fprintf(fp, "/*\tDriver\t\t#Entries\tDevices\t\t*/\n");
		for (dp = dtab, namep = NULL; dp != 0; dp = dp->d_next) {
			mp = dp->d_conn;
			if (mp == 0 || mp == TO_NEXUS ||
			   !eq(mp->d_name, ptp->p_name))
				continue;
			if (namep == NULL || !eq(namep, dp->d_name)) {
				if (namep != NULL)
					fprintf(fp, 
			"{\t&%s_driver,\t%d,\t\t%s_%s,\t},\t/* %s */\n",
			namep, count, ptp->p_name, namep, namep);
				count = 0;
				namep = dp->d_name;
			}
			++count;
		}
		if (namep != NULL) {
			fprintf(fp, 
			  "{\t&%s_driver,\t%d,\t\t%s_%s,\t},\t/* %s */\n",
			  namep, count, ptp->p_name, namep, namep);
		}
		fprintf(fp, "\t{ 0 },\n");
		fprintf(fp, "};\n\n");

	}

	/*
	 * Pseudo's
	 */

	fprintf(fp, "/*\n");
	fprintf(fp, " * Pseudo-device configuration\n");
	fprintf(fp, " */\n\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type == PSEUDO_DEVICE) {
			fprintf(fp, "extern\tint\t%sboot();\n", dp->d_name);
		}
	}
	fprintf(fp, "\nstruct\tpseudo_dev pseudo_dev[] = {\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type == PSEUDO_DEVICE) {
			fprintf(fp, "\t{ \"%s\",\t%d,\t%sboot,\t},\n",
				dp->d_name, 
				dp->d_slave == UNKNOWN ? 32 : dp->d_slave, 
				dp->d_name);
		}
	}
	fprintf(fp, "\t{ 0 },\n");
	fprintf(fp, "};\n\n");

	/*
	 * Bin interrupt table and misc
	 */

	fprintf(fp, "/*\n");
	fprintf(fp, " * Interrupt table\n");
	fprintf(fp, " */\n\n");
	fprintf(fp, "int\tbin_intr[8] = {\n");
	fprintf(fp, "\t\t0,\t\t\t\t/* bin 0, always zero */\n");
	for (count=1; count < 8; count++) {
		fprintf(fp, "\t\t%d,\t\t\t\t/* bin %d */\n", 
			bin_table[count], count);
	}
	fprintf(fp, "};\n");

	/*
	 * b8k_cntlrs[]
	 */

	fprintf(fp, "/*\n");
	fprintf(fp, " * b8k_cntlrs array collects all controller entries\n");
	fprintf(fp, " */\n\n");
	for (ptbx = 0; (ptp = ptab[ptbx]) != NULL; ptbx++) {
		if (ptp->p_seen)
			fprintf(fp, "extern int  conf_%s(),\tprobe_%s_devices(),\t%s_map();\n",
				ptp->p_name, ptp->p_name, ptp->p_name);
	}
	fprintf(fp, "\n\nstruct\tcntlrs b8k_cntlrs[] = {\n");
	fprintf(fp, "/*\tconf\t\tprobe_devs\t\tmap\t*/\n");

	for (ptbx = 0; (ptp = ptab[ptbx]) != NULL; ptbx++) {
		if (ptp->p_seen)
			fprintf(fp, "{\tconf_%s,\tprobe_%s_devices,\t%s_map\t}, \n",
				ptp->p_name, ptp->p_name, ptp->p_name);
	}
	fprintf(fp, "{\t0,\t},\n");
	fprintf(fp, "};\n");

	(void) fclose(fp);
}

#endif	MACHINE_SQT

#if	MACHINE_NeXT
NeXT_ioconf()
{
	register struct device *dp, *mp;
	register int slave;
	register struct idlst *vp;
	FILE *fp;

	unlink(path("ioconf.c"));
	fp = fopen(path("ioconf.c"), "w");
	if (fp == 0) {
		perror(path("ioconf.c"));
		exit(1);
	}
	fprintf(fp, "#include \"sys/param.h\"\n");
	fprintf(fp, "#include \"sys/buf.h\"\n");
	fprintf(fp, "#include \"sys/vm.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#include \"nextdev/busvar.h\"\n");
	fprintf(fp, "\n");
	fprintf(fp, "#define C (caddr_t)\n");
	fprintf(fp, "\n");

	/*
	 * Now generate interrupt vectors for the bus
	 */
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (mp == TO_NEXUS || mp == 0 || mp->d_conn != TO_NEXUS)
			continue;
		fprintf(fp, "extern struct bus_driver %sdriver;\n",
			    dp->d_name);
	}

	/*
	 * Now spew forth the bus_ctrl structures
	 */
	fprintf(fp, "\nstruct bus_ctrl bus_cinit[] = {\n");
	fprintf(fp,
"  /* driver        ctrl   ipl         address */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_type != CONTROLLER || mp == TO_NEXUS || mp == 0 ||
		    mp->d_conn != TO_NEXUS || dp->d_unit == QUES)
			continue;
		if (dp->d_addr == UNKNOWN) {
			printf("must specify csr address for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
			printf("drives need their own entries; ");
			printf("don't specify drive or slave for %s%d\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		if (dp->d_flags) {
			printf("controllers (e.g. %s%d) don't have flags, ");
			printf("only devices do\n",
			    dp->d_name, dp->d_unit);
			continue;
		}
		fprintf(fp,
"  {  %-12s %5d, %4d,   C 0x%08x },\n",
		    dn(dp->d_name), dp->d_unit, dp->d_pri, dp->d_addr);
	}
	fprintf(fp, "  0\n};\n");

	/*
	 * Now we go for the bus_device stuff
	 */
	fprintf(fp, "\nstruct bus_device bus_dinit[] = {\n");
	fprintf(fp,
"  /* driver      unit ctrl slave ipl  dk       flags       address  name */\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		mp = dp->d_conn;
		if (dp->d_unit == QUES || dp->d_type != DEVICE || mp == 0 ||
		    mp == TO_NEXUS || mp->d_type == MASTER)
			continue;
		if (mp->d_conn == TO_NEXUS) {
			if (dp->d_addr == UNKNOWN) {
				printf("must specify csr for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive != UNKNOWN || dp->d_slave != UNKNOWN) {
				printf("drives/slaves can be specified only ");
				printf("for controllers, not for device %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = UNKNOWN;
		} else {
			if (mp->d_conn == 0) {
				printf("%s%d isn't connected to anything, ",
				    mp->d_name, mp->d_unit);
				printf("so %s%d is unattached\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_drive == UNKNOWN) {
				printf("must specify ``drive number'' for %s%d\n",
				   dp->d_name, dp->d_unit);
				continue;
			}
			/* NOTE THAT ON THE UNIBUS ``drive'' IS STORED IN */
			/* ``SLAVE'' AND WE DON'T WANT A SLAVE SPECIFIED */
			if (dp->d_slave != UNKNOWN) {
				printf("slave numbers should be given only ");
				printf("for massbus tapes, not for %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_pri != 0) {
				printf("interrupt priority should not be ");
				printf("given for drive %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			if (dp->d_addr != 0) {
				printf("csr addresses should be given only");
				printf(" on controllers, not on %s%d\n",
				    dp->d_name, dp->d_unit);
				continue;
			}
			slave = dp->d_drive;
		}
		fprintf(fp,
"  {  %-12s %3d, %s,  %s,%3d,%3d, %#10x, C 0x%08x, \"%s\" },\n",
		    dn(mp->d_conn == TO_NEXUS? dp->d_name : mp->d_name),
		    dp->d_unit,
		    mp->d_conn == TO_NEXUS? " -1" : qu(mp->d_unit),
		    qu(slave),
		    dp->d_pri, -dp->d_dk, dp->d_flags,
		    dp->d_addr == UNKNOWN? 0 : dp->d_addr,
		    dp->d_name);
	}
	fprintf(fp, "  0\n};\n");
	NeXT_pseudo_inits (fp);
	(void) fclose(fp);
}

NeXT_pseudo_inits(fp)
	FILE *fp;
{
	register struct device *dp;
	int count;

	fprintf(fp, "\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type != PSEUDO_DEVICE || dp->d_init == 0)
			continue;
		fprintf(fp, "extern %s();\n", dp->d_init);
	}
	fprintf(fp, "\nstruct pseudo_init pseudo_inits[] = {\n");
	for (dp = dtab; dp != 0; dp = dp->d_next) {
		if (dp->d_type != PSEUDO_DEVICE || dp->d_init == 0)
			continue;
		count = dp->d_slave;
		if (count <= 0)
			count = 1;
		fprintf(fp, "\t%d,\t%s,\n", count, dp->d_init);
	}
	fprintf(fp, "\t0,\t0,\n};\n");
}

char *
dn(name)
char *name;
{
	return(sprintf(errbuf, "&%sdriver,", name));
}
#endif	MACHINE_NeXT

char *intv(dev)
	register struct device *dev;
{
	static char buf[20];

	if (dev->d_vec == 0)
		return ("     0");
	return (sprintf(buf, "%sint%d", dev->d_name, dev->d_unit));
}

char *
qu(num)
{

	if (num == QUES)
		return ("'?'");
	if (num == UNKNOWN)
		return (" -1");
#if	MACHINE_NeXT
	return (ns(sprintf(errbuf, "%3d", num)));
#else	MACHINE_NeXT
	return (sprintf(errbuf, "%3d", num));
#endif	MACHINE_NeXT
}
