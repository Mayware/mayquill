import std;
import util;

int main() {
    std::cout << *util::get_env("XDG_RUNTIME_DIR") << std::endl;
}
