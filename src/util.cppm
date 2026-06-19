module;
export module util;
import std;

export namespace util {
std::optional<std::string> get_env(const std::string& name) {
	if (const char* value = std::getenv(name.c_str())) {
		return value;
	}

	return std::nullopt;
}

class IncCounter {
  private:
	std::uint64_t counter;

  public:
	std::uint64_t next() {
		return this->counter += 1;
	}
};
} // namespace util
