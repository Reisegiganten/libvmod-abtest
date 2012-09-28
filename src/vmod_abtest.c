#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>
#include <regex.h>
#include <math.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

#include "vmod_abtest.h"

#define ALLOC_CFG(cfg)                                                          \
    (cfg) = malloc(sizeof(struct vmod_abtest));                                 \
    cfg_init((cfg));

#define DUP_MATCH(to, from, match)                                              \
    (to) = (char*)calloc((match).rm_eo - (match).rm_so + 2, sizeof(char));      \
    AN((to));                                                                   \
    strlcpy((to), from + (match).rm_so, (match).rm_eo - (match).rm_so + 1);



#define VMOD_ABTEST_MAGIC 0xDD2914D8
#define CONF_REGEX "([[:alnum:]_]+):(([[:alnum:]_]+:[[:digit:]]+;)*)"
#define RULE_REGEX "([[:alnum:]_]+):([[:digit:]]+);"

struct rule {
    char* key;
    unsigned num;
    unsigned* weights;
    double* norm_weights;
    char** options;

    VTAILQ_ENTRY(rule) list;
};

struct vmod_abtest {
    unsigned magic;
    unsigned xid;
    VTAILQ_HEAD(, rule) rules;
};

static pthread_mutex_t cfg_mtx = PTHREAD_MUTEX_INITIALIZER;

static void cfg_init(struct vmod_abtest *cfg) {
    AN(cfg);
    cfg->magic = VMOD_ABTEST_MAGIC;
    VTAILQ_INIT(&cfg->rules);
}

static void cfg_clear(struct vmod_abtest *cfg) {
    CHECK_OBJ_NOTNULL(cfg, VMOD_ABTEST_MAGIC);

    struct rule *r, *r2;
    VTAILQ_FOREACH_SAFE(r, &cfg->rules, list, r2) {
        VTAILQ_REMOVE(&cfg->rules, r, list);
        free(r->key);
        free(r->weights);
        free(r->norm_weights);
        free(r->options);
        free(r);
    }
}

static void cfg_free(struct vmod_abtest *cfg) {
    CHECK_OBJ_NOTNULL(cfg, VMOD_ABTEST_MAGIC);

    cfg_clear(cfg);
    cfg->xid = 0;
    cfg->magic = 0;
    free(cfg);
}

static struct rule* get_rule(struct vmod_abtest *cfg, const char *key) {
    struct rule *r;

    if (!key) {
        return NULL;
    }

    VTAILQ_FOREACH(r, &cfg->rules, list) {
        if (r->key && strcmp(r->key, key) == 0) {
            return r;
        }
    }
    return NULL;
}

static struct rule* alloc_rule(struct vmod_abtest *cfg, const char *key) {
    unsigned l;
    char *p;
    struct rule *rule;

    rule = (struct rule*)calloc(sizeof(struct rule), 1);
    AN(rule);

    l = strlen(key) + 1;
    rule->key = calloc(l, 1);
    AN(rule->key);
    memcpy(rule->key, key, l);

    VTAILQ_INSERT_HEAD(&cfg->rules, rule, list);

    return rule;
}

static struct rule* get_rule_alloc(struct vmod_abtest *cfg, const char *key) {
    struct rule *rule;

    rule = get_rule(cfg, key);
    if (!rule) {
        rule = alloc_rule(cfg, key);
    }

    return rule;
}


int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
    priv->free  = (vmod_priv_free_f *)cfg_free;
    return (0);
}

static void parse_rule(struct rule *rule, const char *source) {
    unsigned n;
    unsigned sum;
    int r;
    const char *s;
    regex_t regex;
    regmatch_t match[3];

    if (regcomp(&regex, RULE_REGEX, REG_EXTENDED)){
        perror("Could not compile rule regex");
        exit(1);
    }

    rule->num = 0;
    s = source;
    while ((r = regexec(&regex, s, 3, match, 0)) == 0) {
        rule->num++;
        s += match[0].rm_eo;
    }

    if (r != REG_NOMATCH) {
        char buf[100];
        regerror(r, &regex, buf, sizeof(buf));
        fprintf(stderr, "Regex match failed: %s\n", buf);
        exit(1);
    }

    rule->options = calloc(rule->num, sizeof(char*));
    rule->weights = calloc(rule->num, sizeof(unsigned));
    rule->norm_weights = calloc(rule->num, sizeof(double));

    s = source;
    n = 0;
    sum = 0;
    while ((r = regexec(&regex, s, 3, match, 0)) == 0 && n < rule->num) {
        DUP_MATCH(rule->options[n], s, match[1]);

        rule->weights[n] = strtoul(s + match[2].rm_so, NULL, 10);

        sum += rule->weights[n];
        rule->norm_weights[n] = sum;


        s += match[0].rm_eo;
        n++;
    }

    if (sum != 0) {
        for (n = 0; n < rule->num; n++) {
            rule->norm_weights[n] /= sum;
        }
    }

    regfree(&regex);
}


/*******************************************************************************
** VMOD Functions
*******************************************************************************/

void __match_proto__() vmod_set_rule(struct sess *sp, struct vmod_priv *priv, const char *key, const char *rule) {
    AN(key);

    if (priv->priv == NULL) {
        ALLOC_CFG(priv->priv);
    }

    struct rule *target = get_rule_alloc(priv->priv, key);
    parse_rule(target, rule);
}

void __match_proto__() vmod_rem_rule(struct sess *sp, struct vmod_priv *priv, const char *key) {
    AN(key);

    if (priv->priv == NULL) {
        return;
    }

    struct rule *rule = get_rule(priv->priv, key);
    if (rule != NULL) {
        VTAILQ_REMOVE(&((struct vmod_abtest*)priv->priv)->rules, rule, list);
    }
}

void __match_proto__() vmod_clear(struct sess *sp, struct vmod_priv *priv) {
    if (priv->priv == NULL) { return; }
    cfg_clear(priv->priv);
}

int __match_proto__() vmod_load_config(struct sess *sp, struct vmod_priv *priv, const char *source) {
    AN(source);

    AZ(pthread_mutex_lock(&cfg_mtx));

    if (priv->priv == NULL) {
        ALLOC_CFG(priv->priv);
    } else {
        cfg_clear(priv->priv);
    }

    struct vmod_abtest *cfg = priv->priv;

    FILE *f;
    long file_len;
    char *buf;
    char *s;
    int r;
    regex_t regex;
    regmatch_t match[3];
    struct rule *rule;

    if (regcomp(&regex, CONF_REGEX, REG_EXTENDED)){
        perror("Could not compile line regex");
        AZ(pthread_mutex_unlock(&cfg_mtx));
        return -1;
    }

    f = fopen(source, "r");
    if (f == NULL) {
        perror("ABTest: Could not open configuration file.");
        AZ(pthread_mutex_unlock(&cfg_mtx));
        return -1;
    }

    fseek(f, 0L, SEEK_END);
    file_len = ftell(f);
    rewind(f);

    buf = calloc(file_len + 1, sizeof(char));
    AN(buf);
    fread(buf, file_len, 1, f);

    AZ(fclose(f));

    s = buf;
    while ((r = regexec(&regex, s, 3, match, 0)) == 0) {
        *(s + match[0].rm_eo) = 0;

        rule = (struct rule*)calloc(sizeof(struct rule), 1);
        AN(rule);

        VTAILQ_INSERT_HEAD(&cfg->rules, rule, list);

        DUP_MATCH(rule->key, s, match[1]);
        parse_rule(rule, s + match[2].rm_so);

        s += match[0].rm_eo + 1;
    }

    regfree(&regex);
    free (buf);

    if (r != REG_NOMATCH) {
        char buf[100];
        regerror(r, &regex, buf, sizeof(buf));
        fprintf(stderr, "Regex match failed: %s\n", buf);

        AZ(pthread_mutex_unlock(&cfg_mtx));
        return -1;
    }

    AZ(pthread_mutex_unlock(&cfg_mtx));
    return 0;
}

void __match_proto__() vmod_save_config(struct sess *sp, struct vmod_priv *priv, const char *target) {
    AN(target);

    AZ(pthread_mutex_lock(&cfg_mtx));

    if (priv->priv == NULL) {
        return;
    }

    struct vmod_abtest* cfg = priv->priv;
    struct rule *r;
    FILE *f;

    f = fopen(target, "w");
    AN(f);

    VTAILQ_FOREACH(r, &cfg->rules, list) {
        fprintf(f, "%s:", r->key);
        for (int i = 0; i < r->num; i++) {
            fprintf(f, "%s:%d;", r->options[i], r->weights[i]);
        }
        fprintf(f, "\n");
    }

    AZ(fclose(f));
    AZ(pthread_mutex_unlock(&cfg_mtx));
}

/*
* Weighted random algorithm from:
* http://erlycoder.com/105/javascript-weighted-random-value-from-array
*/
const char* __match_proto__() vmod_get_rand(struct sess *sp, struct vmod_priv *priv, const char *key) {
    struct rule *rule;
    double n;       // needle
    unsigned p;     // probe
    unsigned l, h;  // low & high

    if (priv->priv == NULL) {
        return NULL;
    }

    rule = get_rule(priv->priv, key);
    if (rule == NULL) {
        return NULL;
    }

    n = drand48();
    l = 0;
    h = rule->num - 1;

    while (l < h) {
        p = (unsigned)ceil((h + l) / 2);
        if (rule->norm_weights[p] < n) {
            l = p + 1;
        } else if (rule->norm_weights[p] > n) {
            h = p - 1;
        } else {
            return rule->options[p];
        }
    }

    if (l != h) {
        p = rule->norm_weights[l] >= n ? l : p;
    } else {
        p = rule->norm_weights[l] >= n ? l : l+1;
    }
    return rule->options[p];
}

const char* __match_proto__() vmod_get_rules(struct sess *sp, struct vmod_priv *priv) {
    if (priv->priv == NULL) {
        return NULL;
    }

    struct rule *r;
    size_t len = 0;
    size_t l;
    int i;
    char *rules;
    char *s;

    VTAILQ_FOREACH(r, &((struct vmod_abtest*)priv->priv)->rules, list) {
        len += strlen(r->key) + 2;
        for (i = 0; i < r->num; i++) {
            len += snprintf(NULL, 0, "%s:%d;", r->options[i], r->weights[i]);
        }
    }

    AN(rules = (char*)WS_Alloc(sp->ws, len + 1));

    s = rules;
    VTAILQ_FOREACH(r, &((struct vmod_abtest*)priv->priv)->rules, list) {
        l = strlen(r->key);
        memcpy(s, r->key, l);
        s += l;
        *s++ = ':';
        len -= l + 1;

        for (i = 0; i < r->num; i++) {
            l = snprintf(s, len, "%s:%d;", r->options[i], r->weights[i]);
            s += l;
            len -= l;
        }
        *s++ = ' ';
        len--;
    }
    *s = 0;

    return rules;
}
