## -*- coding: utf-8 -*-
## Copyright (C) by the Spot authors, see the AUTHORS file for details.
##
## This file is part of Spot, a model checking library.
##
## Spot is free software; you can redistribute it and/or modify it
## under the terms of the GNU General Public License as published by
## the Free Software Foundation; either version 3 of the License, or
## (at your option) any later version.
##
## Spot is distributed in the hope that it will be useful, but WITHOUT
## ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
## or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
## License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.

AM_CPPFLAGS = -I$(top_builddir) -I$(top_srcdir) $(BUDDY_CPPFLAGS) \
              -I$(top_builddir)/lib -I$(top_srcdir)/lib
AM_CXXFLAGS = $(WARNING_CXXFLAGS)

miscdir = $(pkgincludedir)/misc

nodist_misc_HEADERS = _config.h
DISTCLEANFILES = _config.h

misc_HEADERS = \
  bareword.hh \
  bddlt.hh \
  bitset.hh \
  bitvect.hh \
  casts.hh \
  common.hh \
  clz.hh \
  escape.hh \
  fixpool.hh \
  formater.hh \
  hash.hh \
  hashfunc.hh \
  intvcomp.hh \
  intvcmp2.hh \
  location.hh \
  ltstr.hh \
  minato.hh \
  memusage.hh \
  mspool.hh \
  optionmap.hh \
  position.hh \
  random.hh \
  satsolver.hh \
  timer.hh \
  tmpfile.hh \
  trival.hh \
  version.hh

noinst_LTLIBRARIES = libmisc.la
libmisc_la_SOURCES = \
  bareword.cc \
  bitset.cc \
  bitvect.cc \
  escape.cc \
  formater.cc \
  intvcomp.cc \
  intvcmp2.cc \
  memusage.cc \
  minato.cc \
  optionmap.cc \
  random.cc \
  satsolver.cc \
  timer.cc \
  tmpfile.cc \
  version.cc
