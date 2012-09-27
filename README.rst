============
vmod_abtest
============

------------------------
Varnish A/B Tests Module
------------------------

:Author: Xavier Basty
:Date: 2012-09-24
:Version: 1.0
:Manual section: 3

SYNOPSIS
========

import abtest;

DESCRIPTION
===========


FUNCTIONS
=========

set_rule
-----

Prototype
        ::

                set_rule(STRING key, STRING rule)

clear
-----

Prototype
        ::

                clear()

load_config
-----

Prototype
        ::

                load_config(STRING path)

save_config
-----

Prototype
        ::

                save_config(STRING path)

get_rand(STRING)
-----

Prototype
        ::

                get_rand(STRING key)
Return value
        STRING
Description
        Returns one of the options in the specified rule.
Example
        ::

                set resp.http.Set-Cookie = "abtesting=" + abtest.get_rand("base");


INSTALLATION
============

The source tree is based on autotools to configure the building, and
does also have the necessary bits in place to do functional unit tests
using the varnishtest tool.

Usage::

 ./configure VARNISHSRC=DIR [VMODDIR=DIR]

`VARNISHSRC` is the directory of the Varnish source tree for which to
compile your vmod. Both the `VARNISHSRC` and `VARNISHSRC/include`
will be added to the include search paths for your module.

Optionally you can also set the vmod install directory by adding
`VMODDIR=DIR` (defaults to the pkg-config discovered directory from your
Varnish installation).

Make targets:

* make - builds the vmod
* make install - installs your vmod in `VMODDIR`
* make check - runs the unit tests in ``src/tests/*.vtc``

In your VCL you could then use this vmod along the following lines::

        import abtest;

        sub vcl_deliver {
                set resp.http.Set-Cookie = "abtesting=" + abtest.get_rand("base");
        }

* Copyright (c) 2012 Destinationpunktse AB
