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

#my $x = new Regmapper("gl841_regmap.txt");
#print $x->dump_devregs();
