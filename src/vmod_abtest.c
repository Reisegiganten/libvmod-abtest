#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>
#include <regex.h>
#include <math.h>
#include <stdbool.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

#include "vmod_abtest.h"

#define ALLOC_CFG(cfg)                                                          \
    (cfg) = calloc(1, sizeof(struct vmod_abtest));                              \
    cfg_init((cfg));

#define DUP_MATCH(to, from, match)                                              \
    (to) = (char*)calloc((match).rm_eo - (match).rm_so + 2, sizeof(char));      \
    AN((to));                                                                   \
    strncpy((to), from + (match).rm_so, (match).rm_eo - (match).rm_so);

#define FREE_FIELD(field)                                                       \
    if ((field) != NULL) {                                                      \
        free ((field));                                                         \
        (field) = NULL;                                                         \
    }

#define LOG_ERR(sess, ...)                                                      \
    if ((sess) != NULL) {                                                       \
        WSP((sess), SLT_VCL_error, __VA_ARGS__);                                \
    } else {                                                                    \
        fprintf(stderr, __VA_ARGS__);                                           \
        fputs("\n", stderr);                                                    \
    }

#define VMOD_ABTEST_MAGIC 0xDD2914D8
#define CONF_REGEX "([^:\r\n]+):(([[:alnum:]_]+:[[:digit:]]+;)+(([[:digit:]]*);)?)"
#define RULE_REGEX "([[:alnum:]_]+):([[:digit:]]+);"
#define TIME_REGEX ";([[:digit:]]+);"

struct rule {
    char* key;
    unsigned num;
    double duration;
    unsigned* weights;
    double* norm_weights;
    char** options;

    regex_t* key_regex;

    VTAILQ_ENTRY(rule) list;
};

struct vmod_abtest {
    unsigned magic;
    unsigned xid;
    VTAILQ_HEAD(, rule) rules;

    bool use_text_key;
    struct rule* (*get_rule)(struct vmod_abtest *cfg, const char *key);
};

static pthread_mutex_t cfg_mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_rwlock_t cfg_rwl = PTHREAD_RWLOCK_INITIALIZER;

static void cfg_init(struct vmod_abtest *cfg) {
    AN(cfg);
    cfg->magic = VMOD_ABTEST_MAGIC;
    VTAILQ_INIT(&cfg->rules);
    cfg->get_rule = &get_regex_rule;
}

static void cfg_clear(struct vmod_abtest *cfg) {
    CHECK_OBJ_NOTNULL(cfg, VMOD_ABTEST_MAGIC);

    struct rule *r, *r2;
    VTAILQ_FOREACH_SAFE(r, &cfg->rules, list, r2) {
        VTAILQ_REMOVE(&cfg->rules, r, list);
        delete_rule(r);
    }
}

static void cfg_free(struct vmod_abtest *cfg) {
    CHECK_OBJ_NOTNULL(cfg, VMOD_ABTEST_MAGIC);

    cfg_clear(cfg);
    cfg->xid = 0;
    cfg->magic = 0;
    free(cfg);
}

static void delete_rule(struct rule* r) {
        FREE_FIELD(r->key);
        FREE_FIELD(r->weights);
        FREE_FIELD(r->norm_weights);
        FREE_FIELD(r->options);

        if (r->key_regex != NULL) {
            regfree(r->key_regex);
            free(r->key_regex);
            r->key_regex = NULL;
        }

        free(r);
}

static struct rule* get_regex_rule(struct vmod_abtest *cfg, const char *key) {
    struct rule *r;

    if (!key) {
        return NULL;
    }

    VTAILQ_FOREACH(r, &cfg->rules, list) {
        if (regexec(r->key_regex, key, 0, NULL, 0) == 0) {
            return r;
        }
    }
    return NULL;
}

static struct rule* get_text_rule(struct vmod_abtest *cfg, const char *key) {
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


static void alloc_key_regex(struct sess *sp, struct vmod_abtest *cfg, struct rule* rule, const char *key) {
    if (cfg->use_text_key) {
        rule->key_regex = NULL;
        return;
    }

    rule->key_regex = calloc(1, sizeof(regex_t));

    int r;
    if (r = regcomp(rule->key_regex, key, REG_EXTENDED | REG_NOSUB)) {
        size_t err_len = regerror(r, rule->key_regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, rule->key_regex, err_buf, err_len);
        LOG_ERR(sp, "Could not compile key regex: %s", err_buf);

        free(rule->key_regex);
        rule->key_regex = NULL;
    }
}

static struct rule* alloc_rule(struct sess *sp, struct vmod_abtest *cfg, const char *key) {
    unsigned l;
    char *p;
    struct rule *rule;

    rule = (struct rule*)calloc(sizeof(struct rule), 1);
    AN(rule);

    l = strlen(key) + 1;
    rule->key = calloc(l, 1);
    AN(rule->key);
    memcpy(rule->key, key, l);

    alloc_key_regex(sp, cfg, rule, key);

    VTAILQ_INSERT_TAIL(&cfg->rules, rule, list);

    return rule;
}

static void parse_rule(struct sess *sp, struct rule *rule, const char *source) {
    unsigned n;
    unsigned sum;
    int r;
    const char *s;
    regex_t rule_regex;
    regex_t time_regex;
    regmatch_t match[3];

    if (r = regcomp(&rule_regex, RULE_REGEX, REG_EXTENDED)){
        size_t err_len = regerror(r, &rule_regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, &rule_regex, err_buf, err_len);
        LOG_ERR(sp, "Could not compile rule regex: %s", err_buf);

        return;
    }

    if (r = regcomp(&time_regex, TIME_REGEX, REG_EXTENDED)){
        regfree(&rule_regex);
        size_t err_len = regerror(r, &time_regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, &time_regex, err_buf, err_len);
        LOG_ERR(sp, "Could not compile time regex: %s", err_buf);

        return;
    }

    s = source;
    if (r = regexec(&time_regex, s, 2, match, 0) == 0) {
        rule->duration = strtod(s + match[1].rm_so, NULL);
    } else {
        rule->duration = 0.;
    }

    rule->num = 0;
    s = source;
    while ((r = regexec(&rule_regex, s, 3, match, 0)) == 0) {
        rule->num++;
        s += match[0].rm_eo;
    }

    if (r != REG_NOMATCH) {
        regfree(&rule_regex);
        regfree(&time_regex);

        size_t err_len = regerror(r, &rule_regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, &rule_regex, err_buf, err_len);
        LOG_ERR(sp, "Rule regex match failed: %s", err_buf);

        return;
    }

    rule->options = calloc(rule->num, sizeof(char*));
    rule->weights = calloc(rule->num, sizeof(unsigned));
    rule->norm_weights = calloc(rule->num, sizeof(double));

    s = source;
    n = 0;
    sum = 0;

    while ((r = regexec(&rule_regex, s, 3, match, 0)) == 0 && n < rule->num) {
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

    regfree(&rule_regex);
    regfree(&time_regex);
}


/*******************************************************************************
** VMOD Functions
*******************************************************************************/

int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
    AZ(pthread_rwlock_wrlock(&cfg_rwl));
    priv->free  = (vmod_priv_free_f *)cfg_free;
    AZ(pthread_rwlock_unlock(&cfg_rwl));
    return (0);
}

void __match_proto__() vmod_set_rule(struct sess *sp, struct vmod_priv *priv, const char *key, const char *rule) {
    AN(key);
    AN(rule);

    AZ(pthread_rwlock_wrlock(&cfg_rwl));

    if (priv->priv == NULL) {
        ALLOC_CFG(priv->priv);
    }

    struct rule *target = get_text_rule(priv->priv, key);
    if (!target) {
        target = alloc_rule(sp, priv->priv, key);
    }

    parse_rule(sp, target, rule);
    AZ(pthread_rwlock_unlock(&cfg_rwl));
}

void __match_proto__() vmod_rem_rule(struct sess *sp, struct vmod_priv *priv, const char *key) {
    AN(key);

    if (priv->priv == NULL) {
        return;
    }

    AZ(pthread_rwlock_wrlock(&cfg_rwl));

    struct rule *rule = get_text_rule(priv->priv, key);
    if (rule != NULL) {
        VTAILQ_REMOVE(&((struct vmod_abtest*)priv->priv)->rules, rule, list);
        delete_rule(rule);
    }

    AZ(pthread_rwlock_unlock(&cfg_rwl));
}

void __match_proto__() vmod_clear(struct sess *sp, struct vmod_priv *priv) {
    if (priv->priv == NULL) {
        return;
    }

    AZ(pthread_rwlock_wrlock(&cfg_rwl));
    cfg_clear(priv->priv);
    AZ(pthread_rwlock_unlock(&cfg_rwl));
}

int __match_proto__() vmod_load_config(struct sess *sp, struct vmod_priv *priv, const char *source) {
    AN(source);

    AZ(pthread_mutex_lock(&cfg_mtx));
    AZ(pthread_rwlock_wrlock(&cfg_rwl));

    FILE *f;
    long file_len;
    char *buf;
    char *s;
    int r;
    regex_t regex;
    regmatch_t match[3];
    struct rule *rule;
    struct vmod_abtest* cfg;

    ALLOC_CFG(cfg);

    if (r = regcomp(&regex, CONF_REGEX, REG_EXTENDED)){
        cfg_free(cfg);
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        AZ(pthread_mutex_unlock(&cfg_mtx));

        size_t err_len = regerror(r, &regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, &regex, err_buf, err_len);
        LOG_ERR(sp, "Could not compile line regex: %s", err_buf);

        return r;
    }

    f = fopen(source, "r");
    if (f == NULL) {
        cfg_free(cfg);
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        AZ(pthread_mutex_unlock(&cfg_mtx));

        LOG_ERR(sp, "Could not open abtest configuration file '%s' for reading.", source);
        return errno;
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

        VTAILQ_INSERT_TAIL(&cfg->rules, rule, list);

        DUP_MATCH(rule->key, s, match[1]);
        alloc_key_regex(sp, cfg, rule, rule->key);
        parse_rule(sp, rule, s + match[2].rm_so);

        s += match[0].rm_eo + 1;
    }

    regfree(&regex);
    free (buf);

    if (r != REG_NOMATCH) {
        cfg_free(cfg);
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        AZ(pthread_mutex_unlock(&cfg_mtx));

        size_t err_len = regerror(r, &regex, NULL, 0);
        char* err_buf = alloca(err_len);
        regerror(r, &regex, err_buf, err_len);
        LOG_ERR(sp, "Regex match failed: %s", err_buf);

        return r;
    }

    if (priv->priv != NULL) {
        cfg_free(priv->priv);
    }
    priv->priv = cfg;

    AZ(pthread_rwlock_unlock(&cfg_rwl));
    AZ(pthread_mutex_unlock(&cfg_mtx));

    return 0;
}

int __match_proto__() vmod_save_config(struct sess *sp, struct vmod_priv *priv, const char *target) {
    AN(target);

    if (priv->priv == NULL) {
        return 0;
    }

    AZ(pthread_mutex_lock(&cfg_mtx));
    AZ(pthread_rwlock_rdlock(&cfg_rwl));

    struct rule *r;
    FILE *f;

    f = fopen(target, "w");
    if (f == NULL) {
        LOG_ERR(sp, "Could not open abtest configuration file '%s' for writing.", target);
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        AZ(pthread_mutex_unlock(&cfg_mtx));
        return errno;
    }

    VTAILQ_FOREACH(r, &((struct vmod_abtest*) priv->priv)->rules, list) {
        int i;
        fprintf(f, "%s:", r->key);
        for (i = 0; i < r->num; i++) {
            fprintf(f, "%s:%d;", r->options[i], r->weights[i]);
        }
        if (r->duration != 0) {
            fprintf(f, "%.0f;", r->duration);
        }
        fprintf(f, "\n");
    }

    AZ(fclose(f));
    AZ(pthread_rwlock_unlock(&cfg_rwl));
    AZ(pthread_mutex_unlock(&cfg_mtx));

    return 0;
}

/*
* Weighted random algorithm from:
* http://erlycoder.com/105/javascript-weighted-random-value-from-array
*/
const char* __match_proto__() vmod_get_rand(struct sess *sp, struct vmod_priv *priv, const char *key) {
    AN(key);

    struct rule *rule;
    double n;       // needle
    unsigned p;     // probe
    unsigned l, h;  // low & high

    if (priv->priv == NULL) {
        return NULL;
    }

    AZ(pthread_rwlock_rdlock(&cfg_rwl));

    rule = ((struct vmod_abtest*)priv->priv)->get_rule(priv->priv, key);
    if (rule == NULL) {
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        return NULL;
    }

    n = drand48();
    l = 0;
    h = rule->num - 1;

    while (l < h) {
        p = (unsigned)ceil((double)(h + l) / 2);
        if (rule->norm_weights[p] < n) {
            l = p + 1;
        } else if (rule->norm_weights[p] > n) {
            h = p - 1;
        } else {
            AZ(pthread_rwlock_unlock(&cfg_rwl));
            return rule->options[p];
        }
    }

    if (l != h) {
        p = rule->norm_weights[l] >= n ? l : p;
    } else {
        p = rule->norm_weights[l] >= n ? l : l+1;
    }
    AZ(pthread_rwlock_unlock(&cfg_rwl));
    return rule->options[p];
}

const char* __match_proto__() vmod_get_rules(struct sess *sp, struct vmod_priv *priv) {
    if (priv->priv == NULL) {
        return NULL;
    }

    AZ(pthread_rwlock_rdlock(&cfg_rwl));

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
        if (r->duration != 0) {
            len += snprintf(NULL, 0, "%.0f;", r->duration);
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
        if (r->duration != 0) {
            l = snprintf(s, len, "%.0f;", r->duration);
            s += l;
            len -= l;
        }
        *s++ = ' ';
        len--;
    }
    *s = 0;

    AZ(pthread_rwlock_unlock(&cfg_rwl));
    return rules;
}

double __match_proto__() vmod_get_duration(struct sess *sp, struct vmod_priv *priv, const char *key) {
    AN(key);

    struct rule *rule;
    if (priv->priv == NULL) {
        return 0;
    }

    AZ(pthread_rwlock_rdlock(&cfg_rwl));

    rule = ((struct vmod_abtest*)priv->priv)->get_rule(priv->priv, key);
    if (rule == NULL) {
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        return 0.;
    }

    AZ(pthread_rwlock_unlock(&cfg_rwl));
    return rule->duration;
}

const char*  __match_proto__() vmod_get_expire(struct sess *sp, struct vmod_priv *priv, const char *key) {
    AN(key);

    struct rule *rule;
    if (priv->priv == NULL) {
        return 0;
    }

    AZ(pthread_rwlock_rdlock(&cfg_rwl));

    double duration = 0.;

    rule = ((struct vmod_abtest*)priv->priv)->get_rule(priv->priv, key);
    if (rule == NULL) {
        AZ(pthread_rwlock_unlock(&cfg_rwl));
        return NULL;
    }

    duration = rule->duration;
    char* expire = VRT_time_string(sp, TIM_real() + duration);

    AZ(pthread_rwlock_unlock(&cfg_rwl));
    return expire;
}