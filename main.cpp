import std;
import parser;

// // Prepends a prefix (shocked emoji) to the start of each line
// std::string prepend(std::string target, const std::string& prefix) {
// 	target.insert(0, prefix);

// 	size_t pos = target.find('\n');

// 	while (pos != std::string::npos) {
// 		// Don't insert it after the last newline
// 		if (pos + 1 < target.size()) {
// 			target.insert(pos + 1, prefix);
// 		}

// 		pos = target.find('\n', pos + prefix.size() + 1);
// 	}

// 	return target;
// }

// // Trims all leading spaces, tabs from the start of each line.
// std::string trim(std::string target) {
// 	size_t start = 0;

// 	while (start < target.size()) {
// 		auto end = start;

// 		while (end < target.size() && (target[end] == ' ' || target[end] == '\t')) {
// 			++end;
// 		}
// 		target.erase(start, end - start);

// 		size_t newline_pos = target.find('\n', start);
// 		// We've done every line
// 		if (newline_pos == std::string::npos) {
// 			break;
// 		}
// 		start = newline_pos + 1;
// 	}
// 	return target;
// }

int main() {
    auto files = parser::get_parsed();
}
