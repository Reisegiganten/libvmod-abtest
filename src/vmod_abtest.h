struct vmod_abtest;
struct rule;

static void cfg_init(struct vmod_abtest *cfg);
static void cfg_clear(struct vmod_abtest *cfg);
static void cfg_free(struct vmod_abtest *cfg);

static void delete_rule(struct rule* r);
static struct rule* get_regex_rule(struct vmod_abtest *cfg, VCL_STRING key);
static struct rule* get_text_rule(struct vmod_abtest *cfg, VCL_STRING key);
static struct rule* alloc_rule(VRT_CTX, struct vmod_abtest *cfg, VCL_STRING key);

static void write_log(struct vsl_log *vsl, enum VSL_tag_e tag,  const char *fmt, ...);
