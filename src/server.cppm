module;
#include <cassert>
#include <cerrno>
#include <mayquill/logger.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill:server;
export import :client;
import :logger;
import :wayland.wl_display;
import :definitions;

export namespace mayquill {
class Server {
  private:
	std::vector<std::unique_ptr<Client>> clients;

  public:
	int fd;

	void bind_socket() {
		std::string directory;
		{
			auto runtime = get_env("XDG_RUNTIME_DIR");
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
			MQ_XERRNO("Failed to open socket at {}", directory);
		}

		// Bind the socket to the address
		sockaddr_un address {.sun_family = AF_UNIX};
		strncpy(address.sun_path, directory.c_str(), sizeof(address.sun_path) - 1);

		// Reason to cast: https://stackoverflow.com/a/57431271
		if (bind(fd, (sockaddr*)&address, sizeof(address)) < 0) {
			MQ_XERRNO("Failed to bind to socket at {}", directory);
		}

		// SOMAXCONN is just as many as the kernel can handle
		if (listen(fd, SOMAXCONN) < 0) {
			MQ_XERRNO("Failed to listen to socket at {}", directory);
		}

		this->fd = fd;
		MQ_DEBUG("Bound socket");
	}

	void try_accept_clients() {
		// Loop until we've accepted all clients
		while (true) {
			int fd = accept4(this->fd, nullptr, nullptr, SOCK_NONBLOCK);
			if (fd == -1) {
				if (!(errno == EWOULDBLOCK)) {
					MQ_ERRNO("Failed to accept client");
				}

				// Nothing to accept, exit the loop
				return;
			}

			// New client accepted
			auto client = std::unique_ptr<Client>(new Client(*this, fd));
			// Add default display object
			client->add_object<WlDisplay, void>(1);
			Client* client_ptr = client.get();
			this->clients.push_back(std::move(client));
			client_ptr->handle_init();
			MQ_DEBUG("Accepted a new client");
		}
	}

	/* Message form:
	 *  Object Id | Opcode | Message Size (inc header) | Arguments then based on opcode
	 *  32 bit    | 16 bit | 16 bit                    | Message Size minus 64 bits
	 */
	void try_listen_requests() {
		std::vector<Client*> clients_to_destroy = {};

		for (auto& unique_client : this->clients) {
			auto& client = *unique_client;

			// Dont take inputs for clients due to disconnect
			if (client.disconnect_pending)
				continue;

			std::uint8_t buffer[128];

			while (true) {
				// Useful link: https://stackoverflow.com/questions/32593697/understanding-the-msghdr-structure-from-sys-socket-h
				// int bytes_read = recv(client.fd, &buffer[0], sizeof(buffer), 0);
				// c means control, Reserve enough space for the max amount of FDs we can read. CMSG_SPACE simply adds the header requriements, and padding; we provide it the payload size, so we get the total
				// We align as a cmsghdr, as we declare it as a char, but it will actually be interpreted as a cmsghdr, so it needs to follow the alignment requirements of it.
				alignas(cmsghdr) char control_buffer[128];
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
					MQ_DEBUG("Client disconnected");
					clients_to_destroy.push_back(&client);
					break;
				} else if (bytes_read == -1) {
					if (errno == EWOULDBLOCK) {
						break;
					} else {
						MQ_ERRNO("Failed to read from fd {}", client.fd);
						clients_to_destroy.push_back(&client);
						break;
					}
				}

				MQ_DEBUG("Read {} bytes", bytes_read);
				client.request_data.insert(client.request_data.end(), buffer, buffer + bytes_read);

				// PBUG: I have avoided macros in this, because I didn't understand how there could be alignment between the
				// cmsghdr and the payload, given the payload is just intgers. Hence, I have done it manually.
				// cmsgs (control headers) are roughly the header data -> padding -> payload data -> padding
				// The control messages are a packed list (uneven stride though, hence why we use cmsg_nexthdr, it readso the len from the control message)
				// Useful macro docs: https://man.archlinux.org/man/core/man-pages/CMSG_LEN.3
				for (cmsghdr* cmsg = CMSG_FIRSTHDR(&message_header); cmsg != nullptr; cmsg = CMSG_NXTHDR(&message_header, cmsg)) {
					// Check this cmsg will contain data about scm_rights, SOL_SOCKET is just the layer SCM_RIGHTS is on
					if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
						// The cmsg_len does NOT include the size of the trailing bytes (required for alignment), just the cmsghdr + payload
						// The CMSG_LEN macro gives you the size of the header + "alignment"
						std::size_t number_fds = (cmsg->cmsg_len - sizeof(cmsghdr)) / sizeof(int);
						// CMSG_DATA just gives the first byte of the payload, which should be a packed int array of the fd ids
						int* received_fds = reinterpret_cast<int*>(cmsg + 1); // Skip the header, pointer arithemtic will actually do cmsg + sizeof(cmsghdr) in human logic
						client.request_fds.insert(client.request_fds.end(), received_fds, received_fds + number_fds);
						MQ_DEBUG("Received {} fds", number_fds);
					}
				}

                // Notice how we just read the FDs up above, but, only now we check if the ancillary data fit and wasn't truncated
                // The above logic should work for fds that were receieved still (it just hits nullptr earlier), so that still gets added
                // to the request_fds. If it didn't fit, we now error, so that will cleaned up any fds that did fit in the truncated buffer
                // that we are obligated to close (so we dont leak)
				if (message_header.msg_flags & MSG_CTRUNC) {
					client.error(1, WlDisplay::ErrorEnum::Implementation, "Control message was truncated, couldn't fit into buffer");
					break;
				}

				// If the header exists, try to parse it
				while (client.request_data.size() >= 8) {
					std::uint16_t message_size;
					std::memcpy(&message_size, client.request_data.data() + 6, sizeof(std::uint16_t));

					// Arbitrary message_size check, it of course needs to be atleast as large as the header (because it's part of the header)
					// prevents looping if they say message_size is 0 (it can't be)
					if (message_size < sizeof(Header) || message_size % 4 != 0) {
						client.error(1,
							WlDisplay::ErrorEnum::InvalidMethod,
							std::format("Message size {} is invalid, it is either smaller than the header (8 bytes), "
										"or not aligned to a 4 byte boundary (all argument sizes are multiples of 4)",
								message_size));
						break;
					}

					// The message has been fully received
					if (client.request_data.size() >= message_size) {
						// TODO, find an elegant move, this is copy
						std::vector<std::uint8_t> message(client.request_data.begin(), client.request_data.begin() + message_size);
						client.request_data.erase(client.request_data.begin(), client.request_data.begin() + message_size);
						client.process_request(std::move(message));

						// If that previous call errors, make sure we don't potentially loop around again
						if (client.disconnect_pending) {
							break;
						}
					} else {
						break;
					}
				}

				// Incase we do a client.error whilst parsing the inner while
				if (client.disconnect_pending)
					break;
			}
		}

		for (auto client : clients_to_destroy) {
			client->destroy();
		}
	}

	void try_flush_events() {
		std::vector<Client*> clients_to_destroy;

		for (auto& client : clients) {
			if (!client->flush_events()) {
				// Send failed, consider it permenantly disconnected (ie. the socket is just gone)
				// So don't set disconnect_pending, just destroy now as there's no hope for it
				clients_to_destroy.push_back(client.get());
				continue;
			}

			// If the client wants to kill itself, do so after it has no more data to send
			// .error() sets disconnect_pending
			if (client->disconnect_pending && client->event_data.empty()) {
				clients_to_destroy.push_back(client.get());
			}
		}

		for (Client* client : clients_to_destroy) {
			client->destroy();
		}
	}

	void remove_client(Client* client) {
		for (auto it = this->clients.begin(); it != this->clients.end(); ++it) {
			if ((*it).get() == client) {
				this->clients.erase(it);
				return;
			}
		}
		MQ_XERROR("Tried to destroy a nonexistent client");
	}
};
} // namespace mayquill
