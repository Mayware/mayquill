module;
#include <cassert>
export module mayquill:client;
import std;
import :definitions;
import :interface;

export namespace mayquill {
struct Client {
	int fd;
	std::vector<std::optional<Interface>> objects; // Index is the objectid, 0th index is wasted

	// Part of messages received
	std::vector<std::uint8_t> data;
	std::vector<int> fds;

	Client(int fd) : fd(fd) {}

	template<typename T>
	void add_object(std::uint32_t id) {
		if (id >= objects.size()) {
			objects.resize(id + 1);
		}
		objects[id] = T {
			.client = this,
			.id = id};
	}

	template<WlType Wl, typename T>
	T deserialise_field(std::vector<std::uint8_t>& message) {
		if constexpr (Wl == WlType::Int ||
					  Wl == WlType::Uint ||
					  Wl == WlType::Object ||
					  Wl == WlType::NewId ||
					  Wl == WlType::Enum) {
			T value;
			std::memcpy(&value, message.data(), sizeof(value));
			message.erase(message.begin(), message.begin() + sizeof(value));
			return value;
		} else if constexpr (Wl == WlType::Fixed) {
			std::int32_t raw;
			std::memcpy(&raw, message.data(), sizeof(raw));
			message.erase(message.begin(), message.begin() + sizeof(raw));

			// Divide by 2^8, to get the binary point leftwards 8 places
			T value = static_cast<T>(raw) / 256.0;
			return value;
		} else if constexpr (Wl == WlType::Fd) {
			assert(!this->fds.empty() && "Expected an FD, but vector was empty");
			T value;
			std::memcpy(&value, &fds.front(), sizeof(T));
			this->fds.erase(this->fds.begin());
			// No need to consume byte stream, as FDs are purely ancillary data
			// This also prevented us from having a separate Vec<Messages>, because
			// with no byte marker, we're unable to deliminate between which message
			// an FD belongs to
			return value;
		} else if constexpr (Wl == WlType::String) {
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit, plus a null terminator at the end (included in byte count)
			std::uint32_t count;
			std::memcpy(&count, message.data(), sizeof(count));
			message.erase(message.begin(), message.begin() + sizeof(count));
			T value(message.begin(), message.begin() + (count - 1));

			// Round the count up to the nearest 4
			message.erase(message.begin(), message.begin() + ((count + 3) & ~3u));
			return value;
		} else if constexpr (Wl == WlType::Array) {
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit
			std::uint32_t count;
			std::memcpy(&count, message.data(), sizeof(count));
			message.erase(message.begin(), message.begin() + sizeof(count));
			T value(message.begin(), message.begin() + count);

			// Round the count up to the nearest 4
			message.erase(message.begin(), message.begin() + ((count + 3) & ~3u));
			return value;
		} else {
			static_assert(false, "Invalid WlType passed");
		}
	}

	// T is the GeneratedModule::Request type
	template<typename T>
	T deserialise_struct(std::vector<std::uint8_t> message) {
		// Strip the header from the message, we have no more use for it
		message.erase(message.begin(), message.begin() + sizeof(Header));

#ifndef __clang__
		static constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
		static constexpr auto wl_types = get_wl_types(fields);

		// The parameter pack (and hence, outer lambda) approach is required for aggregate intiialisation because
		// ... produces an expression, whereas template for produces a statement. For new readers, this is a templated lambda,
		// of which <std::size_t... n> is a non-type template parameter pack. Usually with parameter packs, they can vary in
		// type between the n, but this defines it as they must all be of size_t.
		return [this, &message]<std::size_t... N>(std::index_sequence<N...>) -> T {
			return T {
				this->deserialise_field<wl_types[N], typename[:std::meta::type_of(fields[N]):]>(message)...};
		}(std::make_index_sequence<fields.size()> {});
#endif
	}

	void parse_message(std::vector<std::uint8_t> message) {
		Header header;
		std::memcpy(&header, message.data(), sizeof(Header));
		std::println("Object ID: {}", header.object_id);

		Interface& object = *this->objects.at(header.object_id); // TODO sec

		std::visit([&](auto& interface) {
			// Get the actual type
			using T = std::decay_t<decltype(interface)>;

			// Check if the nested request type exists
			// If it does, we'll parse the args accordingly
			// If it doesn't, then there shouldn't be any args
			if constexpr (requires { typename T::Request; }) {
#ifndef __clang__
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
						return;
					}
				}
#endif
				std::println("No opcode matched: {}", header.opcode);
			}
		},
			object);
	}
};
}; // namespace mayquill
