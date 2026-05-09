module;
#include <pugixml.hpp>
export module parser;
import std;

export namespace parser {

// Useful docs: https://wayland.freedesktop.org/docs/book/Message_XML.html

/*
 *  Types:
 *  int / uint : 32 bits
 *  fixed point: 24 bits whole, 8 bits decimal
 *  object     : 32 bits
 *  new_id     : 32 bits
 *  string     : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits), \0 terminator
 *  array      : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits
 *  fd         : 0 bits, stored in ancillary data of the message
 *  enum       : 32 bits integer
 */
std::unordered_map<std::string, std::string> types = {
	{"int", "int32_t"},
	{"uint", "uint32_t"},
	{"fixed", "int32_t"},
	{"object", "uint32_t"},
	{"new_id", "uint32_t"},
	{"string", "std::string"},
	{"array", "void*"},
	{"fd", "int"},
	{"enum", "uint32_t"},
};

// We do not parse frozen, or deprecated-since
struct Description {
	std::optional<std::string> summary;
	std::optional<std::string> full;
};

struct Argument {
	std::string name;
	std::string type;
	Description description;
	std::optional<std::string> interface_name;
	std::optional<std::string> enum_name;
	bool allow_null;
};

struct Declaration {
	std::string name;
	Description description;
	std::vector<Argument> arguments;
	uint since;
	bool is_destructor;
};

struct Entry {
	std::string name;
	Description description;
	int value;
	uint since;
};

struct Enum {
	std::string name;
	Description description;
	std::vector<Entry> entries;
	uint since;
	bool bitfield;
};

struct Interface {
	std::string name;
	Description description;
	uint version;
	std::vector<Declaration> requests;
	std::vector<Declaration> events;
	std::vector<Enum> enums;
};

struct Protocol {
	std::string name;
	std::optional<std::string> copyright;
	Description description;
	std::vector<Interface> interfaces;
};

struct File {
	std::string name;
	std::vector<Protocol> protocols;
};

std::optional<std::string> optional_string(const pugi::xml_node& node, const char* name) {
	auto attribute = node.attribute(name);
	return attribute ? std::optional<std::string> {attribute.as_string()} : std::nullopt;
}

Description get_description(const pugi::xml_node& node) {
	auto full = node.text();
	Description description = {
		.summary = optional_string(node, "summary"),
		.full = full ? std::optional(std::string(full.as_string())) : std::nullopt,
	};

	return description;
}

uint get_since(const pugi::xml_node& node) {
	auto since = node.attribute("since");
	return since ? since.as_uint() : 1;
}

bool get_allow_null(const pugi::xml_node& node) {
	auto allow_null = node.attribute("allow-null");
	return allow_null ? allow_null.as_bool() : false;
}

bool get_destructor(const pugi::xml_node& node) {
	auto type = node.attribute("type");

	// API is a bit retarded, because from what i can see, the type can only ever be
	// destructor for a declaration, but i've added the check anyways
	// https://wayland.freedesktop.org/docs/book/Message_XML.html#typedestructor
	return type ? (std::string(type.as_string()) == "destructor" ? true : false) : false;
}

bool get_bitfield(const pugi::xml_node& node) {
	auto bitfield = node.attribute("bitfield");
	return bitfield ? bitfield.as_bool() : false;
}

int get_entry_value(const pugi::xml_node& node) {
	// From the docs: https://pugixml.org/docs/manual.html#access.attrdata
	// the default as_int doesn't mention octal support, which is technically
	// valid although I haven't seen it be used in wayland.xml itself.
	// Base 0 means let stoi do whatever the fuck it wants (auto-detect)
	return std::stoi(node.attribute("value").as_string(), nullptr, 0);
}

Declaration get_declaration(const pugi::xml_node& node) {
	Declaration declaration = Declaration {
		.name = node.attribute("name").as_string(),
		.description = get_description(node.child("description")),
		.since = get_since(node),
		.is_destructor = get_destructor(node),
	};

	for (pugi::xml_node node : node.children("arg")) {
		declaration.arguments.push_back(Argument {
			.name = node.attribute("name").as_string(),
			.type = types[node.attribute("type").as_string()], // This is the only place we do conversion on parse
			.description = get_description(node),
			.interface_name = optional_string(node, "interface"),
			.enum_name = optional_string(node, "enum"),
			.allow_null = get_allow_null(node),
		});
	}

	return declaration;
}

Enum get_enum(const pugi::xml_node& node) {
	Enum enum_ret = Enum {
		.name = node.attribute("name").as_string(),
		.description = get_description(node.child("description")),
		.since = get_since(node),
		.bitfield = get_bitfield(node),
	};

	for (pugi::xml_node node : node.children("entry")) {
		enum_ret.entries.push_back(Entry {
			.name = node.attribute("name").as_string(),
			.description = get_description(node),
			.value = get_entry_value(node),
			.since = get_since(node),
		});
	}

	return enum_ret;
}

std::vector<File> get_parsed() {
	std::vector<File> files;

	for (const auto& entry : std::filesystem::directory_iterator("./in")) {
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(entry.path().c_str());
		if (!result) {
			throw std::runtime_error(std::format("Failed to load document: {}, {}", entry.path().filename().c_str(), result.description()));
		}

		// out += "/* Generated by MayQuill, godspeed */";
		std::vector<Protocol> protocols;

		for (pugi::xml_node node : doc.children("protocol")) {
			Protocol protocol = Protocol {
				.name = node.attribute("name").as_string(),
				.copyright = optional_string(node, "copyright"),
				.description = get_description(node),
			};

			for (pugi::xml_node node : node.children()) {
				Interface interface = Interface {
					.name = node.attribute("name").as_string(),
					.description = get_description(node),
					.version = node.attribute("version").as_uint(),
				};

				for (pugi::xml_node node : node.children("request")) {
					interface.requests.push_back(get_declaration(node));
				}
				for (pugi::xml_node node : node.children("event")) {
					interface.events.push_back(get_declaration(node));
				}
				for (pugi::xml_node node : node.children("enum")) {
					interface.enums.push_back(get_enum(node));
				}

				protocol.interfaces.push_back(interface);
			}
		}

		files.push_back(File {
			.name = entry.path().filename(),
			.protocols = std::move(protocols),
		});
	}

	return files;
}

}; // namespace parser
