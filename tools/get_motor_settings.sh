#!/bin/sh

./parsedump.pl $1 | cut -d ' ' -f 2- | grep -v "rd_r" | cat -n > tmp.log
egrep 'wr_r ac|MTRPWR| SCAN | MOVE |STEPTIM|FWDSTEP|BWDSTEP|FSTPSEL|STEPSEL|FASTNO|STEPNO|FMOVDEC|FMOVNO|FSHDEC' \
	tmp.log | \
	sed \
	-e s/"RAMDLY = ., MOTLAG = ., CMODE = ., "//g \
	-e s/"CISSET = ., DOGENB = ., DVDSET = ., STAGGER = ., COMPENB = ., TRUEGRAY = ., SHDAREA = ., "//g \
	-e s/"MULDMYLN = ., IFRS = ., "//g \
	-e s/"HOMENEG = ., LONGCURV = 0, "//g \
	-e s/"MTRPWM = .., "//g -e s/"FASTPWM = .., "//g \
	> tmp_settings.txt
egrep 'MTRTBL = 1' -A 3 tmp.log | sed -e /^--$/d -e /"wr_r 5c"/d -e /"wr_r 28"/d > tmp_data.txt
sort -m -n tmp_settings.txt tmp_data.txt | cut -c 8- | uniq
