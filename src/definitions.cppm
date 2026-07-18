module mayquill:definitions;
import std;

namespace mayquill {
enum class WlType {
	Int,
	Uint,
	Fixed,
	Object,
	NullableObject,
	NewId,
	String,
	NullableString,
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

#ifdef MAYQUILL_ICE
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

// T isn't used directly, but T acts to make it unique for T
template<typename T>
constexpr bool enable_bitfield_operators = false;

// This operator has global scope within mayquill
template<typename T>
	requires enable_bitfield_operators<T> // Only enable the operator, if they've set enable_bitfield_operators true for their T
constexpr T operator|(T left, T right) {
	return static_cast<T>(std::to_underlying(left) | std::to_underlying(right));
}

/* It would then be used like
template<> constexpr bool enable_bitfield_operators<DndActionEnum> = true;
* General template specialisation syntax, to specify that for that T, enable_bitfield_operator is set to true
*/
} // namespace mayquill
