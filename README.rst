============
vmod_abtest
============

------------------------
Varnish A/B Tests Module
------------------------

:Author: Xavier Basty
:Date: 2012-09-24
:Version: 0.1
:Manual section: 3

DESCRIPTION
===========

Varnish Module (vmod) to get weighted random values for A/B testing,
with a dynamic configuration.

SYNOPSIS
========

::

        import abtest;

        abtest.set_rule(<key>, <rule>)
        abtest.rem_rule(<key>)
        abtest.clear()

        abtest.load_config(<path>)
        abtest.save_config(<path>)

        abtest.get_rand(<key>)
        abtest.get_rules()


FUNCTIONS
=========

set_rule
--------

Prototype
        ::

                set_rule(STRING key, STRING rule)
Return value
        VOID
Description
        Set the rule associated with the specified key.

        The format for the rule declaration is:

        ``<option1>:<weight1>;<option2>:<weight2>;``

        The weights are relative to each others, so sum of all the weight is
        considered equals to 100%.

Example
        ``abtest.set_rule("base_rule", "a:60;b:40");``

rem_rule
--------

Prototype
        ::

                rem_rule(STRING)
Return value
        VOID
Description
        Remove the rule associated with the specified key.

        If the rule doesn't exist in the configuration, the function does
        nothing and returns without errors.
Example
        ``abtest.rem_rule("base_rule");``

clear
-----

Prototype
        ::

                clear()
Return value
        VOID
Description
        Remove all the rules from the current configuration.
Example
        ``abtest.clear();``

load_config
-----------

Prototype
        ::

                load_config(STRING path)
Return value
        INT
Description
        On successful completion, the load_config() function returns 0.
        Otherwise, it returns an integer value indicating the error that occured
        and the content of the current configuration remains unchanged.

        If an active configuration is already present, it is overwritten when
        the file is loaded successfully.

        See `configuration file`_ for information on the file format.
Example
        ::

                if (abtest.load_config("abtest.cfg") != 0) {
                        std.log("Could not load the configuration file!");
                }

save_config
-----------

Prototype
        ::

                save_config(STRING path)
Return value
        INT
Description
        On successful completion, the save_config() function returns 0.
        Otherwise, it returns an integer value indicating the error that occured.

        ..      note:: If the current configuration is uninitialized, the function returns
                immediatly and does **not** overwrite the configuration file.

        See `configuration file`_ for information on the file format.
Example
        ::

                if (abtest.save_config("abtest.cfg") != 0) {
                        std.log("Could not save the configuration file!");
                }

get_rand
--------

Prototype
        ::

                get_rand(STRING key)
Return value
        STRING
Description
        Returns one of the options in the specified rule,
        the option is chosen with the random weights declared in the rule.

        If the rule is not present in the current configuration, the function
        returns NULL.
Example
        ``set resp.http.Set-Cookie = "abtesting=" + abtest.get_rand("base");``

get_rules
---------

Prototype
        ::

                get_rules()
Return value
        STRING
Description
        Returns a list of all the rules present in the current configuration.

        The format of the rule returned is the same as in the
        `configuration file`_, but with the rule separated with spaces instead
        of new lines.
Example
        ``set resp.http.X-AB-Rules = abtest.get_rules();``


CONFIGURATION FILE
==================

The configuration is saved as an ASCII file with each rule on a separate line in
the following format::

        <rule_name_1>:<option>:<weight>;<option>:<weight>;...
        <rule_name_2>:<option>:<weight>;<option>:<weight>;...

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

ACKNOWLEDGEMENTS
================

The general structure is inspired from the
`example vmod <https://www.varnish-cache.org/vmod/example-vmod-hello-world>`_
and many others.

The weighted random function is originally from
`Sergiy Dzysyak <http://erlycoder.com/105/javascript-weighted-random-value-from-array>`_

HISTORY
=======

Version 0.1: Initial version.


SEE ALSO
========

* varnishd(1)
* vcl(7)
* https://github.com/Destination/libvmod-abtest

COPYRIGHT
=========

This document is licensed under the same license as the
libvmod-abtest project. See LICENSE for details.

* Copyright (c) 2012 Destinationpunktse AB
