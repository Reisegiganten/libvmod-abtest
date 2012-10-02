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
    std.log(abtest.get_rules());

    if (req.http.X-AB-Cfg) {
        if (!client.ip ~ abconfig) {
            std.log("AB Config request not allowed from " + client.ip);
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

            // curl localhost:8080 -X GET -H "X-AB-Cfg;"
            if (req.request == "GET") {
                std.log("AB Config GET request: " + req.http.X-AB-Cfg);
                std.log("CFG -> " + abtest.get_rules());
                if (req.http.X-AB-Cfg != "") {
                    std.log("duration for '" + req.http.X-AB-Cfg + "': " + abtest.get_duration(req.http.X-AB-Cfg));
                }
            }
        }
    }

    std.log("AB Cookie for '" + req.url + "': " + abtest.get_rand(req.url));

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
