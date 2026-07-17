export module mayquill.definitions;
import std;

export namespace mayquill {
enum class WlType {
	Int,
	Uint,
	Fixed,
	Object,
	NewId,
	String,
	Array,
	Fd,
	Enum
};

enum class WlDeclaration {
	None,
	Destructor,
};

struct Header {
	std::uint32_t object_id;
	std::uint16_t opcode;
	std::uint16_t size;
};

#ifndef __clang__
consteval auto get_wl_types(std::span<const std::meta::info> fields) {
	std::vector<WlType> wl_types(fields.size());

	for (std::size_t i = 0; i < fields.size(); ++i) {
		wl_types[i] = std::meta::extract<WlType>(
			std::meta::annotations_of(fields[i])[0]);
	}

	return std::define_static_array(wl_types);
}
#endif

std::optional<std::string> get_env(const std::string& name) {
	if (const char* value = std::getenv(name.c_str())) {
		return value;
	}

	return std::nullopt;
}
} // namespace mayquill
