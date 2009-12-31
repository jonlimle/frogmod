// JSON reader and writer
#ifndef JSON_H_
#define JSON_H_

#include <vector>
#include <map>

namespace JSON {

enum Type {
	NullType,
	BoolType,
	NumberType,
	StringType,
	ArrayType,
	ObjectType
};

struct Value;
typedef std::vector<Value> Vector;
typedef std::map<char *, Value> Object;

struct Value {
	Type type;
	union {
		bool b;
		float f;
		char *s;
		Vector *v;
		Object *o;
	} value;
	bool parse(char *str);
};

}

#endif /* JSON_H_ */
