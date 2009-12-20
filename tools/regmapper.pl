#!/usr/bin/perl
#
# regmapper.pl - a register-number to register-name map
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


package Regmapper;

use Class::Struct;
use feature qw{ switch };
use strict;

# I/O register
struct( ioregister => {
	addr => '$',	# Address in IO space
	devregs => '@',	# Names of device registers at this address (strings)
	value => '$',	# Current value stored at this address
	oldval => '$',	# TODO: Previous value
});

# I/O register <=> device register mapping
struct( regbitmap => {
	io_addr => '$',
	io_lsb => '$',
	io_msb => '$',
	dev_lsb => '$',
	dev_msb => '$',
});

# Device register
struct( devregister => {
	name => '$',	# Register name
	width => '$',	# Number of bits
	regbm => '@',	# regbitmap array (array of struct regbitmap)
});

my $bus_width = 8;

# Parse register map text file.
# Will return the map in %ioregs and %devregs

sub Regmapper::new {

	my $class = shift;
	my ($fname) = @_;
	my $s;
	my $regmap;

	my $ioregs = {}; # key: I/O register address, val: struct ioregister
	my $devregs = {}; # key: register name, val: struct devregister

	my $self = {};

	bless $self, $class;

	open(regmap, $fname) or die ("Can't open $fname\n");
	while ($s=<regmap>) {
		chomp($s);
		next if ($s =~ /^[ \t]*#/); # Ignore lines starting with #

		my @reg = split(/ +/, $s); # Space separated columns
		my $addr = shift(@reg);	# I/O register address
		my $bit = $bus_width - 1;

		$addr =~ tr/A-F/a-f/;

		foreach (@reg) {
			my $name;
			my $msb;
			my $lsb;
			# Process device registers (MSB -> LSB in I/O register)
			given ($_) {
				when (/(.*)\[([0-9]*):([0-9]*)\]/) {
					# Match $name[$msb:$lsb]
					# (multi-bit device register)
					$name = $1;
					$msb = $2;
					$lsb = $3;
				}
				when (/(.*)\[([0-9]*)\]/) {
					# Match $name[$lsb]
					# (single bit of multi-bit dev. register)
					$name = $1;
					$msb = $2;
					$lsb = $2;
				}
				when (/-/) {
					# Match unused bit
					$bit = $bit - 1;
					next;
				}
				default {
					# Match $name
					# (single-bit device register)
					$name = $_;
					$msb = 0;
					$lsb = 0;
				}
			}

			# Create/update I/O register object

			my $ioreg;

			if (exists ($ioregs->{$addr})) {
				$ioreg = $ioregs->{$addr};
			} else {
				$ioreg = new ioregister;
				$ioreg->addr($addr);
				$ioregs->{$addr} = $ioreg;
			}

			# Associate named device register with the I/O register

			push(@{$ioreg->devregs}, $name);

			# Create/update device register object

			my $devreg;

			if (exists ($devregs->{$name})) {
				$devreg = $devregs->{$name};
			} else {
				$devreg = new devregister;
				$devreg->name($name);
				$devreg->width(0);
				$devregs->{$name} = $devreg;
			}

			# Store device register bitmaps and
			# location in the I/O-register file,
			# with the device register.

			if ($devreg->width < $msb + 1) {
				$devreg->width($msb + 1);
			}

			my $regbm = new regbitmap;

			$regbm->io_addr($addr);
			$regbm->io_lsb($bit - ($msb - $lsb));
			$regbm->io_msb($bit);
			$regbm->dev_lsb($lsb);
			$regbm->dev_msb($msb);
			push(@{$devreg->regbm}, $regbm);
			$bit = $bit - ($msb - $lsb) - 1;
		}
	}
	close(regmap);

	$self->{_ioregs} = $ioregs;
	$self->{_devregs} = $devregs;

	return $self;
}

sub Regmapper::dump_devregs
{
	my $self = shift;
	my $devregs = $self->{_devregs};

	while ( my ($name, $devreg) = each(%$devregs) ) {
		my $regbm;
		print "-----------\n";
		print "$name (" . $devreg->width;
		print (($devreg->width == 1) ? " bit):\n" : " bits):\n");
		my @dev_regbm = @{$devreg->regbm};
		foreach $regbm (@dev_regbm) {
			my $dlsb = $regbm->dev_lsb;
			my $dmsb = $regbm->dev_msb;
			my $addr = $regbm->io_addr;
			my $iolsb = $regbm->io_lsb;
			my $iomsb = $regbm->io_msb;

			if ($dlsb == $dmsb) {
				print "bit $dlsb is mapped to bit $iolsb" .
					" at address $addr.\n";
			} else {
				print "bits $dlsb to $dmsb are mapped to bits" .
					" $iolsb to $iomsb at address $addr.\n";
			}
		}
	}
}

sub Regmapper::print_array_c
{
	my ($self, $indent, $width, @array) = @_;
	my $len = 0;
	my $ret = "";

	foreach my $s (@array) {
		$len = $len + length("$s, ");
		if ($len > $width) {
			$ret = $ret . "\n$indent";
			$len = 0;
		}
		$ret = $ret . "$s, ";
	}
	return $ret;
}

# Dump register definitions and map as C source code
sub Regmapper::dump_devregs_c
{
	my ($self, $devname) = @_;
	my $DEVNAME = $devname;
	$DEVNAME =~ tr/a-z/A-Z/;

	my $devregs = $self->{_devregs};
	my $len = 0;
	my $regmap_size = 0;
	my $max_ioaddr = 0;

	my $regmap_index = 0;
	my @regmap_indices = ();# Index of first regmap_ent for each register

	# Build device register enums

	my @enums = sort(keys(%$devregs));
	foreach my $name (@enums) { $name = "$DEVNAME\_$name"; }
	my $enum_array = $self->print_array_c("\t", 50, @enums);

	# Build device register name strings

	my @regnames = sort(keys(%$devregs));
	foreach my $name (@regnames) { $name = "\"$name\""; }
	my $names_array = $self->print_array_c("\t", 50, @regnames);

	# Count IO registers

	foreach my $name (sort(keys(%$devregs))) {
		my %h = %$devregs;
		my $devreg = $h{$name};
		my @dev_regbm = @{$devreg->regbm};
		foreach my $regbm (@dev_regbm) {
			my $addr = hex($regbm->io_addr);
			if ($addr > $max_ioaddr) {
				$max_ioaddr = $addr;
			}
		}
	}

	# Build register map

	my $regmap_struct = "";

	# Build IO register map (trivial 1:1 mapping)
	for (my $addr = 0; $addr <= $max_ioaddr; $addr++) {
		push(@regmap_indices, $regmap_index);
		$regmap_struct = $regmap_struct .
			sprintf ("{ 0x%x, 0x%x, 0, 0xff },\n\t", $addr, $addr);
		$regmap_index++;
	}

	# Build device register map
	foreach my $name (sort(keys(%$devregs))) {
		my %h = %$devregs;
		my $devreg = $h{$name};
		my @dev_regbm = @{$devreg->regbm};
		push(@regmap_indices, $regmap_index);
		foreach my $regbm (@dev_regbm) {
			my $addr = hex($regbm->io_addr);
			my $mask =  ((1 << ($regbm->io_msb+1)) - 1)
				 & ~((1 << $regbm->io_lsb) - 1);
			my $shift = $regbm->io_msb - $regbm->dev_msb;

			$regmap_struct = $regmap_struct .
				sprintf ("{ $DEVNAME\_%s, 0x%x, %d, 0x%02x },\n\t",
					$name, $addr, $shift, $mask);
			$regmap_index++;
		}
	}
	$regmap_struct = $regmap_struct . "{ -1, 0, 0, 0 }";

	# Build register map index

	my $index_array = $self->print_array_c("\t", 60, @regmap_indices);

	# Print source code

	printf <<END_OF_TEXT
/* $DEVNAME register definitions and hardware map.
 * This file is auto generated.
 */
#ifndef _$DEVNAME\_REGS_H_
#define _$DEVNAME\_REGS_H_

/* Some definitions
 *
 * Device register:
 *
 * A named register with a single function in the scanner.
 * It can occupy one or many IO registers.
 * Example: SCANLEN , which is a 12-bit register in IO registers 0x95 and 0x96
 *
 * IO register:
 *
 * A register identified by its address in the hardware IO space.
 * It can contain one or several device registers.
 * Example: 0x5a, holds the device registers ADCLKINV, RLCSEL,
 * CDSREF[1:0] and RLC[3:0].
 *
 * Register map:
 *
 * A device register to IO register mapping (and possibly vice versa).
 */

struct regset_ent
{
	int reg;		/* Device register - enum $devname\_reg */
	unsigned int val;
};

/* Device register to IO register mapping */
struct regmap_ent
{
	int devreg;	/* Device register - enum $devname\_reg */
	int ioreg;	/* IO register address */
	char shift;	/* IO register shift
			 * When storing devreg in ioreg:
			 * > 0: shift left, < 0: shift right
			 * When fetching devreg from ioreg: vice versa. */
	int mask;	/* IO register bitmask */
};

/* IO register */
struct ioregister
{
	int ioreg;	/* IO register address */
	int inuse;	/* Defined/declared bits bitmask */
	int dirty;	/* Dirty bits bitmask */
	int val;	/* Current value in the I/O register */
};

/* Enumeration of $DEVNAME device and IO registers. */

enum $devname\_reg
{
	/* Min/max $DEVNAME IO register addresses */

	$DEVNAME\_MIN_IOREG = 0,
	$DEVNAME\_MAX_IOREG = $max_ioaddr,

	/* Device registers */

	$enum_array

	$DEVNAME\_MAX_DEVREG /* not a register */
};

#ifdef $DEVNAME\_PRIVATE /* For private use in $devname\_low.c */

/* $DEVNAME device register name strings. */
const char *$devname\_devreg_names[] = {
	$names_array
};

/* $DEVNAME device register --> IO register map.
 * All IO registers are mapped onto themselves (1:1 mapping)
 * followed by the device to IO register map.
 */
const struct regmap_ent $devname\_regmap[] = {
	$regmap_struct
};

/* Start locations in $devname\_regmap[] of the $devname\_reg enumerations. */
const int $devname\_regmap_index[] = {
	$index_array
};

#endif /* $DEVNAME\_PRIVATE */

#endif /* _$DEVNAME\_REGS_H_ */
END_OF_TEXT
;
}

sub Regmapper::set_ioreg_val
{
	my ($self, $addr, $val) = @_;
	my $ioregs = $self->{_ioregs};

	my $a = $addr;

	$addr =~ tr/A-F/a-f/;
	$val = hex($val);

	if (!exists ($ioregs->{$addr})) {
		#warn "Undefined I/O register: $addr ($a).\n";
		return 0;
	}

	$ioregs->{$addr}->value($val);
	return 1;
}

# Get an array of the names of the device registers stored
# in the given I/O register address.
sub Regmapper::get_devregs_at_ioreg
{
	my ($self, $addr) = @_;

	my $ioregs = $self->{_ioregs};

	my $a = $addr;

	$addr =~ tr/A-F/a-f/;

	if (!exists ($ioregs->{$addr})) {
		#warn "Undefined I/O register: $addr ($a).\n";
		return ();
	}

	return @{$ioregs->{$addr}->devregs};
}

# Get current stored value of named device register.
# Returns -1 if not found.
sub Regmapper::get_devreg_val
{
	my ($self, $name) = @_;
	my $devregs = $self->{_devregs};
	my $ioregs = $self->{_ioregs};

	if (!exists ($devregs->{$name})) {
		return -1;
	}

	# Assemble a device register value from one or more IO registers.

	my $devreg = $devregs->{$name};
	my @dev_regbm = @{$devreg->regbm};
	my $val = 0;

	foreach my $regbm (@dev_regbm) {
		my $dlsb = $regbm->dev_lsb;
		my $dmsb = $regbm->dev_msb;
		my $addr = $regbm->io_addr;
		my $iolsb = $regbm->io_lsb;
		my $iomsb = $regbm->io_msb;

		my $shift = $iolsb - $dlsb;
		my $mask = ((1 << ($iomsb + 1)) - 1) - ((1 << $iolsb) - 1);

		if (!exists ($ioregs->{$addr})) {
			warn "Warning: I/O register $addr referred to by " .
				"device register $name is missing.\n";
			return 0;
		}

		my $ioreg = $ioregs->{$addr};
		my $v = $ioreg->value & $mask;
		$v = (($shift > 0) ? ($v >> $shift) : ($v << -$shift));
		$val |= $v;
	}

	return $val;
}
