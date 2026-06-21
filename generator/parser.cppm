module;
#include <cctype>
#include <pugixml.hpp>
export module parser;
import std;

export namespace parser {

// Useful docs: https://wayland.freedesktop.org/docs/book/Message_XML.html

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
	std::uint32_t since;
	bool is_destructor;
};

struct Entry {
	std::string name;
	Description description;
	std::int32_t value;
	std::uint32_t since;
};

struct Enum {
	std::string name;
	Description description;
	std::vector<Entry> entries;
	std::uint32_t since;
	bool bitfield;
};

struct Interface {
	std::string name;
	Description description;
	std::uint32_t version;
	std::vector<Declaration> requests;
	std::vector<Declaration> events;
	std::vector<Enum> enums;
	std::vector<std::string> required_interfaces; // Interface modules may depend on eachother, through enums
};

struct Protocol {
	std::string name;
	std::optional<std::string> copyright;
	Description description;
	std::vector<Interface> interfaces;
};

std::string snake_to_pascal(std::string target) {
	bool capitalise_next = true;
	std::size_t written = 0;
	for (char c : target) {
		if (c == '_') {
			capitalise_next = true;
			continue;
		}
		// Because we don't increment written when it is _, it overwrites it with the next character
		// then when we resize it to the number of chars we wrote, it is correct
		target[written++] = capitalise_next ? std::toupper(c) : c;
		capitalise_next = false;
	}
	target.resize(written);
	return target;
}

/*
 *  Types:
 *  int / uint : 32 bits
 *  fixed point: 24 bits whole, 8 bits decimal
 *  object     : 32 bits
 *  new_id     : 32 bits
 *  string     : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits), \0 terminator
 *  array      : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits
 *  fd         : 0 bits, stored in ancillary data of the message
 *  enum       : 32 bits integer, accompanied alongside the main type of int / uint
 */
std::string convert_type(const pugi::xml_node& node, std::vector<std::string>& required_interfaces) {
	std::string_view type = node.attribute("type").as_string();

	// We just return the enum class directory, no need to worry about further types
	if (auto enum_choice = node.attribute("enum")) {
		std::string enum_str = enum_choice.as_string();
		auto seperator = enum_str.find('.');
		if (seperator != std::string_view::npos) {
			std::string interface_name = enum_str.substr(0, seperator);

			// Ensure we haven't already parsed this enum before, otherwise we would've already specified
			// it to be imported
			if (std::ranges::find(required_interfaces, interface_name) == required_interfaces.end()) {
				required_interfaces.push_back(interface_name);
			}
			enum_str = snake_to_pascal(interface_name) + "::" + snake_to_pascal(enum_str.substr(seperator + 1));
		} else {
			enum_str = snake_to_pascal(enum_str);
		}
		return "[[=WlType::Enum]] " + enum_str + "Enum";
	}

	if (type == "int") {
		return "[[=WlType::Int]] std::int32_t";
	} else if (type == "uint") {
		return "[[=WlType::Uint]] std::uint32_t";
	} else if (type == "fixed") {
		return "[[=WlType::Fixed]] std::int32_t";
	} else if (type == "object") {
		return "[[=WlType::Object]] std::uint32_t";
	} else if (type == "new_id") {
		return "[[=WlType::NewId]] std::uint32_t";
	} else if (type == "string") {
		return "[[=WlType::String]] std::string";
	} else if (type == "array") {
		return "[[=WlType::Array]] void*";
	} else if (type == "fd") {
		return "[[=WlType::Fd]] int";
	}

	throw std::invalid_argument(
		"Incorrect wayland type passed, " + std::string(type));
}

std::optional<std::string> optional_string(const pugi::xml_node& node, const char* name) {
	auto attribute = node.attribute(name);
	if (attribute) {
		return snake_to_pascal(attribute.as_string());
	}
	return std::nullopt;
}

Description get_description(const pugi::xml_node& node) {
	auto full = node.text();
	Description description = {
		.summary = optional_string(node, "summary"),
		.full = full ? std::optional(std::string(full.as_string())) : std::nullopt,
	};

	return description;
}

std::uint32_t get_since(const pugi::xml_node& node) {
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

std::string get_pascal_name(const pugi::xml_node& node) {
	return snake_to_pascal(std::move(node.attribute("name").as_string()));
}

std::string get_name(const pugi::xml_node& node) {
	return node.attribute("name").as_string();
}

std::int32_t get_entry_value(const pugi::xml_node& node) {
	// From the docs: https://pugixml.org/docs/manual.html#access.attrdata
	// the default as_int doesn't mention octal support, which is technically
	// valid although I haven't seen it be used in wayland.xml itself.
	// Base 0 means let stoi do whatever the fuck it wants (auto-detect)
	return std::stoi(node.attribute("value").as_string(), nullptr, 0);
}

Declaration get_declaration(const pugi::xml_node& node, std::vector<std::string>& required_interfaces) {
	Declaration declaration = Declaration {
		.name = get_pascal_name(node),
		.description = get_description(node.child("description")),
		.since = get_since(node),
		.is_destructor = get_destructor(node),
	};

	for (pugi::xml_node node : node.children("arg")) {
		declaration.arguments.push_back(Argument {
			.name = get_name(node),
			.type = convert_type(node, required_interfaces),
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
		.name = get_pascal_name(node) + "Enum",
		.description = get_description(node.child("description")),
		.since = get_since(node),
		.bitfield = get_bitfield(node),
	};

	for (pugi::xml_node node : node.children("entry")) {

		// Spec is retarded and sometimes uses numbers as the name, so we need to prefix it to make it valid
		auto name = get_pascal_name(node);
		if (std::isdigit(name.front())) {
			name.insert(name.begin(), '_'); // Prefix it with _ if so
		}

		enum_ret.entries.push_back(Entry {
			.name = name,
			.description = get_description(node),
			.value = get_entry_value(node),
			.since = get_since(node),
		});
	}

	return enum_ret;
}

std::vector<Protocol> get_protocols() {
	std::vector<Protocol> protocols;

	for (const auto& entry : std::filesystem::directory_iterator("./spec")) {
		pugi::xml_document doc;
		pugi::xml_parse_result result = doc.load_file(entry.path().c_str());
		if (!result) {
			throw std::runtime_error(std::format("Failed to load document: {}, {}", entry.path().filename().c_str(), result.description()));
		}

		for (pugi::xml_node node : doc.children("protocol")) {
			Protocol protocol = Protocol {
				.name = get_name(node),
				.copyright = optional_string(node, "copyright"),
				.description = get_description(node),
			};

			for (pugi::xml_node node : node.children("interface")) {
				Interface interface = Interface {
					.name = get_name(node),
					.description = get_description(node),
					.version = node.attribute("version").as_uint(),
				};

				for (pugi::xml_node node : node.children("request")) {
					interface.requests.push_back(get_declaration(node, interface.required_interfaces));
				}
				for (pugi::xml_node node : node.children("event")) {
					interface.events.push_back(get_declaration(node, interface.required_interfaces));
				}
				for (pugi::xml_node node : node.children("enum")) {
					interface.enums.push_back(get_enum(node));
				}

				protocol.interfaces.push_back(std::move(interface));
			}
			protocols.push_back(std::move(protocol));
		}
	}

	return protocols;
}

}; // namespace parser
