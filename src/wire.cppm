module;
#include <cassert>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill;
import util;
import shared;
import wayland.wl_display;

export import :generated;

using namespace shared;

export namespace mayquill {
constexpr std::size_t HEADER_SIZE = 8;
constexpr std::size_t MAX_FDS = 6;

struct Header {
	std::uint32_t object_id;
	std::uint16_t opcode;
	std::uint16_t size;
};

struct Client {
	std::uint64_t id;

	int fd;
	std::unordered_map<std::uint32_t, Interface> objects;
	std::vector<std::uint8_t> data;
	std::vector<int> fds;

	template<typename T>
	T deserialise_field(WlType wl_type, std::vector<std::uint8_t>& message) {
		switch (wl_type) {
		case WlType::Int:
		case WlType::Uint:
		case WlType::Object:
		case WlType::NewId:
		case WlType::Enum: {
			T value;
			std::memcpy(&value, message.data(), sizeof(value));
			message.erase(message.begin(), message.begin() + sizeof(value));
			return value;
		}

		case WlType::Fixed: {
			std::int32_t raw;
			std::memcpy(&raw, message.data(), sizeof(raw));
			message.erase(message.begin(), message.begin() + sizeof(raw));

			// Divide by 2^8, to get the binary point leftwards 8 places
			T value = static_cast<T>(raw) / 256.0;
			return value;
		}

		case WlType::Fd: {
			assert(!this->fds.empty() && "Expected an FD, but vector was empty");
			T value;
			std::memcpy(&value, &fds.front(), sizeof(T));
			this->fds.erase(this->fds.begin());
			// No need to consume byte stream, as FDs are purely ancillary data
			// This also prevented us from having a separate Vec<Messages>, because
			// with no byte marker, we're unable to deliminate between which message
			// an FD belongs to
			return value;
		}

		case WlType::String: {
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit, plus a null terminator at the end (included in byte count)
			std::uint32_t count;
			std::memcpy(&count, message.data(), sizeof(count));
			message.erase(message.begin(), message.begin() + sizeof(count));
			T value(message.begin(), message.begin() + (count - 1));

			// Round the count up to the nearest 4
			message.erase(message.begin(), message.begin() + ((count + 3) & ~3u));
			return value;
		}

		case WlType::Array: {
			// 32 bit prefix, then n bytes (not including the prefix) padded to 32 bit
			std::uint32_t count;
			std::memcpy(&count, message.data(), sizeof(count));
			message.erase(message.begin(), message.begin() + sizeof(count));
			T value(message.begin(), message.begin() + count);

			// Round the count up to the nearest 4
			message.erase(message.begin(), message.begin() + ((count + 3) & ~3u));
			return value;
		}
		}
		throw std::invalid_argument("Invalid WlType");
	}

	// T is the GeneratedModule::Request type
	template<typename T>
	T deserialise_struct(std::vector<std::uint8_t> message) {
		// Strip the header from the message, we have no more use for it
		message.erase(message.begin(), message.begin() + sizeof(Header));

#ifndef __clang__
		static constexpr auto fields = std::define_static_array(
			std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));

		// Precompute the WlType tags in another lambda, rather directly in the Return T lambda / code part.
		// We need to do this in a separate lambda (i.e. this), because extract must be consteval, which is not what our next lambda is.
		static constexpr auto wl_types = []() {
			std::array<WlType, fields.size()> array {};
			template for (constexpr auto i : std::views::iota(0uz, fields.size())) {
				array[i] = std::meta::extract<WlType>(std::meta::annotations_of(fields[i])[0]);
			}
			return array;
		}();

		// The parameter pack (and hence, outer lambda) approach is required for aggregate intiialisation because
		// ... produces an expression, whereas template for produces a statement. For new readers, this is a templated lambda,
		// of which <std::size_t... n> is a non-type template parameter pack. Usually with parameter packs, they can vary in
		// type between the n, but this defines it as they must all be of size_t.
		return [this, &message]<std::size_t... n>(std::index_sequence<n...>) -> T {
			return T {
				this->deserialise_field<typename[:std::meta::type_of(fields[n]):]>(wl_types[n], message)...};
		}(std::make_index_sequence<fields.size()> {});
#endif
	}

	void parse_message(std::vector<std::uint8_t> message) {
		Header header;
		std::memcpy(&header, message.data(), sizeof(Header));
		std::println("Object ID: {}", header.object_id);

		Interface& object = this->objects.at(header.object_id);

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

class Server {

  private:
	int fd;
	util::IncCounter next_id;
	std::vector<Client> clients;

  public:
	void bind_socket() {
		std::string directory;
		{
			auto runtime = util::get_env("XDG_RUNTIME_DIR");
			directory = runtime ? *runtime : "/run/user/1000";
		}

		// Check for the next free /run/user/uid/wayland-i
		for (int i = 0; i < 9; i++) {
			auto current_directory = std::format("{}/wayland-{}", directory, i);
			if (!std::filesystem::exists(current_directory)) {
				directory = current_directory;
				break;
			}
		}

		// Allocate the socket, set it to be non blocking
		int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0);
		if (fd < 0) {
			throw std::runtime_error(std::format("Failed to open socket at {}", directory));
		}

		// Bind the socket to the address
		sockaddr_un address {.sun_family = AF_UNIX};
		strncpy(address.sun_path, directory.c_str(), sizeof(address.sun_path) - 1);

		// Reason to cast: https://stackoverflow.com/a/57431271
		if (bind(fd, (sockaddr*)&address, sizeof(address)) < 0) {
			throw std::runtime_error(std::format("Failed to bind to socket at {}, {}", directory, std::strerror(errno)));
		}

		// SOMAXCONN is just as many as the kernel can handle
		if (listen(fd, SOMAXCONN) < 0) {
			throw std::runtime_error(
				std::format("Failed to listen to socket at {}, {}", directory, std::strerror(errno)));
		}

		this->fd = fd;
		std::println("Bound socket");
	}

	void accept_clients() {
		// Loop until we've accepted all clients
		while (true) {
			int fd = accept(this->fd, nullptr, nullptr);
			if (fd == -1) {
				if (!(errno == EWOULDBLOCK)) {
					throw std::runtime_error(std::format("Failed to accept client: {}", std::strerror(errno)));
				}

				// Nothing to accept, exit the loop
				return;
			}

			// New client accepted
			auto client = Client {
				.id = this->next_id.next(),
				.fd = fd,
			};

			// Add default display object
			client.objects.emplace(1, WlDisplay {
										  .object_id = 1,
										  .client_id = 1, // TODO this is wrong
									  });

			this->clients.push_back(client);
			std::println("Accepted a new client");
		}
	}

	/* Message form:
	 *  Object Id | Opcode | Message Size (inc header) | Arguments then based on opcode
	 *  32 bit    | 16 bit | 16 bit                    | Message Size minus 64 bits
	 */
	void try_listen() {
		std::vector<int> disconnected_fds = {};

		for (auto& client : this->clients) {
			std::uint8_t buffer[128]; // (Remember, this is bytes)

			while (true) {
				// int bytes_read = recv(client.fd, &buffer[0], sizeof(buffer), 0);
				alignas(msghdr) char control_buffer[CMSG_SPACE(sizeof(int) * MAX_FDS)] = {};
				iovec io_vector = {
					.iov_base = buffer,
					.iov_len = sizeof(buffer),
				};
				msghdr message_header {
					.msg_iov = &io_vector,
					.msg_iovlen = 1,
					.msg_control = control_buffer,
					.msg_controllen = sizeof(control_buffer),
				};
				int bytes_read = recvmsg(client.fd, &message_header, 0);

				if (bytes_read == 0) {
					// Client disconnected, cleanup
					std::println("Client disconnected");
					disconnected_fds.push_back(client.fd);
					break;
				} else if (bytes_read == -1) {
					if (errno == EWOULDBLOCK) {
						break;
					} else {
						throw std::runtime_error(std::format("Failed to read from fd {}, {}", client.fd, std::strerror(errno)));
						// potentially clean up client before this, also don't throw
					}
				}

				std::println("Read {} bytes", bytes_read);
				client.data.insert(client.data.end(), buffer, buffer + bytes_read);

				// The control messages are a packed list (uneven stride though, hence why we use cmsg_nexthdr, it reads the len from the control message)
				// https://docs.huihoo.com/doxygen/linux/kernel/3.7/structcmsghdr.html, after that definition, it's the arbitrary payload
				for (cmsghdr* control_message = CMSG_FIRSTHDR(&message_header);
					control_message != nullptr;
					control_message = CMSG_NXTHDR(&message_header, control_message)) {
					if (control_message->cmsg_level == SOL_SOCKET && control_message->cmsg_type == SCM_RIGHTS) {
						std::size_t number_fds = (control_message->cmsg_len - CMSG_LEN(0)) / sizeof(int);
                        // CMSG_DATA just gives the first byte of the payload, which should be a packed int array of the fd ids
						int* received_fds = reinterpret_cast<int*>(CMSG_DATA(control_message));
						client.fds.insert(client.fds.end(), received_fds, received_fds + number_fds);
						std::println("Received {} fds", number_fds);
					}
				}

				// If the header exists, try to parse it
				while (client.data.size() >= 8) {
					std::uint16_t message_size;
					std::memcpy(&message_size, client.data.data() + 6, sizeof(std::uint16_t));

					// The message has been fully received
					if (client.data.size() >= message_size) {
						// TODO, find an elegant move, this is copy
						std::vector<std::uint8_t> message(client.data.begin(), client.data.begin() + message_size);
						client.data.erase(client.data.begin(), client.data.begin() + message_size);
						client.parse_message(std::move(message));
					} else {
						break;
					}
				}
			}
		}

		for (auto fd : disconnected_fds) {
			this->remove_client(fd);
		}
	}

	void remove_client(int fd) {
		for (auto it = this->clients.begin(); it != this->clients.end(); ++it) {
			if (it->fd == fd) {
				this->clients.erase(it);
				break;
			}
		}
	}
};
} // namespace mayquill
