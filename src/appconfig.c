#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "avl.h"
#include "common.h"
#include "appconfig.h"
#include "log.h"

#define CONFIG_FILE_LINE_MAX ((CONFIG_MAX_NAME + CONFIG_MAX_VALUE + 1024) * 2)

pthread_rwlock_t config_rwlock = PTHREAD_RWLOCK_INITIALIZER;

// ----------------------------------------------------------------------------
// definitions

#define CONFIG_VALUE_LOADED  0x01 // has been loaded from the config
#define CONFIG_VALUE_USED    0x02 // has been accessed from the program
#define CONFIG_VALUE_CHANGED 0x04 // has been changed from the loaded value
#define CONFIG_VALUE_CHECKED 0x08 // has been checked if the value is different from the default

struct config_value {
	avl avl;				// the index - this has to be first!

	uint32_t hash;			// a simple hash to speed up searching
							// we first compare hashes, and only if the hashes are equal we do string comparisons

	char *name;
	char *value;

	uint8_t	flags;

	struct config_value *next;
};

struct config {
	avl avl;

	uint32_t hash;			// a simple hash to speed up searching
							// we first compare hashes, and only if the hashes are equal we do string comparisons

	char *name;

	struct config_value *values;
	avl_tree values_index;

	struct config *next;

	pthread_rwlock_t rwlock;
} *config_root = NULL;


// ----------------------------------------------------------------------------
// config value

static int config_value_iterator(avl *a) { if(a) {}; return 0; }

static int config_value_compare(void* a, void* b) {
	if(((struct config_value *)a)->hash < ((struct config_value *)b)->hash) return -1;
	else if(((struct config_value *)a)->hash > ((struct config_value *)b)->hash) return 1;
	else return strcmp(((struct config_value *)a)->name, ((struct config_value *)b)->name);
}

#define config_value_index_add(co, cv) avl_insert(&((co)->values_index), (avl *)(cv))
#define config_value_index_del(co, cv) avl_remove(&((co)->values_index), (avl *)(cv))

static struct config_value *config_value_index_find(struct config *co, const char *name, uint32_t hash) {
	struct config_value *result = NULL, tmp;
	tmp.hash = (hash)?hash:simple_hash(name);
	tmp.name = (char *)name;

	avl_search(&(co->values_index), (avl *)&tmp, config_value_iterator, (avl **)&result);
	return result;
}

// ----------------------------------------------------------------------------
// config

static int config_iterator(avl *a) { if(a) {}; return 0; }

static int config_compare(void* a, void* b) {
	if(((struct config *)a)->hash < ((struct config *)b)->hash) return -1;
	else if(((struct config *)a)->hash > ((struct config *)b)->hash) return 1;
	else return strcmp(((struct config *)a)->name, ((struct config *)b)->name);
}

avl_tree config_root_index = {
		NULL,
		config_compare,
#ifndef AVL_WITHOUT_PTHREADS
#ifdef AVL_LOCK_WITH_MUTEX
		PTHREAD_MUTEX_INITIALIZER
#else
		PTHREAD_RWLOCK_INITIALIZER
#endif
#endif
};

#define config_index_add(cfg) avl_insert(&config_root_index, (avl *)(cfg))
#define config_index_del(cfg) avl_remove(&config_root_index, (avl *)(cfg))

static struct config *config_index_find(const char *name, uint32_t hash) {
	struct config *result = NULL, tmp;
	tmp.hash = (hash)?hash:simple_hash(name);
	tmp.name = (char *)name;

	avl_search(&config_root_index, (avl *)&tmp, config_iterator, (avl **)&result);
	return result;
}

struct config_value *config_value_create(struct config *co, const char *name, const char *value)
{
	debug(D_CONFIG, "Creating config entry for name '%s', value '%s', in section '%s'.", name, value, co->name);

	struct config_value *cv = calloc(1, sizeof(struct config_value));
	if(!cv) fatal("Cannot allocate config_value");

	cv->name = strdup(name);
	if(!cv->name) fatal("Cannot allocate config.name");
	cv->hash = simple_hash(cv->name);

	cv->value = strdup(value);
	if(!cv->value) fatal("Cannot allocate config.value");

	config_value_index_add(co, cv);

	// no need for string termination, due to calloc()

	pthread_rwlock_wrlock(&co->rwlock);

	struct config_value *cv2 = co->values;
	if(cv2) {
		while (cv2->next) cv2 = cv2->next;
		cv2->next = cv;
	}
	else co->values = cv;

	pthread_rwlock_unlock(&co->rwlock);

	return cv;
}

struct config *config_create(const char *section)
{
	debug(D_CONFIG, "Creating section '%s'.", section);

	struct config *co = calloc(1, sizeof(struct config));
	if(!co) fatal("Cannot allocate config");

	co->name = strdup(section);
	if(!co->name) fatal("Cannot allocate config.name");
	co->hash = simple_hash(co->name);

	pthread_rwlock_init(&co->rwlock, NULL);
	avl_init(&co->values_index, config_value_compare);

	config_index_add(co);

	// no need for string termination, due to calloc()

	pthread_rwlock_wrlock(&config_rwlock);

	struct config *co2 = config_root;
	if(co2) {
		while (co2->next) co2 = co2->next;
		co2->next = co;
	}
	else config_root = co;

	pthread_rwlock_unlock(&config_rwlock);

	return co;
}

struct config *config_find_section(const char *section)
{
	return config_index_find(section, 0);
}

int load_config(char *filename, int overwrite_used)
{
	int line = 0;
	struct config *co = NULL;

	char buffer[CONFIG_FILE_LINE_MAX + 1], *s;

	if(!filename) filename = CONFIG_DIR "/" CONFIG_FILENAME;
	FILE *fp = fopen(filename, "r");
	if(!fp) {
		error("Cannot open file '%s'", filename);
		return 0;
	}

	while(fgets(buffer, CONFIG_FILE_LINE_MAX, fp) != NULL) {
		buffer[CONFIG_FILE_LINE_MAX] = '\0';
		line++;

		s = trim(buffer);
		if(!s) {
			debug(D_CONFIG, "Ignoring line %d, it is empty.", line);
			continue;
		}

		int len = (int) strlen(s);
		if(*s == '[' && s[len - 1] == ']') {
			// new section
			s[len - 1] = '\0';
			s++;

			co = config_find_section(s);
			if(!co) co = config_create(s);

			continue;
		}

		if(!co) {
			// line outside a section
			error("Ignoring line %d ('%s'), it is outside all sections.", line, s);
			continue;
		}

		char *name = s;
		char *value = strchr(s, '=');
		if(!value) {
			error("Ignoring line %d ('%s'), there is no = in it.", line, s);
			continue;
		}
		*value = '\0';
		value++;

		name = trim(name);
		value = trim(value);

		if(!name) {
			error("Ignoring line %d, name is empty.", line);
			continue;
		}
		if(!value) {
			debug(D_CONFIG, "Ignoring line %d, value is empty.", line);
			continue;
		}

		struct config_value *cv = config_value_index_find(co, name, 0);

		if(!cv) cv = config_value_create(co, name, value);
		else {
			if(((cv->flags & CONFIG_VALUE_USED) && overwrite_used) || !(cv->flags & CONFIG_VALUE_USED)) {
				debug(D_CONFIG, "Overwriting '%s/%s'.", line, co->name, cv->name);
				free(cv->value);
				cv->value = strdup(value);
				if(!cv->value) fatal("Cannot allocate config.value");
			}
			else
				debug(D_CONFIG, "Ignoring line %d, '%s/%s' is already present and used.", line, co->name, cv->name);
		}
		cv->flags |= CONFIG_VALUE_LOADED;
	}

	fclose(fp);

	return 1;
}

char *config_get(const char *section, const char *name, const char *default_value)
{
	struct config_value *cv;

	debug(D_CONFIG, "request to get config in section '%s', name '%s', default_value '%s'", section, name, default_value);

	struct config *co = config_find_section(section);
	if(!co) co = config_create(section);

	cv = config_value_index_find(co, name, 0);
	if(!cv) {
		cv = config_value_create(co, name, default_value);
		if(!cv) return NULL;
	}
	cv->flags |= CONFIG_VALUE_USED;

	if((cv->flags & CONFIG_VALUE_LOADED) || (cv->flags & CONFIG_VALUE_CHANGED)) {
		// this is a loaded value from the config file
		// if it is different that the default, mark it
		if(!(cv->flags & CONFIG_VALUE_CHECKED)) {
			if(strcmp(cv->value, default_value) != 0) cv->flags |= CONFIG_VALUE_CHANGED;
			cv->flags |= CONFIG_VALUE_CHECKED;
		}
	}

	return(cv->value);
}

long long config_get_number(const char *section, const char *name, long long value)
{
	char buffer[100], *s;
	sprintf(buffer, "%lld", value);

	s = config_get(section, name, buffer);
	if(!s) return value;

	return strtoll(s, NULL, 0);
}

int config_get_boolean(const char *section, const char *name, int value)
{
	char *s;
	if(value) s = "yes";
	else s = "no";

	s = config_get(section, name, s);
	if(!s) return value;

	if(!strcmp(s, "yes")) return 1;
	else return 0;
}

int config_get_boolean_ondemand(const char *section, const char *name, int value)
{
	char *s;

	if(value == CONFIG_ONDEMAND_ONDEMAND)
		s = "on demand";

	else if(value == CONFIG_ONDEMAND_NO)
		s = "no";

	else
		s = "yes";

	s = config_get(section, name, s);
	if(!s) return value;

	if(!strcmp(s, "yes"))
		return CONFIG_ONDEMAND_YES;
	else if(!strcmp(s, "no"))
		return CONFIG_ONDEMAND_NO;
	else if(!strcmp(s, "on demand"))
		return CONFIG_ONDEMAND_ONDEMAND;

	return value;
}

const char *config_set_default(const char *section, const char *name, const char *value)
{
	struct config_value *cv;

	debug(D_CONFIG, "request to set config in section '%s', name '%s', value '%s'", section, name, value);

	struct config *co = config_find_section(section);
	if(!co) return config_set(section, name, value);

	cv = config_value_index_find(co, name, 0);
	if(!cv) return config_set(section, name, value);

	cv->flags |= CONFIG_VALUE_USED;

	if(cv->flags & CONFIG_VALUE_LOADED)
		return cv->value;

	if(strcmp(cv->value, value) != 0) {
		cv->flags |= CONFIG_VALUE_CHANGED;

		free(cv->value);
		cv->value = strdup(value);
		if(!cv->value) fatal("Cannot allocate config.value");
	}

	return cv->value;
}

const char *config_set(const char *section, const char *name, const char *value)
{
	struct config_value *cv;

	debug(D_CONFIG, "request to set config in section '%s', name '%s', value '%s'", section, name, value);

	struct config *co = config_find_section(section);
	if(!co) co = config_create(section);

	cv = config_value_index_find(co, name, 0);
	if(!cv) cv = config_value_create(co, name, value);
	cv->flags |= CONFIG_VALUE_USED;

	if(strcmp(cv->value, value) != 0) {
		cv->flags |= CONFIG_VALUE_CHANGED;

		free(cv->value);
		cv->value = strdup(value);
		if(!cv->value) fatal("Cannot allocate config.value");
	}

	return value;
}

long long config_set_number(const char *section, const char *name, long long value)
{
	char buffer[100];
	sprintf(buffer, "%lld", value);

	config_set(section, name, buffer);

	return value;
}

int config_set_boolean(const char *section, const char *name, int value)
{
	char *s;
	if(value) s = "yes";
	else s = "no";

	config_set(section, name, s);

	return value;
}

void generate_config(BUFFER *wb, int only_changed)
{
	int i, pri;
	struct config *co;
	struct config_value *cv;

	for(i = 0; i < 3 ;i++) {
		switch(i) {
			case 0:
				buffer_strcat(wb,
					"# NetData Configuration\n"
					"# You can uncomment and change any of the options below.\n"
					"# The value shown in the commented settings, is the default value.\n"
					"\n# global netdata configuration\n");
				break;

			case 1:
				buffer_strcat(wb, "\n\n# per plugin configuration\n");
				break;

			case 2:
				buffer_strcat(wb, "\n\n# per chart configuration\n");
				break;
		}

		pthread_rwlock_wrlock(&config_rwlock);
		for(co = config_root; co ; co = co->next) {
			if(strcmp(co->name, "global") == 0 || strcmp(co->name, "plugins") == 0) pri = 0;
			else if(strncmp(co->name, "plugin:", 7) == 0) pri = 1;
			else pri = 2;

			if(i == pri) {
				int used = 0;
				int changed = 0;
				int count = 0;

				pthread_rwlock_wrlock(&co->rwlock);

				for(cv = co->values; cv ; cv = cv->next) {
					used += (cv->flags & CONFIG_VALUE_USED)?1:0;
					changed += (cv->flags & CONFIG_VALUE_CHANGED)?1:0;
					count++;
				}

				pthread_rwlock_unlock(&co->rwlock);

				if(!count) continue;
				if(only_changed && !changed) continue;

				if(!used) {
					buffer_sprintf(wb, "\n# node '%s' is not used.", co->name);
				}

				buffer_sprintf(wb, "\n[%s]\n", co->name);

				pthread_rwlock_wrlock(&co->rwlock);
				for(cv = co->values; cv ; cv = cv->next) {

					if(used && !(cv->flags & CONFIG_VALUE_USED)) {
						buffer_sprintf(wb, "\n\t# option '%s' is not used.\n", cv->name);
					}
					buffer_sprintf(wb, "\t%s%s = %s\n", ((!(cv->flags & CONFIG_VALUE_CHANGED)) && (cv->flags & CONFIG_VALUE_USED))?"# ":"", cv->name, cv->value);
				}
				pthread_rwlock_unlock(&co->rwlock);
			}
		}
		pthread_rwlock_unlock(&config_rwlock);
	}
}

