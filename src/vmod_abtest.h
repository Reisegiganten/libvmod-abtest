struct vmod_abtest;
struct rule;

static void cfg_init(struct vmod_abtest *cfg);
static void cfg_clear(struct vmod_abtest *cfg);
static void cfg_free(struct vmod_abtest *cfg);

static void delete_rule(struct rule* r);
static struct rule* get_regex_rule(struct vmod_abtest *cfg, const char *key);
static struct rule* get_text_rule(struct vmod_abtest *cfg, const char *key);
static struct rule* alloc_rule(struct sess *sp, struct vmod_abtest *cfg, const char *key);
