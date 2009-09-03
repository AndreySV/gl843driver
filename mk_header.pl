#!/usr/bin/perl

use strict;
require("regmapper.pl");

# Main

if (@ARGV != 2) {
	printf("Usage: ./mk_header <regmap.txt> <devname>\n");
	printf("Example: ./mk_header gl843_regmap.txt gl843\n");
	exit(1);
}

my $x = new Regmapper(@ARGV[0]);
$x->dump_devregs_c(@ARGV[1]);
