# -*- coding: utf-8 -*-
# Copyright (C) by the Spot authors, see the AUTHORS file for details.
#
# This file is part of Spot, a model checking library.
#
# Spot is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# Spot is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
# or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
# License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.

AC_PREREQ([2.69])
AC_INIT([spot], [2.12], [spot@lrde.epita.fr])
AC_CONFIG_AUX_DIR([tools])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 gnu tar-ustar color-tests parallel-tests])
AC_CONFIG_HEADERS([config.h])dnl Private config, not to be used in .hh files.
AX_PREFIX_CONFIG_H([spot/misc/_config.h])dnl Public config, for .hh files.

# If the user didn't supply a CFLAGS value,
# set an empty one to prevent autoconf to stick -O2 -g here.
test -z "$CFLAGS" && CFLAGS=
test -z "$CXXFLAGS" && CXXFLAGS=

adl_ENABLE_DEVEL

AC_PROG_CC
gl_EARLY

AM_PROG_CC_C_O
AC_PROG_CXX

AM_PROG_LEX
adl_CHECK_BISON

# Decrease verbosity when passing argument V=0
AM_SILENT_RULES([no])

# Option to activate C++20
AC_ARG_ENABLE([c++20],
              [AS_HELP_STRING([--enable-c++20],
                              [Compile in C++20 mode.])],
              [enable_20=$enableval], [enable_20=no])

AC_ARG_ENABLE([pthread],
              [AS_HELP_STRING([--enable-pthread],
                              [Allow libspot to use POSIX threads.])],
              [enable_pthread=$enableval], [enable_pthread=no])
if test "$enable_pthread" = yes; then
  AC_DEFINE([ENABLE_PTHREAD], [1], [Whether Spot is compiled with -pthread.])
  AC_SUBST([LIBSPOT_PTHREAD], [-pthread])
fi

AC_ARG_ENABLE([doxygen],
              [AS_HELP_STRING([--enable-doxygen],
  [enable generation of Doxygen documentation (requires Doxygen)])],
  [enable_doxygen=$enableval], [enable_doxygen=no])
AM_CONDITIONAL([ENABLE_DOXYGEN], [test "x${enable_doxygen:-no}" = xyes])

# Option to indicate the maximal number of acceptance marks
AC_COMPUTE_INT([default_max_accsets], [8*sizeof(unsigned)])
AC_ARG_ENABLE([max-accsets],
              [AS_HELP_STRING([--enable-max-accsets=N],
                              [Support up to N acceptance sets])],
              [enable_max_accsets=$enableval],
              [enable_max_accsets=$default_max_accsets])
if test 0 -eq `expr $enable_max_accsets % $default_max_accsets`
then
  AC_DEFINE_UNQUOTED([MAX_ACCSETS], [$enable_max_accsets],
    [The maximal number of acceptance sets supported (also known as acceptance marks)])
  AC_SUBST([MAX_ACCSETS], [$enable_max_accsets])
else
  AC_MSG_ERROR([The argument of --enable-max-accsets must be a multiple of $default_max_accsets])
fi

# Activate C11 for gnulib tests
AX_CHECK_COMPILE_FLAG([-std=c11], [CFLAGS="$CFLAGS -std=c11"])

gl_INIT

# Use -Werror since using -fvisibility under MinGW is only a warning.
# (The option is ignored anyway since this does not make sense under windows).
AX_CHECK_COMPILE_FLAG([-Werror -fvisibility=hidden],
  [CFLAGS="$CFLAGS -fvisibility=hidden"])
AC_LANG(C++)
AX_CHECK_COMPILE_FLAG([-Werror -fvisibility=hidden],
  [CXXFLAGS="$CXXFLAGS -fvisibility=hidden"
  AX_CHECK_COMPILE_FLAG([-fvisibility-inlines-hidden],
    [CXXFLAGS="$CXXFLAGS -fvisibility-inlines-hidden"])])
CXXFLAGS="$CXXFLAGS -DSPOT_BUILD"

# Turn on C++17 support
m4_define([_AX_CXX_COMPILE_STDCXX_17_testbody],
[AC_LANG_SOURCE([#include <memory>
#include <string>
#include <chrono>  // used to fail in C++11 with some installation of clang
#include <map>

    template <typename T>
    struct check
    {
      static_assert(sizeof(int) <= sizeof(T), "not big enough");
    };

    typedef check<check<bool>> right_angle_brackets;

    auto f = std::make_unique<std::string>("uniq_ptr");

    int a;
    decltype(a) b;

    typedef check<int> check_type;
    check_type c;
    check_type&& cr = static_cast<check_type&&>(c);

    auto d = a;

    int aa[[2]] = {1,2};
    auto [[x,y]] = aa;
])])

if test x"${enable_20}" = xyes; then
  for f in -std=c++20 '-std=c++20 -stdlib=libc++' -std=c++2a
  do
    AX_CHECK_COMPILE_FLAG([$f], [CXXFLAGS="$CXXFLAGS $f" stdpass=true], [], [],
                          [_AX_CXX_COMPILE_STDCXX_17_testbody])
    ${stdpass-false} && break
  done
  if ! "${stdpass-false}"; then
    AC_MSG_ERROR([unable to turn on C++20 mode with this compiler])
  fi
else
  for f in -std=c++17 '-std=c++17 -stdlib=libc++' -std=c++1z
  do
    AX_CHECK_COMPILE_FLAG([$f], [CXXFLAGS="$CXXFLAGS $f" stdpass=true], [], [],
                          [_AX_CXX_COMPILE_STDCXX_17_testbody])
    ${stdpass-false} && break
  done
  if ! "${stdpass-false}"; then
    AC_MSG_ERROR([unable to turn on C++17 mode with this compiler])
  fi
fi

dnl Check if -latomic is required for <std::atomic>
CHECK_ATOMIC

AX_CHECK_BUDDY

AC_CHECK_FUNCS([times kill alarm sigaction sched_getcpu])

oLIBS=$LIBS
LIBS="$LIBS -pthread"
AC_CHECK_FUNCS([pthread_setaffinity_np])
LIBS=$oLIBS

LT_CONFIG_LTDL_DIR([ltdl])
LT_INIT([win32-dll])
LTDL_INIT([subproject convenience])

AX_BSYMBOLIC

AC_ARG_ENABLE([python],
              [AS_HELP_STRING([--disable-python],
                              [do not compile Python bindings])],
              [], [enable_python=yes])

case $enable_shared:$enable_python in
 no:no);;
 no:*)
    enable_python=no
    AC_MSG_NOTICE([Not building Python bindings because of --disable-shared.])
    ;;
esac

AM_CONDITIONAL([USE_PYTHON], [test "x${enable_python:-yes}" = xyes])

if test "x${enable_python:-yes}" = xyes; then
  AC_MSG_NOTICE([You may configure with --disable-python ]dnl
[if you do not need Python bindings.])
  adl_CHECK_PYTHON

  AC_ARG_WITH([pythondir],
  [AS_HELP_STRING([--with-pythondir], [override the computed pythondir])],
  [pythondir=$withval pyexecdir=$withval], [])
fi



adl_ENABLE_DEBUG
ad_GCC_OPTIM
adl_NDEBUG
adl_ENABLE_GLIBCXX_DEBUG

spot_INTEL
AC_ARG_ENABLE([warnings],
              [AS_HELP_STRING([--enable-warnings],
                              [enable a lot of compiler warnings])])
if test x$enable_warnings = xyes; then
  CF_GXX_WARNINGS
fi

AM_CONDITIONAL([NEVER], [false])
AC_CHECK_PROG([DOT], [dot], [dot])
AC_CHECK_PROG([LTL2BA], [ltl2ba], [ltl2ba])
AC_CHECK_PROG([LTL3BA], [ltl3ba], [ltl3ba])
AC_CHECK_PROG([PERL], [perl], [perl])
AC_CHECK_PROG([SPIN], [spin], [spin])
AC_CHECK_PROG([LBTT], [lbtt], [lbtt])
AM_MISSING_PROG([EMACS], [emacs])
AC_CHECK_PROGS([IPYTHON], [ipython3 ipython], [ipython])
AC_CHECK_PROGS([JUPYTER], [jupyter], [jupyter])
AC_CHECK_PROG([LBTT_TRANSLATE], [lbtt-translate], [lbtt-translate])
AX_CHECK_VALGRIND
# Debian used to reserve the name 'swig' for swig-2.0.  So prefer
# swig4.0 (available in Debian bullseye) to swig3.0 (available in Debian buster)
# ti swig.
AC_CHECK_PROGS([SWIG], [swig4.0 swig3.0 swig], [swig])

AC_SUBST([CROSS_COMPILING], [$cross_compiling])

AC_SUBST([GITPATCH], [$(if (git rev-parse) >/dev/null 2>&1; then
 echo ".$(git rev-list $(git rev-list --tags='spot-*' --no-walk \
 --max-count=1)..HEAD --count)"
fi)])

AC_CONFIG_FILES([
  Makefile
  bin/Makefile
  bin/man/Makefile
  bench/Makefile
  bench/dtgbasat/Makefile
  bench/emptchk/Makefile
  bench/emptchk/defs
  bench/ltlcounter/Makefile
  bench/ltlclasses/Makefile
  bench/ltl2tgba/Makefile
  bench/ltl2tgba/defs
  bench/spin13/Makefile
  bench/wdba/Makefile
  bench/stutter/Makefile
  doc/Doxyfile
  doc/Makefile
  doc/tl/Makefile
  doc/org/.dir-locals.el
  doc/org/init.el
  elisp/Makefile
  lib/Makefile
  picosat/Makefile
  spot/graph/Makefile
  spot/kripke/Makefile
  spot/ltsmin/Makefile
  spot/Makefile
  spot/misc/Makefile
  spot/mc/Makefile
  spot/parseaut/Makefile
  spot/parsetl/Makefile
  spot/priv/Makefile
  spot/taalgos/Makefile
  spot/ta/Makefile
  spot/tl/Makefile
  spot/twaalgos/gtec/Makefile
  spot/twaalgos/Makefile
  spot/twacube_algos/Makefile
  spot/twa/Makefile
  spot/gen/Makefile
  spot/twacube/Makefile
  python/Makefile
  tests/core/defs
  tests/ltsmin/defs:tests/core/defs.in
  tests/Makefile
  tools/x-to-1
])
AC_CONFIG_FILES([doc/org/g++wrap], [chmod +x doc/org/g++wrap])
AC_CONFIG_FILES([tests/run], [chmod +x tests/run])
AC_OUTPUT

case $VERSION:$enable_devel in
  *[[abcdefghijklmnopqrstuvwxyz]]:yes)
    echo
    echo '==================================================================='
    echo ' This is a development version of Spot: Assertions and debuging    '
    echo ' code are enabled by default.  If you find this too slow or        '
    echo ' plan to do some benchmarking, run configure with --disable-devel. '
    echo '==================================================================='
  ;;
esac

case $enable_python in
    yes)
        pd=$pythondir
        eval pd=$pd
        eval pd=$pd
        $PYTHON -c "
import sys
if '$pd' in sys.path:
   exit()
else:
   print('\nWARNING: Python bindings will be installed in $pd')
   print('         however this path is not searched by default by $PYTHON.')
   print('\n$PYTHON\'s sys.path contains the following paths:\n',
         '\n'.join(sys.path))
   print('\nUse --with-pythondir=... if you wish '
         'to change this installation path.')
"
        ;;
esac
