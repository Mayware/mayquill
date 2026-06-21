import mayquill;

using namespace mayquill;

int main() {
	Server server {};
	server.bind_socket();

	while (true) {
        server.accept_clients();
        server.try_listen();
	}
}
