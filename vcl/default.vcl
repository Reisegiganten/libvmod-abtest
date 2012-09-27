/*-
 * Copyright (c) 2006 Verdens Gang AS
 * Copyright (c) 2006-2011 Varnish Software AS
 * All rights reserved.
 *
 * Author: Poul-Henning Kamp <phk@phk.freebsd.dk>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The default VCL code.
 *
 * NB! You do NOT need to copy & paste all of these functions into your
 * own vcl code, if you do not provide a definition of one of these
 * functions, the compiler will automatically fall back to the default
 * code from this file.
 *
 * This code will be prefixed with a backend declaration built from the
 * -b argument.
 */

 import std;
 import abtest;

 C{
    #include <syslog.h>
 }C


 backend default {
    .host = "127.0.0.1";
    .port = "3000";
 }

/* Only permit localhost to manipulate abtest configuration */
acl abconfig {
    "localhost";
}

sub vcl_init {
    if (abtest.load_config("/tmp/abtest.cfg") != 0) {
        C{ syslog(LOG_ALERT, "Unable to load AB config from /tmp/abtest.cfg"); }C
    }
    return (ok);
}

sub vcl_fini {
}

sub vcl_recv {
    if (req.http.AB-Cfg) {
        if (!client.ip ~ abconfig) {
            std.log("AB Config request not allowed from " + client.ip);
            error 405 "Not allowed.";
        } else {
            // curl localhost:8080 -X PUT -H "AB-Cfg:base" -H "AB-Cfg-Val:a:25;b:75;"
            if (req.request == "PUT") {
                std.log("AB Config PUT request: " + req.http.AB-Cfg + "|" + req.http.AB-Cfg-Val);
                abtest.set_rule(req.http.AB-Cfg, req.http.AB-Cfg-Val);
                abtest.save_config("/tmp/abtest.cfg");
            }

            if (req.request == "DELETE") {
                std.log("AB Config DELETE request: " + req.http.AB-Cfg);
            }

            if (req.request == "GET") {
                std.log("AB Config GET request: " + req.http.AB-Cfg);
            }
        }
    }

/*
    if(req.http.Cookie ~ "abtesting") {
    }
*/
}

sub vcl_pipe {
}

sub vcl_pass {
}

sub vcl_hash {
}

sub vcl_hit {
}

sub vcl_miss {
}

sub vcl_fetch {
}

sub vcl_deliver {
}

sub vcl_error {
}
