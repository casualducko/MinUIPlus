#include "json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MAX_DEPTH 32

typedef struct { const char* p; const char* end; } Cur;

static JVal* parse_value(Cur* c, int depth);

static void skip_ws(Cur* c) {
	while (c->p<c->end && (*c->p==' ' || *c->p=='\t' || *c->p=='\n' || *c->p=='\r')) c->p++;
}
static JVal* new_val(JType t) {
	JVal* v = calloc(1, sizeof(JVal));
	if (v) v->type = t;
	return v;
}
static int hex4(const char* p, const char* end, unsigned* out) {
	if (end-p<4) return 0;
	unsigned v = 0;
	for (int i=0;i<4;i++) {
		char ch = p[i];
		v <<= 4;
		if (ch>='0' && ch<='9') v |= ch-'0';
		else if (ch>='a' && ch<='f') v |= ch-'a'+10;
		else if (ch>='A' && ch<='F') v |= ch-'A'+10;
		else return 0;
	}
	*out = v;
	return 1;
}
static int put_utf8(char* o, unsigned cp) {
	if (cp<0x80) { o[0] = cp; return 1; }
	if (cp<0x800) { o[0] = 0xC0|(cp>>6); o[1] = 0x80|(cp&0x3F); return 2; }
	if (cp<0x10000) { o[0] = 0xE0|(cp>>12); o[1] = 0x80|((cp>>6)&0x3F); o[2] = 0x80|(cp&0x3F); return 3; }
	o[0] = 0xF0|(cp>>18); o[1] = 0x80|((cp>>12)&0x3F); o[2] = 0x80|((cp>>6)&0x3F); o[3] = 0x80|(cp&0x3F); return 4;
}
// c->p is just after the opening quote; returns a malloc'd string or NULL
static char* parse_string(Cur* c) {
	size_t cap = (size_t)(c->end - c->p) + 1; // an unescaped string is never longer than its source
	char* out = malloc(cap);
	if (!out) return NULL;
	size_t n = 0;
	while (c->p<c->end) {
		unsigned char ch = *c->p++;
		if (ch=='"') { out[n] = '\0'; return out; }
		if (ch<0x20) break; // raw control characters are not allowed
		if (ch!='\\') { out[n++] = ch; continue; }
		if (c->p>=c->end) break;
		char e = *c->p++;
		switch (e) {
			case '"': out[n++] = '"'; break;
			case '\\': out[n++] = '\\'; break;
			case '/': out[n++] = '/'; break;
			case 'b': out[n++] = '\b'; break;
			case 'f': out[n++] = '\f'; break;
			case 'n': out[n++] = '\n'; break;
			case 'r': out[n++] = '\r'; break;
			case 't': out[n++] = '\t'; break;
			case 'u': {
				unsigned cp;
				if (!hex4(c->p, c->end, &cp)) goto fail;
				c->p += 4;
				if (cp>=0xD800 && cp<0xDC00) { // high surrogate, needs a low one
					unsigned lo;
					if (c->end-c->p>=6 && c->p[0]=='\\' && c->p[1]=='u' && hex4(c->p+2, c->end, &lo) && lo>=0xDC00 && lo<0xE000) {
						c->p += 6;
						cp = 0x10000 + ((cp-0xD800)<<10) + (lo-0xDC00);
					}
					else cp = 0xFFFD;
				}
				else if (cp>=0xDC00 && cp<0xE000) cp = 0xFFFD; // lone low surrogate
				// an escape (6 bytes) never expands past 4 bytes of UTF-8, so cap holds
				n += put_utf8(out+n, cp);
				break;
			}
			default: goto fail;
		}
	}
fail:
	free(out);
	return NULL;
}
static int add_item(JVal* v, char* key, JVal* item) {
	JVal** items = realloc(v->items, sizeof(JVal*) * (v->count+1));
	if (!items) return 0;
	v->items = items;
	if (v->type==J_OBJ) {
		char** keys = realloc(v->keys, sizeof(char*) * (v->count+1));
		if (!keys) return 0;
		v->keys = keys;
		v->keys[v->count] = key;
	}
	v->items[v->count++] = item;
	return 1;
}
static JVal* parse_value(Cur* c, int depth) {
	if (depth>MAX_DEPTH) return NULL;
	skip_ws(c);
	if (c->p>=c->end) return NULL;
	char ch = *c->p;
	if (ch=='"') {
		c->p++;
		char* s = parse_string(c);
		if (!s) return NULL;
		JVal* v = new_val(J_STR);
		if (!v) { free(s); return NULL; }
		v->str = s;
		return v;
	}
	if (ch=='{' || ch=='[') {
		int obj = ch=='{';
		char close = obj ? '}' : ']';
		c->p++;
		JVal* v = new_val(obj ? J_OBJ : J_ARR);
		if (!v) return NULL;
		skip_ws(c);
		if (c->p<c->end && *c->p==close) { c->p++; return v; }
		for (;;) {
			char* key = NULL;
			skip_ws(c);
			if (obj) {
				if (c->p>=c->end || *c->p!='"') goto fail;
				c->p++;
				key = parse_string(c);
				if (!key) goto fail;
				skip_ws(c);
				if (c->p>=c->end || *c->p!=':') { free(key); goto fail; }
				c->p++;
			}
			JVal* item = parse_value(c, depth+1);
			if (!item) { free(key); goto fail; }
			if (!add_item(v, key, item)) { free(key); json_free(item); goto fail; }
			skip_ws(c);
			if (c->p>=c->end) goto fail;
			if (*c->p==',') { c->p++; continue; }
			if (*c->p==close) { c->p++; return v; }
			goto fail;
		}
	fail:
		json_free(v);
		return NULL;
	}
	if (c->end-c->p>=4 && !strncmp(c->p, "true", 4)) { c->p += 4; JVal* v = new_val(J_BOOL); if (v) v->num = 1; return v; }
	if (c->end-c->p>=5 && !strncmp(c->p, "false", 5)) { c->p += 5; return new_val(J_BOOL); }
	if (c->end-c->p>=4 && !strncmp(c->p, "null", 4)) { c->p += 4; return new_val(J_NULL); }
	if (ch=='-' || (ch>='0' && ch<='9')) {
		char buf[64];
		size_t n = 0;
		while (c->p<c->end && n<sizeof(buf)-1 && (*c->p=='-' || *c->p=='+' || *c->p=='.' || *c->p=='e' || *c->p=='E' || (*c->p>='0' && *c->p<='9'))) buf[n++] = *c->p++;
		buf[n] = '\0';
		char* endp;
		double d = strtod(buf, &endp);
		if (endp==buf || *endp) return NULL;
		JVal* v = new_val(J_NUM);
		if (v) v->num = d;
		return v;
	}
	return NULL;
}

JVal* json_parse(const char* text, size_t len) {
	if (!text) return NULL;
	Cur c = { text, text+len };
	JVal* v = parse_value(&c, 0);
	if (!v) return NULL;
	skip_ws(&c);
	if (c.p!=c.end) { json_free(v); return NULL; } // trailing garbage
	return v;
}
void json_free(JVal* v) {
	if (!v) return;
	for (int i=0; i<v->count; i++) {
		json_free(v->items[i]);
		if (v->type==J_OBJ) free(v->keys[i]);
	}
	free(v->items);
	free(v->keys);
	free(v->str);
	free(v);
}
JVal* json_get(const JVal* obj, const char* key) {
	if (!obj || obj->type!=J_OBJ) return NULL;
	for (int i=0; i<obj->count; i++) if (!strcmp(obj->keys[i], key)) return obj->items[i];
	return NULL;
}
const char* json_str(const JVal* obj, const char* key, const char* fallback) {
	JVal* v = json_get(obj, key);
	return (v && v->type==J_STR) ? v->str : fallback;
}
long long json_int(const JVal* obj, const char* key, long long fallback) {
	JVal* v = json_get(obj, key);
	if (v && v->type==J_NUM) return (long long)v->num;
	if (v && v->type==J_STR) { char* e; long long n = strtoll(v->str, &e, 10); if (e!=v->str && !*e) return n; }
	return fallback;
}
