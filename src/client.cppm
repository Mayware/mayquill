module;
#include <cassert>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill:client;
import std;
import :definitions;
import :interface;

static constexpr std::uint32_t server_id_start = 0xff000000u;

export namespace mayquill {
class Server;

class Client {
	friend class Server;

  private:
	Server& server;
	std::unordered_map<std::uint32_t, Interface> objects; // Index is the objectid, 0th index is wasted

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
	T deserialise_field(std::vector<std::uint8_t>& message) {

		if constexpr (Wl == WlType::NullableObject) {
			std::uint32_t value;
			std::memcpy(&value, message.data(), sizeof(value));
			message.erase(message.begin(), message.begin() + sizeof(value));
			if (value == 0) {
				return std::nullopt;
			}
			return value;
		} else if constexpr (Wl == WlType::Int ||
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
			assert(!this->request_fds.empty() && "Expected an FD, but vector was empty");
			T value;
			std::memcpy(&value, &request_fds.front(), sizeof(T));
			this->request_fds.erase(this->request_fds.begin());
			// No need to consume byte stream, as FDs are purely ancillary data
			// This also prevented us from having a separate Vec<Messages>, because
			// with no byte marker, we're unable to deliminate between which message
			// an FD belongs to
			return value;
		} else if constexpr (Wl == WlType::String || Wl == WlType::NullableString) {
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit, plus a null terminator at the end (included in byte count)
			std::uint32_t count;
			std::memcpy(&count, message.data(), sizeof(count));
			message.erase(message.begin(), message.begin() + sizeof(count));

			if constexpr (Wl == WlType::NullableString) {
				if (count == 0) {
					return std::nullopt;
				}
			}

			std::string value(message.begin(), message.begin() + (count - 1));

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

	// destroy calls handle_destroy(), then tells the server to remove this client
	void destroy();
	void process_request(std::vector<std::uint8_t> message);
	// Configuration points
	void handle_destroy();
	void handle_init();

	std::uint32_t current_server_id = server_id_start;
	bool disconnect_pending = false;

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

	template<typename T>
	void add_object(std::uint32_t id) {
		objects.emplace(
			id,
			Interface {T {
				.client = *this,
				.id = id,
			}});
	}

	void remove_object(std::uint32_t id) {
		objects.erase(id);

		// Tell wl_display that they can reuse this id, if they allocated it
		if (id < server_id_start) {
			std::get<WlDisplay>(objects.at(1)).delete_id(id);
		}
	}

	std::uint32_t next_id() {
		return current_server_id++;
	}

	void disconnect_after_flushed() {
		disconnect_pending = true;
	}

#ifdef MAYQUILL_ICE
	template<std::meta::info Fn, std::uint16_t Opcode, typename... Args>
	void process_event(std::uint32_t object_id, const Args&... args) {
		static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(Fn));
		static constexpr auto wl_types = get_wl_types(parameters);

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
