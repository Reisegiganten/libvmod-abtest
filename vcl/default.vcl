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
                return(synth(200,"Done."));
            }

            // curl localhost:8080 -X DELETE -H "X-AB-Cfg:base"
            if (req.request == "DELETE") {
                std.log("AB Config DELETE request: " + req.http.X-AB-Cfg);
                abtest.rem_rule(req.http.X-AB-Cfg);
                if (abtest.save_config("/tmp/abtest.cfg") != 0) {
                    std.log("ABTest - Error, could not save the configuration");
                }
                return(synth(200,"Done."));
            }
        }
    }

    if (abtest.get_duration(req.url)) {
        # The header only contains the name of the assigned segment, making easier
        # to the backend code to get it
        if(!req.http.Cookie ~ "abtesting") {
            set req.http.X-AB-Test = abtest.get_rand(req.url);
        }
        else {
            set req.http.X-AB-Test = regsub(req.http.Cookie, "(?:^|;\s*)(?:abtesting=(.*?))(?:;|$)", "\1");
        }
    }
    return (hash);
}

/* To be added in case the backend does not set the X-AB-Test and Vary headers */
sub vcl_backend_response {
    # Lets copy in the response the X-AB-Test header and add it the Vary
    if (bereq.http.X-AB-Test) {
        set beresp.http.X-AB-Test = bereq.http.X-AB-Test;
        if (beresp.http.Vary) {
            set beresp.http.Vary = beresp.http.Vary+", X-AB-Test";
        } else {
            set beresp.http.Vary = "X-AB-Test";
        }
    }
}

sub vcl_deliver {
    if (resp.http.X-AB-Test) {
        set resp.http.Set-Cookie = "abtesting=" + req.http.X-AB-Test + "; path=/; expires=" + abtest.get_expire(req.url);
    }

    if (req.http.X-AB-Cfg && client.ip ~ abconfig) {
        if (req.request == "GET") {
            // curl localhost:8080 -X GET -H "X-AB-Cfg:;"
            set resp.http.X-AB-Cfg = abtest.get_rules();
        }
    }
}

