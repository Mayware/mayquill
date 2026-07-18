module;
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
export module mayquill:serialiser;
import std;
import :definitions;

export namespace mayquill {
ssize_t send_message(
	int fd,
	const std::vector<std::uint8_t>& data) {
	return send(fd, data.data(), data.size(), 0);
}

ssize_t send_message(
	int fd,
	const std::vector<std::uint8_t>& data,
	const std::vector<int>& fds) {
	const std::size_t fd_bytes = sizeof(int) * fds.size();
	alignas(cmsghdr) char control_buffer[CMSG_SPACE(fd_bytes)];

	iovec io_vector {
		.iov_base = const_cast<std::uint8_t*>(data.data()),
		.iov_len = data.size(),
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
		fds.data(),
		fd_bytes);

	return sendmsg(fd, &message_header, 0);
}

template<WlType Wl, typename T>
void serialise_field(std::vector<std::uint8_t>& message, std::vector<int>& fds, const T& value) {
	if constexpr (
		Wl == WlType::Int ||
		Wl == WlType::Uint ||
		Wl == WlType::Object ||
		Wl == WlType::NewId ||
		Wl == WlType::Enum) {
		const std::size_t old_size = message.size();
		message.resize(old_size + sizeof(value));
		std::memcpy(message.data() + old_size, &value, sizeof(value));

	} else if constexpr (Wl == WlType::Fixed) {
		const std::int32_t raw = static_cast<std::int32_t>(value * 256.0);
		const std::size_t old_size = message.size();
		message.resize(old_size + sizeof(raw));
		std::memcpy(message.data() + old_size, &raw, sizeof(raw));

	} else if constexpr (Wl == WlType::Fd) {
		fds.push_back(value);

	} else if constexpr (Wl == WlType::String) {
		// + 1 For null term
		const std::uint32_t count = static_cast<std::uint32_t>(value.size() + 1);
		const std::size_t old_size = message.size();
		const std::size_t padded_count = (count + 3u) & ~3u; // Pad to next multiple of 4

		// Account for prefix
		message.resize(old_size + sizeof(std::uint32_t) + padded_count);
		std::memcpy(message.data() + old_size, &count, sizeof(count));								// Copy prefix
		std::memcpy(message.data() + old_size + sizeof(std::uint32_t), value.data(), value.size()); // Copy string

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

#ifdef MAYQUILL_ICE
template<std::meta::info Fn, std::uint16_t Opcode, typename... Args>
void serialise(int fd, std::uint32_t object_id, const Args&... args) {
	static constexpr auto parameters = std::define_static_array(std::meta::parameters_of(Fn));
	static constexpr auto wl_types = get_wl_types(parameters);

	std::vector<std::uint8_t> message(sizeof(Header));
	std::vector<int> fds;

	template for (constexpr auto i : std::views::iota(0uz, wl_types.size())) {
		serialise_field<wl_types[i]>(
			message,
			fds,
			args...[i]);
	}

	const Header header {
		.object_id = object_id,
		.opcode = Opcode,
		.size = static_cast<std::uint16_t>(message.size()),
	};

	std::memcpy(
		message.data(),
		&header,
		sizeof(header));

	if (fds.empty()) {
		send_message(fd, message);
	} else {
		send_message(fd, message, fds);
	}
}
#endif

} // namespace mayquill
