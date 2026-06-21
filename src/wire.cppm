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

struct Header {
	std::uint32_t object_id;
	std::uint16_t opcode;
	std::uint16_t size;
};

struct Client {
	std::uint64_t id;

	int fd;
	std::unordered_map<std::uint32_t, Interface> objects;

	bool reading_header = true;
	std::int32_t bytes_needed = HEADER_SIZE;
	std::vector<std::uint8_t> message;
	std::size_t read_offset = 0;

	template<typename T>
	T deserialise_field(WlType wl_type) {
		switch (wl_type) {
		case WlType::Int:
		case WlType::Uint:
		case WlType::Object:
		case WlType::NewId:
		case WlType::Enum: {
			std::uint32_t raw;
			std::memcpy(&raw, this->message.data() + sizeof(Header) + this->read_offset, sizeof(raw));
			this->read_offset += sizeof(raw);
			T value;
			assert(sizeof(std::uint32_t) == sizeof(T));
			std::memcpy(&value, &raw, sizeof(raw));
			return value;
		}

		case WlType::Fixed: {
			assert(false && "Fixed handling not implemented");
		}

		case WlType::Fd: {
			assert(false && "Fd handling not implemented");
		}

		case WlType::String: {
			assert(false && "String handling not implemented");
		}

		case WlType::Array: {
			assert(false && "Array handling not implemented");
		}
		}
		throw std::invalid_argument("Invalid WlType");
	}

	template<typename T>
	T deserialise_struct() {
		this->read_offset = 0;
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

		// The parameter pack (and hence, outer lambda) approach is required for aggregate intiialisation because
		// ... produces an expression, whereas template for produces a statement. For new readers, this is a templated lambda,
		// of which <std::size_t... n> is a non-type template parameter pack. Usually with parameter packs, they can vary in
		// type between the n, but this defines it as they must all be of size_t.
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
						auto alternative = deserialise_struct<Alternative>();
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
		for (auto& client : this->clients) {
			std::uint8_t buffer[64]; // (Remember, this is bytes)

			while (true) {
				std::uint16_t to_read = std::min<std::uint16_t>(client.bytes_needed, sizeof(buffer));
				int bytes_read = recv(client.fd, &buffer[0], to_read, 0);

				if (bytes_read == 0) {
					// Client disconnected, cleanup
					std::println("Client disconnected");
					this->remove_client(client.fd);
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
					std::println("No more bytes need to be read, target reached");
					if (client.reading_header) {
						std::println("Reading header");
						// Read the Message size (so 6 bytes in), to get the opcode + argument size
						// I would like to be explictly be 0 copy, but i can't find how to do such without UB
						std::uint16_t payload_size;
						std::memcpy(&payload_size, client.message.data() + 6, sizeof(std::uint16_t));
						client.bytes_needed = payload_size + client.bytes_needed; // bytes_needed may be negative, if we read more than we needed, hence we add it on
						// Discount the header size from the total message size, as payload_size includes the header size, and we've already read that
						client.bytes_needed -= HEADER_SIZE;
						goto parse_message; // I will change the logic later, uncry your tears
					} else {
					parse_message:
						std::println("Parsing message");
						client.parse_message();
						client.message.clear();
						client.bytes_needed = HEADER_SIZE;
					}
					client.reading_header = !client.reading_header;
				}
			}
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
