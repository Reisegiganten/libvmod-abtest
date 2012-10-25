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
    abtest.set_rule(".*", "a:1;b:1;86400;"); // Set default rule with a duration of 24H

    return (ok);
}

sub vcl_fini {
}

sub vcl_recv {
    if (req.http.X-AB-Cfg) {
        if (!client.ip ~ abconfig) {
            error 405 "Not allowed.";
        } else {
            // curl localhost:8080 -X PUT -H "X-AB-Cfg:base" -H "X-AB-Cfg-Val:a:25;b:75;"
            if (req.request == "PUT") {
                std.log("AB Config PUT request: " + req.http.X-AB-Cfg + "|" + req.http.X-AB-Cfg-Val);
                abtest.set_rule(req.http.X-AB-Cfg, req.http.X-AB-Cfg-Val);
                if (abtest.save_config("/tmp/abtest.cfg") != 0) {
                    std.log("ABTest - Error, could not save the configuration");
                }
            }

            // curl localhost:8080 -X DELETE -H "X-AB-Cfg:base"
            if (req.request == "DELETE") {
                std.log("AB Config DELETE request: " + req.http.X-AB-Cfg);
                abtest.rem_rule(req.http.X-AB-Cfg);
            }
        }
    }

    if(!req.http.Cookie ~ "abtesting") {
        std.log("No AB cookie, setting it.");
        set req.http.Cookie = "abtesting=" + abtest.get_rand(req.url) + "; path=/; expires=" + abtest.get_expire(req.url);
        set req.http.X-Set-AB-Cookie = req.http.Cookie;
    } else {
        std.log("AB cookie found.");
    }

    return (lookup);
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
    if (req.http.X-Set-AB-Cookie) {
        std.log("Setting cookie to " + req.http.X-Set-AB-Cookie);
        set resp.http.Set-Cookie = req.http.X-Set-AB-Cookie;
    }

    if (req.http.X-AB-Cfg && client.ip ~ abconfig) {
        if (req.request == "GET") {
            // curl localhost:8080 -X GET -H "X-AB-Cfg:;"
            set resp.http.X-AB-Cfg = abtest.get_rules();
        }
    }
}

sub vcl_error {
}
