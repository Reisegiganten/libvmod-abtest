#include <stdlib.h>
#include <ctype.h>
#include <stddef.h>

#include "vrt.h"
#include "bin/varnishd/cache.h"

#include "vcc_if.h"

#include "vmod_abtest.h"

struct rule {
	unsigned* probability;
	char* names;

	VTAILQ_ENTRY(rule) list;
};

struct vmod_abtest {
	unsigned magic;
#define VMOD_ABTEST_MAGIC 0xDD2914D8
	unsigned xid;
	VTAILQ_HEAD(, rule) rules;
};

static struct vmod_abtest **vmod_abtest_list;
int vmod_abtest_list_sz;
static pthread_mutex_t cfg_mtx = PTHREAD_MUTEX_INITIALIZER;

static void cfg_init(struct vmod_abtest *c) {
	c->magic = VMOD_ABTEST_MAGIC;
	VTAILQ_INIT(&c->rules);
	cfg_clear(c);
}

static void cfg_clear(struct vmod_abtest *cfg) {
	CHECK_OBJ_NOTNULL(cfg, VMOD_ABTEST_MAGIC);

	struct rule *r, *r2;
	VTAILQ_FOREACH_SAFE(r, &cfg->rules, list, r2) {
		VTAILQ_REMOVE(&cfg->rules, r, list);
		free(r->probability);
		free(r->names);
		free(r);
	}
	cfg->xid = 0;
	cfg->magic = 0;
}


int init_function(struct vmod_priv *priv, const struct VCL_conf *conf) {
	int i;

	vmod_abtest_list = NULL;
	vmod_abtest_list_sz = 256;

	vmod_abtest_list = malloc(sizeof(struct vmod_abtest *) * 256);
	AN(vmod_abtest_list);

	for (i = 0; i < vmod_abtest_list_sz; i++) {
		vmod_abtest_list[i] = malloc(sizeof(struct rule));
		cfg_init(vmod_abtest_list[i]);
	}

	return (0);
}

void vmod_set_rule(struct sess *sp, struct vmod_priv *priv, const char *key, const char *rule) {
}

void vmod_clear(struct sess *sp, struct vmod_priv *priv) {
}

void vmod_load_config(struct sess *sp, struct vmod_priv *priv, const char *source) {
}

void vmod_save_config(struct sess *sp, struct vmod_priv *priv, const char *target) {

}

const char* vmod_get_version(struct sess *sp, struct vmod_priv *priv, const char *key) {
	char *p;
	unsigned u, v;

	u = WS_Reserve(sp->wrk->ws, 0); /* Reserve some work space */
	p = sp->wrk->ws->f;		/* Front of workspace area */
	//v = snprintf(p, u, "Hello, %s", name);
	v++;
	if (v > u) {
		/* No space, reset and leave */
		WS_Release(sp->wrk->ws, 0);
		return (NULL);
	}
	/* Update work space with what we've used */
	WS_Release(sp->wrk->ws, v);
	return (p);
}
