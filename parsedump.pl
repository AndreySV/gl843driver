#!/usr/bin/perl
#
# parsedump.pl - prints commands to/from a GL843 scanner in human-readable form.
#
#    Copyright (C) 2009  Andreas Robinson <andr345 at gmail.com>
#
#    This program is free software: you can redistribute it and/or modify
#    it under the terms of the GNU General Public License as published by
#    the Free Software Foundation, either version 2 of the License, or
#    (at your option) any later version.
#
#    This program is distributed in the hope that it will be useful,
#    but WITHOUT ANY WARRANTY; without even the implied warranty of
#    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#    GNU General Public License for more details.
#
#    You should have received a copy of the GNU General Public License
#    along with this program.  If not, see <http://www.gnu.org/licenses/>.

use Class::Struct;
use feature qw{ switch };
use POSIX qw(ceil floor);
use strict;
require("regmapper.pl");

my $regmap = new Regmapper("gl843_regmap.txt");

sub print_reg
{
	my ($reg, $val) = @_;

	$regmap->set_ioreg_val($reg, $val);
	my @changed = $regmap->get_devregs_at_ioreg($reg);
	foreach my $reg (@changed) {
		my $val = $regmap->get_devreg_val($reg);
		printf("$reg = $val, ");
	}
	printf("\n");
}

sub byte2hex
{
	my @r = ();
	push(@r, sprintf("%02x", $_)) foreach (@_);
	return ((@r == 1) ? pop(@r) : return @r);
}

sub read_log
{
	my ($fname) = @_;

	my $hdrbuf;

	my $ts;
	my $t0 = -1;

	my $cmd;
	my $prev_cmd = 'x';
	my $have_data;
	my $blen;
	my $mtr_gamma = 0;

	my $sel_reg = "00";

	open(scanner_log, $fname) or die "Could not open $fname: $!";

	while (read(scanner_log, $hdrbuf, 8)) {

		($ts, $cmd, $have_data, $blen) = unpack("NCCn", $hdrbuf);
		my $offset = tell(scanner_log);
		$cmd = chr($cmd);

		if ($t0 < 0) {
			$t0 = $ts;
			$ts = 0;
		} else {
			$ts = $ts - $t0;
		}

		my $data;

		if ($have_data == 1) {
			read(scanner_log, $data, $blen)
				or die("Can't read $fname: $!");
		}

		given ($cmd) {
			when('r') { # Request value of selected scanner IO register
				# Print nothing
			}
			when('w') { # Write byte to scanner IO register
				my ($reg, $val) = byte2hex(unpack("CC", $data));
				printf("$ts wr_r $reg <- $val -- ");
				print_reg($reg, $val);
			}
			when('s') { # Select scanner IO register
				$sel_reg = byte2hex(unpack("C", $data));
			}
			when('d') { # Write bytes to selected scanner IO register
				printf("$ts wr_r $sel_reg <- ");
				if ($blen < 17) {
					printf("-- " . join(" ",
						byte2hex(unpack("C*", $data))));
				} else {
					printf("$blen bytes @ 0x%x", $offset);
				}
				$mtr_gamma = ($sel_reg eq '28') ? 1 : 0;
				printf("\n");
			}
			when('R') { # Request bulk data from scanner
				# Print nothing
			}
			when('W') { # Send bulk data to scanner
				printf("$ts wr_b => $blen bytes @ 0x%x ", $offset);
				if ($mtr_gamma eq '1') {
					# Print first and last word of a motor or gamma table
					printf("data (gmmaddr=%d): %d ... %d",
						$regmap->get_devreg_val("GMMADDR"),
						unpack("v", $data),
						vec($data,$blen-2,8) + vec($data, $blen-1, 8) * 256);
				}
				printf("\n");
			}
			when('a') { # Scanner returns data
				if ($prev_cmd eq 'r' && $sel_reg ne '6d') {
					# Print register read
					my $val = byte2hex(unpack("C", $data));
					printf("$ts rd_r $sel_reg -> $val -- ");
					print_reg($sel_reg, $val);
				} elsif ($prev_cmd eq 'r' && $sel_reg eq '6d') {
					# Don't print register 6d
					# where the Canoscan 4400F Win32 driver
					# polls the scanner buttons.
				} else {
					printf("$ts rd_c => $blen bytes @ 0x%x\n", $offset);
				}
			}
			when('b') { # Scanner acks receiving data
				# Print nothing
			}
			when('A') { # Scanner returns bulk data
				printf("$ts rd_b => $blen bytes @ 0x%x\n", $offset);
			}
			when('B') { # Scanner acks receiving bulk data
				# Print nothing
			}
			when('x') { # Unhandled URB
				printf("$ts x (unhandled URB) @ 0x%x\n", $offset);
			}
		}
		$prev_cmd = $cmd;
	}
}

# Main

if (@ARGV != 1) {
	printf("Usage: ./parsedump <dumpscanner_log.bin>\n");
	exit(1);
}
read_log(@ARGV[0]);
