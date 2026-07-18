module;
#include <cassert>
export module mayquill:client;
import std;
import :definitions;
import :interface;

export namespace mayquill {
class Client {
	friend class Server;

  private:
	std::vector<std::optional<Interface>> objects; // Index is the objectid, 0th index is wasted

	// Part of messages received
	std::vector<std::uint8_t> data;
	std::vector<int> fds;

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

#ifdef MAYQUILL_ICE
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

	void parse_message(std::vector<std::uint8_t> message); // Defined in impl

	Client(int fd) : fd(fd) {}

  public:
	int fd;

	template<typename T>
	void add_object(std::uint32_t id) {
		if (id >= objects.size()) {
			objects.resize(id + 1);
		}
		objects[id] = T {
			.client = this,
			.id = id};
	}
};
}; // namespace mayquill
