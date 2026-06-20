module;
#include <cassert>
#include <cerrno>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill;
import util;
import shared;

export import :generated;

using namespace shared;

constexpr std::size_t HEADER_SIZE = 8;

struct Header {
	std::uint32_t object_id;
	std::uint16_t size;
	std::uint16_t opcode;
};

struct Client {
	std::uint64_t id;

	int fd;
	std::unordered_map<std::uint32_t, Interface> objects;

	bool reading_header = true;
	std::uint16_t bytes_needed = HEADER_SIZE;
	std::vector<std::uint8_t> message;

	template<typename T>
	T deserialise_field(WlType wl_type) {
		return T {};
	}

	template<typename T>
	T deserialise_struct() {
		std::vector<std::uint8_t> argument_data(
			this->message.begin() + sizeof(Header),
			this->message.end());
#ifndef __clang__
		static constexpr auto fields = std::define_static_array(
			std::meta::nonstatic_data_members_of(^^T, std::meta::access_context::unchecked()));

        // Precompute the WlType tags in another lambda, rather directly in the Return T lambda / code part.
        // This is required as when we previously did it there, the lambda would be promoted to consteval,
        // since reflection was used, but then client (ie. this, runtime data) was required, thus violating consteval.
        // Instead, we compute wl types in its own consteval lambda, which is then static and so can be read by the runtime lambda.
        // I've left the previous incorrect approach at the bottom, for the sake of learning.
		static constexpr auto wl_types = []() {
			std::array<WlType, fields.size()> array {};
			template for (constexpr auto i : std::views::iota(0uz, fields.size())) {
				array[i] = std::meta::extract<WlType>(std::meta::annotations_of(fields[i])[0]);
			}
			return array;
		}();

		return [this]<std::size_t... n>(std::index_sequence<n...>) -> T {
			return T {
				this->deserialise_field<typename[:std::meta::type_of(fields[n]):]>(wl_types[n])...};
		}(std::make_index_sequence<fields.size()> {});
#endif

        // Previous incorect approach, deserialise field is runtime data (since it requires client)
		// return [this]<std::size_t... n>(std::index_sequence<n...>) -> T {
		// 	return T {
		// 		[this]() -> typename[:std::meta::type_of(fields[n]):] {
		// 			constexpr auto field = fields[n];
		// 			constexpr WlType wl_type = std::meta::extract<WlType>(std::meta::annotations_of(field)[0]);
		// 			return this->deserialise_field<[:std::meta::type_of(field):]>(wl_type);
		// 		}()...};
		// }(std::make_index_sequence<fields.size()> {});

	}

	void parse_message() {
		Header header;
		std::memcpy(&header, this->message.data(), sizeof(Header));

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
						deserialise_struct<Alternative>();
						break;
					}
				}
#endif
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
	static Server& get() {
		static Server instance;
		return instance;
	}

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
			this->clients.push_back(Client {
				.id = this->next_id.next(),
				.fd = fd,
			});
		}
	}

	/* Message form:
	 *  Object Id | Message Size (including header) | Opcode | Arguments then based on opcode
	 *  32 bit    | 16 bit                          | 16 bit | Message Size - 64 bits
	 */
	void try_listen() {
		for (auto& client : this->clients) {
			std::uint8_t buffer[64]; // (Remember, this is bytes)

			while (true) {
				int bytes_read = recv(client.fd, &buffer[0], client.bytes_needed, 0);

				if (bytes_read == 0) {
					// Client disconnected, cleanup
					this->remove_client(client.fd);
					break;
				} else if (bytes_read == -1) {
					if (errno == EWOULDBLOCK) {
						break;
					} else {
						throw std::runtime_error(std::format("Failed to read from fd {}, {}", fd, std::strerror(errno)));
						// potentially clean up client before this, also don't throw
					}
				}
				client.bytes_needed -= bytes_read;
				client.message.insert(client.message.end(), buffer, buffer + bytes_read);

				/*
				 * The listener works in two modes - reading the header, or the rest of the body
				 * It is in header mode, until 48 bits (6 bytes) are read. At that point, it
				 * reads how many bytes the entire message is from the last 2 bytes, and then
				 * swaps to reading the rest of the body. Once those target bytes are met, the
				 * message should be complete, so it can be parsed and it can all restart.
				 * Of course, this is per client
				 */
				if (client.bytes_needed == 0) {
					if (client.reading_header) {
						assert(client.message.size() == HEADER_SIZE && "message header should only be 48 bits");
						// Read the Message size (so 4 bytes in), to get the opcode + argument size
						// I would like to be explictly be 0 copy, but i can't find how to do such without UB
						std::memcpy(&client.bytes_needed, client.message.data() + 4, sizeof(std::uint16_t));
						// Discount the header size from the total message size
						client.bytes_needed -= HEADER_SIZE;
					} else {
						client.bytes_needed = HEADER_SIZE;
						// Handle the message
					}
					client.reading_header = !client.reading_header;
				}
			}
		}
	}

	void remove_client(int fd) {}
};
