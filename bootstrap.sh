#!/bin/sh
set -x
aclocal || exit 1
automake --add-missing -c || exit 1
autoconf || exit 1
