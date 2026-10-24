// A small JSON reader, enough for the romulus API. Parses into a tree of JVal.
#ifndef ROMULUS_JSON_H
#define ROMULUS_JSON_H
#include <stddef.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal {
	JType type;
	double num;			// J_NUM, and J_BOOL as 0/1
	char* str;			// J_STR
	struct JVal** items;	// J_ARR items, J_OBJ values
	char** keys;			// J_OBJ keys
	int count;
} JVal;

JVal* json_parse(const char* text, size_t len); // NULL if it isn't valid JSON (or nested deeper than 32)
void json_free(JVal* v);
JVal* json_get(const JVal* obj, const char* key); // NULL if absent or not an object
const char* json_str(const JVal* obj, const char* key, const char* fallback);
long long json_int(const JVal* obj, const char* key, long long fallback);
#endif
