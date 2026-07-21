module;
#include "mayquill/logger.h"
#include <cassert>
#include <cerrno>
#include <exception>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill:client;
import std;
import :definitions;
import :interface;
import :logger;

#define FIRST_SERVER_ID 0xff000000u

export namespace mayquill {
class Server;

class Client {
	friend class Server;

  private:
	Server& server;
	// Index is the objectid, the 1st value is a unique id. 0th index is wasted
	std::unordered_map<std::uint32_t, std::tuple<std::uint32_t, Interface>> objects;

	// Part of outgoing messages (events)
	std::vector<std::uint8_t> event_data;
	std::vector<int> event_fds;

	// Part of messages received (requests)
	std::vector<std::uint8_t> request_data;
	std::vector<int> request_fds;

	Client(Server& server, int fd) : server(server), fd(fd) {}

	bool flush_events() {
		if (!event_data.empty()) {
			ssize_t bytes_sent;
			if (event_fds.empty()) {
				bytes_sent = send(fd, event_data.data(), event_data.size(), 0);
			} else {
				const std::size_t fd_bytes = sizeof(int) * event_fds.size();
				alignas(cmsghdr) char control_buffer[CMSG_SPACE(fd_bytes)];

				iovec io_vector {
					.iov_base = event_data.data(),
					.iov_len = event_data.size(),
				};

				msghdr message_header {
					.msg_iov = &io_vector,
					.msg_iovlen = 1,
					.msg_control = control_buffer,
					.msg_controllen = sizeof(control_buffer),
				};

				cmsghdr* cmsg = CMSG_FIRSTHDR(&message_header);

				cmsg->cmsg_level = SOL_SOCKET;
				cmsg->cmsg_type = SCM_RIGHTS;
				cmsg->cmsg_len = CMSG_LEN(fd_bytes);

				std::memcpy(
					CMSG_DATA(cmsg),
					event_fds.data(),
					fd_bytes);

				bytes_sent = sendmsg(fd, &message_header, 0);
			}
			if (bytes_sent == -1) {
				// EAGAIN means may succeed in future
				if (!(errno == EAGAIN || errno == EWOULDBLOCK)) {
					// Send failed, return false
					return false;
				}
			} else {
				for (auto fd : event_fds) {
					// Close our duplicated fd handle, since we've already sent it off
					close(fd);
				}
				event_fds.clear();
				event_data.erase(
					event_data.begin(),
					event_data.begin() + bytes_sent);
			}
		}
		return true;
	}

	template<WlType Wl, typename T>
	void serialise_field(std::vector<std::uint8_t>& message, std::vector<int>& fds, const T& value) {
		if constexpr (Wl == WlType::NullableObject) {
			const std::uint32_t raw = value.value_or(0);
			const auto old_size = message.size();
			message.resize(old_size + sizeof(raw));
			std::memcpy(message.data() + old_size, &raw, sizeof(raw));
		} else if constexpr (
			Wl == WlType::Int ||
			Wl == WlType::Uint ||
			Wl == WlType::Object ||
			Wl == WlType::NewId ||
			Wl == WlType::Enum ||
			Wl == WlType::NullableObject) {
			const auto old_size = message.size();
			message.resize(old_size + sizeof(value));
			std::memcpy(message.data() + old_size, &value, sizeof(value));

		} else if constexpr (Wl == WlType::Fixed) {
			const std::int32_t raw = static_cast<std::int32_t>(value * 256.0);
			const std::size_t old_size = message.size();
			message.resize(old_size + sizeof(raw));
			std::memcpy(message.data() + old_size, &raw, sizeof(raw));

		} else if constexpr (Wl == WlType::Fd) {
			// Duplicate the fd, to get a second handle
			// We do this, so if the caller closes their fd, we still have a valid fd to the file
			// I've seen people recommend close on exec, but we never exec, so i don't see a point
			auto dupe = dup(value);
			if (dupe == -1)
				throw std::runtime_error("Failed to dupe fd");
			fds.push_back(dupe);
		} else if constexpr (Wl == WlType::String || Wl == WlType::NullableString) {
			std::string_view raw;
			if constexpr (Wl == WlType::NullableString) {
				if (!value) {
					const std::uint32_t count = 0;
					const std::size_t old_size = message.size();

					message.resize(old_size + sizeof(count));
					std::memcpy(
						message.data() + old_size,
						&count,
						sizeof(count));
					return;
				}

				raw = *value;
			} else {
				raw = value;
			}
			// + 1 For null term
			const std::uint32_t count = static_cast<std::uint32_t>(raw.size() + 1);
			const std::size_t old_size = message.size();
			const std::size_t padded_count = (count + 3u) & ~3u; // Pad to next multiple of 4

			// Account for prefix
			message.resize(old_size + sizeof(std::uint32_t) + padded_count);
			std::memcpy(message.data() + old_size, &count, sizeof(count));							// Copy prefix
			std::memcpy(message.data() + old_size + sizeof(std::uint32_t), raw.data(), raw.size()); // Copy string

		} else if constexpr (Wl == WlType::Array) {
			const std::uint32_t count = static_cast<std::uint32_t>(value.size());
			const std::size_t old_size = message.size();
			const std::size_t padded_count = (count + 3u) & ~3u;
			message.resize(old_size + sizeof(std::uint32_t) + padded_count, 0);
			std::memcpy(message.data() + old_size, &count, sizeof(count));
			if (count != 0)
				std::memcpy(message.data() + old_size + sizeof(std::uint32_t), value.data(), count);

		} else {
			static_assert(false, "Invalid WlType passed");
		}
	}

	template<WlType Wl, typename T>
	T deserialise_field(std::span<const std::uint8_t>& message) {

		if constexpr (Wl == WlType::Int ||
					  Wl == WlType::Uint ||
					  Wl == WlType::Object ||
					  Wl == WlType::NullableObject ||
					  Wl == WlType::NewId ||
					  Wl == WlType::Enum) {
			auto bytes = advance(message, sizeof(std::uint32_t));
			std::uint32_t value;
			std::memcpy(&value, bytes.data(), sizeof(value));
			if constexpr (Wl == WlType::NewId) {
				if (this->objects.contains(value) || value == 0 || FIRST_SERVER_ID <= value) {
					// Technically they should be densly packed, but it doesn't affect us, and checking would be relatively expensive
					MQ_XERROR("Invalid new_id provided, it was either in range of the server ids, or already used");
				}
			} else if constexpr (Wl == WlType::NullableObject) {
				if (value == 0) {
					return std::nullopt;
				}
			}
			// Static cast done for the enum type
			return static_cast<T>(value);
		} else if constexpr (Wl == WlType::Fixed) {
			auto bytes = advance(message, sizeof(std::uint32_t));
			std::int32_t raw;
			std::memcpy(&raw, bytes.data(), sizeof(raw));
			// Divide by 2^8, to get the binary point leftwards 8 places
			T value = static_cast<T>(raw) / 256.0;
			return value;
		} else if constexpr (Wl == WlType::Fd) {
			if (this->request_fds.empty())
				MQ_XERROR("Expected an fd, but the vector was empty");
			T value;
			std::memcpy(&value, &request_fds.front(), sizeof(T));
			this->request_fds.erase(this->request_fds.begin());
			// No need to consume byte stream, as FDs are purely ancillary data
			// This also prevented us from having a separate Vec<Messages>, because
			// with no byte marker, we're unable to deliminate between which message
			// an FD belongs to
			return value;
		} else if constexpr (Wl == WlType::String || Wl == WlType::NullableString) {
			auto bytes = advance(message, sizeof(std::uint32_t));
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit, plus a null terminator at the end (included in byte count)
			std::uint32_t count;
			std::memcpy(&count, bytes.data(), sizeof(count));
			if (count == 0) {
				// Count of 0 is what represents the null in nullable strings
				if constexpr (Wl == WlType::NullableString) {
					return std::nullopt;
				} else {
					// However, non-nullable strings cannot have a count of 0, since they must always
					// have a trailing \0, to represent an empty string instead.
					MQ_XERROR("The string wasn't nullable, yet it had a count of 0");
				}
			}
			bytes = advance(message, count); // Consume the null terminator too
			// Ensure the last thing was actually a null term, so we can directly cnstruct a string
			if (bytes.back() != '\0')
				MQ_XERROR("String wasn't null terminated, quite cheeky, wonder what you were trying to pull off here");
			std::string value(bytes.begin(), bytes.end() - 1); // Ignore the null terminator
			// Round the count up to the nearest 4, then consume JUST the padding it took, since we already consumed count
			advance(message, ((count + 3) & ~3u) - count);
			return value;
		} else if constexpr (Wl == WlType::Array) {
			auto bytes = advance(message, sizeof(std::uint32_t));
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit
			std::uint32_t count;
			std::memcpy(&count, bytes.data(), sizeof(count));
			bytes = advance(message, count);
			T value(bytes.begin(), bytes.end());
			advance(message, ((count + 3) & ~3u) - count);
			return value;
		} else {
			static_assert(false, "Invalid WlType passed");
		}
	}

	// T is the GeneratedModule::Request type
	template<typename T>
	T deserialise_struct(std::span<const std::uint8_t> message) {
		// Strip the header from the message, we have no more use for it
		advance(message, sizeof(Header));

#ifdef MAYQUILL_ICE
		static constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
		static constexpr auto wl_types = get_wl_types(fields);

		// The parameter pack (and hence, outer lambda) approach is required for aggregate intiialisation because
		// ... produces an expression, whereas template for produces a statement. For new readers, this is a templated lambda,
		// of which <std::size_t... n> is a non-type template parameter pack. Usually with parameter packs, they can vary in
		// type between the n, but this defines it as they must all be of size_t.
		return [this, message]<std::size_t... N>(std::index_sequence<N...>) mutable -> T {
			return T {
				this->deserialise_field<wl_types[N], typename[:std::meta::type_of(fields[N]):]>(message)...};
		}(std::make_index_sequence<fields.size()> {});
		// There can be extra data potentially let for message, that wasn't consumed, but it should be harmless on our end, so we'll ignore it
#endif
	}

	std::span<const std::uint8_t> advance(std::span<const std::uint8_t>& message, std::size_t n) {
		if (message.size() < n) {
			MQ_XERROR("Message length was invalid to deserialise field");
		}
		auto left_behind = message.first(n); // Gets a subview of the first n bytes
		message = message.subspan(n);		 // Gets a subview of anything after n
		return left_behind;
	}

	WlDisplay& get_display() {
		return std::get<WlDisplay>(std::get<1>(objects.at(1)));
	}

	// destroy calls handle_destroy(), then tells the server to remove this client
	void destroy();
	void process_request(std::vector<std::uint8_t> message);
	// Configuration points
	void handle_destroy();
	void handle_init();

	std::uint32_t current_server_id = FIRST_SERVER_ID;
	bool disconnect_pending = false;

	template<WlType Wl, typename T>
	std::string log_field(const T& value) {
#ifdef MAYQUILL_ICE
		if constexpr (Wl == WlType::NullableObject || Wl == WlType::NullableString) {
			return value ? std::format("{}", *value) : "null";
		} else if constexpr (Wl == WlType::Array) {
			return std::format("[{} bytes]", value.size());
		} else if constexpr (Wl == WlType::Enum) {
			static constexpr auto enumerators = std::define_static_array(std::meta::enumerators_of(^^T));
			template for (constexpr auto i : std::views::iota(0uz, enumerators.size())) {
				if (value == std::meta::extract<T>(enumerators[i])) {
					return std::format("{}:{}", std::meta::identifier_of(enumerators[i]), std::to_underlying(value));
				}
			}
			return std::format("Unknown?:{}", std::to_underlying(value));
		} else {
			return std::format("{}", value);
		}
#endif
	}

	template<typename T>
	std::string log_wl_struct(const T& wl_struct, std::uint32_t id, std::uint32_t opcode) {
#ifdef MAYQUILL_ICE
		static constexpr auto fields = std::define_static_array(std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));
		static constexpr auto wl_types = get_wl_types(fields);
		std::string message = std::format("{}.{}[Id{}, Op{}] {{ ", std::meta::identifier_of(std::meta::parent_of(^^T)), std::meta::identifier_of(^^T), id, opcode);

		template for (constexpr auto i : std::views::iota(0uz, fields.size())) {
			message += std::format("{}{}={}", i == 0 ? "" : ", ", std::meta::identifier_of(fields[i]), log_field<wl_types[i]>(wl_struct.[:fields[i]:]));
		}
		return message + " }";
#endif
	}

#ifdef MAYQUILL_ICE
	template<std::meta::info Fn, std::uint16_t Opcode, typename... Args>
	std::string log_wl_function(std::uint32_t id, const Args&... args) {
		static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(Fn));
		static constexpr auto wl_types = get_wl_types(parameters);
		std::string message = std::format("{}.{}[Id{}, Op{}] {{ ", std::meta::identifier_of(std::meta::parent_of(Fn)), std::meta::identifier_of(Fn), id, Opcode);

		template for (constexpr auto i : std::views::iota(0uz, parameters.size())) {
			message += std::format("{}{}={}", i == 0 ? "" : ", ", std::meta::identifier_of(parameters[i]), log_field<wl_types[i]>(args...[i]));
		}
		return message + " }";
	}
#endif

  public:
	int fd;

	~Client() {
		close(fd);
		for (auto fd : request_fds) {
			close(fd);
		}
		for (auto fd : event_fds) {
			close(fd);
		}
	}

	template<typename T, typename D>
	std::pair<Key, T&> add_object(std::uint32_t id, std::source_location source = std::source_location::current()) {
		static std::uint32_t unique_count = 0;
		auto key = Key {.id = id, .unique = ++unique_count};
		auto [it, inserted] = objects.emplace(
			key.id,
			std::make_tuple(
				key.unique,
				Interface {T {
					.client = *this,
					.keyd = key,
					.user_data = {
						nullptr,
						[](void* user_data) {
							if constexpr (!std::is_same_v<D, void>) {
								delete static_cast<D*>(user_data);
							}
						},
					},
				}}));

		if (!inserted) {
			MQ_SXERROR(source, "Tried to insert an object {} that was already added", key.id);
		}
		MQ_DEBUG("Added object id {}", id);
		return {key, std::get<T>(std::get<1>(it->second))};
	}

	template<typename T>
	std::optional<T&> get_object(Key key, std::source_location source = std::source_location::current()) {
		auto it = objects.find(key.id);
		if (it == objects.end())
			return std::nullopt;
		auto& [unique, object] = it->second;
		if (unique == key.unique)
			return std::get<T>(object);
		else
			return std::nullopt;
	}

	void remove_object(Key key, std::source_location source = std::source_location::current()) {
		MQ_DEBUG("Removed object id {}", key.id);
		if constexpr (log::level < log::LogLevel::Error) {
			auto pair = objects.extract(key.id);
			if (pair.empty())
				MQ_SXERROR(source, "Tried to remove an object, which did not exist!");
			if (std::get<0>(pair.mapped()) != key.unique)
				MQ_SXERROR(source, "Tried to remove an object, but the key's unique value was not the same! {}", key.id);
		} else {
			objects.erase(key.id);
		}

		// Tell wl_display that they can reuse this id, if they allocated it
		// Obviously if we removed the display, don't emit that event because we can't
		if (key.id < FIRST_SERVER_ID && key.id != 1) {
			get_display().delete_id(key.id);
			MQ_DEBUG("Told client it can reuse id {}", key.id);
		}
	}

	template<typename E>
		requires std::is_enum_v<E>
	void error(
		std::uint32_t object_id,
		E code,
		std::string message,
		std::source_location source = std::source_location::current()) {

		MQ_SERROR(source, "{}", message);
		get_display().error(object_id, static_cast<std::uint32_t>(code), message);

		disconnect_pending = true;
	}

	std::uint32_t next_id() {
		return current_server_id++;
	}

#ifdef MAYQUILL_ICE
	template<std::meta::info Fn, std::uint16_t Opcode, typename... Args>
	void process_event(std::uint32_t object_id, const Args&... args) {
		static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(Fn));
		static constexpr auto wl_types = get_wl_types(parameters);
		MQ_DEBUG("Event {}", log_wl_function<Fn, Opcode>(object_id, args...));

		auto offset = event_data.size();
		event_data.resize(offset + sizeof(Header)); // Add reserved space for the header

		template for (constexpr auto i : std::views::iota(0uz, wl_types.size())) {
			serialise_field<wl_types[i]>(
				event_data,
				event_fds,
				args...[i]);
		}

		// Rewrite the header reserved space, now we know the size
		const Header header {
			.object_id = object_id,
			.opcode = Opcode,
			.size = static_cast<std::uint16_t>(event_data.size() - offset),
		};

		std::memcpy(
			event_data.data() + offset,
			&header,
			sizeof(header));
	}
#endif
};
}; // namespace mayquill
