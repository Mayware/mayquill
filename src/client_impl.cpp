module;
#include "mayquill/logger.h"
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

	auto it = this->objects.find(header.object_id);
	if (it == this->objects.end()) {
		this->error(1, WlDisplay::ErrorEnum::InvalidObject, std::format("Attempted to send a request for object id {}, which doesn't exist!", header.object_id));
		return;
	}
	Interface& object = it->second;

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
					Alternative alternative;
					// Deserialise struct throws if it's invalid, so just catch and convert to a client error
					try {
						alternative = deserialise_struct<Alternative>(std::span(message));
					} catch (const std::runtime_error& e) {
						this->error(header.object_id, WlDisplay::ErrorEnum::InvalidMethod, e.what());
						return;
					}
					MQ_DEBUG("Request {}", log_wl_struct(alternative, header.object_id, header.opcode));
					interface.handle(alternative);
					static constexpr auto wl_declaration = std::meta::extract<WlDeclaration>(
						// Ask for the reflections on the original type, not on the alias
						std::meta::annotations_of(std::meta::dealias(^^Alternative))[0]);
					if constexpr (wl_declaration == WlDeclaration::Destructor) {
						interface.destroy();
					}
					return;
				}
			}
#endif
			this->error(header.object_id, WlDisplay::ErrorEnum::InvalidMethod, std::format("No opcode matches {}", header.opcode));
		} else {
			this->error(header.object_id, WlDisplay::ErrorEnum::InvalidMethod, std::format("Attempted to call opcode {} on an interface with no requests", header.opcode));
		}
	},
		object);
}

[[gnu::weak]]
void Client::handle_destroy() {}

[[gnu::weak]]
void Client::handle_init() {}

void Client::destroy() {
	// No iterator, so we don't invalidate it
	while (objects.size() > 1) { // 1, because we don't destroy the display here
		// Anything but the wl_display, we'll need to delete that last
		auto it = std::ranges::find_if(objects, [](const auto& slot) {
			return slot.first != 1;
		});
		std::visit([](auto& object) {
			object.destroy();
		},
			it->second);
	}
    get_display().destroy();
	handle_destroy();
	server.remove_client(this);
}
}; // namespace mayquill
