import mayquill;
import std;

using namespace mayquill;
int main() {
#ifndef MAYQUILL_ICE
    std::println("Running non-ICE build! This is incorrect! Exiting!");
    std::exit(1);
#endif
	Server server;
	server.bind_socket();

	while (true) {
        server.accept_clients();
        server.try_listen();
	}
}
