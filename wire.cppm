module;
#include <cerrno>
#include <cstdint>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

export module Server;
import std;
import Util;

struct Client {
	uint64_t id;
};

namespace Wire {

/*
 *  Types:
 *  int / uint : 32 bits
 *  fixed point: 24 bits whole, 8 bits decimal
 *  object     : 32 bits
 *  new_id     : 32 bits
 *  string     : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits), \0 terminator
 *  array      : 32 bits integer length prefix, contents (n bits, padded to the nearest 32 bits
 *  fd         : 0 bits, stored in ancillary data of the message
 *  enum       : 32 bits integer
 */
enum class Types {
	Int,
	Uint,
	Fixed,
	Object,
	NewId,
	String,
	Fd,
	Enum
};

class Server {
  private:
	int fd;
	util::IncCounter next_id;
	// fd -> Client
	std::unordered_map<int, Client> clients;

  public:
	static Server &get() {
		static Server instance;
		return instance;
	}

	void bind_socket() {
		std::string directory;
		{
			auto runtime = util::get_env("XDG_RUNTIME_DIR");
			directory = runtime ? *runtime : "/run/user/1000";
		}

		// Check for the next free /run/user/uid/wayland-X
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
		sockaddr_un address{.sun_family = AF_UNIX};
		strncpy(address.sun_path, directory.c_str(), sizeof(address.sun_path) - 1);

		// Reason to cast: https://stackoverflow.com/a/57431271
		if (bind(fd, (sockaddr *)&address, sizeof(address)) < 0) {
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
				if (!(errno = EWOULDBLOCK)) {
					throw std::runtime_error(std::format("Failed to accept client: {}", std::strerror(errno)));
				}

				// Nothing to accept, exit the loop
				return;
			}

			// New client accepted
			this->clients[fd] = Client{
				.id = next_id.next()};
		}
	}

	/*
	 *  Message form:
	 *
	 *  Object Id | Message Size (including header) | Opcode | Arguments then based on opcode
	 *  32 bit    | 16 bit                          | 16 bit | Message Size - 64 bits
	 *
	 */
	void dispatch() {
		for (auto &[fd, client] : this->clients) {
			std::vector<uint8_t> message;
			uint8_t buffer[64];

			while (true) {
				int bytes_read = recv(fd, &buffer[0], sizeof(buffer), 0);
				if (bytes_read == 0) {
					// Client disconnected, cleanup
					this->remove_client(fd);
					break;
				} else if (bytes_read == -1) {
					if (errno == EWOULDBLOCK) {
						break;
					} else {
						throw std::runtime_error(std::format("Failed to read from fd {}, {}", fd, std::strerror(errno)));
						// potentially clean up client before this, also don't throw
					}
				}

				message.insert(message.end(), buffer, buffer + bytes_read);
			}
		}
	}

	void remove_client(int fd) {}
};
} // namespace Wire
