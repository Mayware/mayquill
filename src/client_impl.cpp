module mayquill;
import std;
import :client;
import :definitions;

// This is a completely arbitrary split for parse_message, to avoid corrupted module cluster data from the client module
// ie. without this we run into GCC ICE
namespace mayquill {
void Client::process_request(std::vector<std::uint8_t> message) {
	Header header;
	std::memcpy(&header, message.data(), sizeof(Header));
	std::println("Object ID: {}", header.object_id);

	Interface& object = this->objects.at(header.object_id); // TODO sec

	std::visit([&](auto& interface) {
		// Get the actual type
		using T = std::decay_t<decltype(interface)>;

		// Check if the nested request type exists
		// If it does, we'll parse the args accordingly
		// If it doesn't, then there shouldn't be any args
		if constexpr (requires { typename T::Request; }) {
#ifdef MAYQUILL_ICE
			// Template for stamps out each iteration at compile time
			// This is needed, as getting the variant at an index requires a comptime value
			// Template for only supports range-based syntax, hence the iota
			template for (constexpr auto i : std::views::iota(0uz, std::variant_size_v<typename T::Request>)) {
				// I tried to find a way to do a jump table directly, but was unable to
				// In reality, it will probably optimise to if statements since there are few cases
				// but this is something I want to come back to
				if (header.opcode == i) {
					using Alternative = std::variant_alternative_t<i, typename T::Request>;
					auto alternative = deserialise_struct<Alternative>(std::move(message));
					interface.handle(alternative);
                    static constexpr auto wl_declaration = std::meta::extract<WlDeclaration>(
                        // Ask for the reflections on the original type, not on the alias
                        std::meta::annotations_of(std::meta::dealias(^^Alternative))[0]
                    );
                    if constexpr (wl_declaration == WlDeclaration::Destructor) {
                        interface.destroy();
                    }
					return;
				}
			}
#endif
			std::println("No opcode matched: {}", header.opcode);
		}
	},
		object);
}

[[gnu::weak]]
void Client::handle_destroy() {}

[[gnu::weak]]
void Client::handle_init() {}

void Client::destroy() {
    handle_destroy();
    server.remove_client(this);
}


}; // namespace mayquill
