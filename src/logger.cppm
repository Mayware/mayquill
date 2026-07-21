module;
#include <cerrno>
export module mayquill:logger;
import std;

// Previously this was all in logger.h, but clangd was throwing out random invalid errors (compile passed fine, i assume a clangd bug).
// Splitting it into its own module resolved this, albeit to use logger.h you must ensure :logger is imported. Albeit, it's probably cleaner to do it like this.

// Default log level is Debug
#ifndef MAYQUILL_LOG_LEVEL
#define MAYQUILL_LOG_LEVEL Debug
#endif

export namespace mayquill {
namespace log {
enum class LogLevel {
	Error,
	Warn,
	Info,
	Debug,
};

template<LogLevel Level>
constexpr std::string_view stringify_tag() {
	if constexpr (Level == LogLevel::Error) {
		return "\033[31m[Error]\033[0m";
	} else if constexpr (Level == LogLevel::Warn) {
		return "\033[33m[Warn]\033[0m";
	} else if constexpr (Level == LogLevel::Info) {
		return "\033[36m[Info]\033[0m";
	} else if constexpr (Level == LogLevel::Debug) {
		return "\033[35m[Debug]\033[0m";
	} else {
		static_assert(false, "Invalid LogLevel!");
	}
}

constexpr LogLevel level = LogLevel::MAYQUILL_LOG_LEVEL;

// So the header file doesn't need to include anything itself, part of the previous clangd bug
constexpr std::source_location here(std::source_location source = std::source_location::current()) {
	return source;
}

template<LogLevel Level, bool ShouldErrno, bool ShouldKill, typename... Args>
void log_impl(std::source_location source, std::format_string<Args...> format, Args&&... args) {
	std::string message = std::format(format, std::forward<Args>(args)...);
#ifdef MAYQUILL_ICE
	std::string final_message = std::format("{} [{}:{}] {}", stringify_tag<Level>(), std::filesystem::path(source.file_name()).filename().display_string(), source.line(), message);
	if constexpr (ShouldErrno) {
		final_message = std::format("{}: {}", final_message, std::strerror(errno));
	}
	if constexpr (ShouldKill) {
		throw std::runtime_error(final_message);
	}
	std::println("{}", final_message);
#endif
}
// Overloads the () operator on variables, so they can be used *like* functions
// They are not actually functions, just variables where you can use the () operator lmao
template<LogLevel Level, bool ShouldErrno, bool ShouldKill>
struct Generator {
	// && means either an lvalue or rvalue here, not just a temporary
	// https://old.reddit.com/r/cpp_questions/comments/1kngj5i/why_do_some_devs_use_for_variadic_template/msi31nv/
	template<typename... Args>
	void operator()(std::source_location source, std::format_string<Args...> format, Args&&... args) const {
		log_impl<Level, ShouldErrno, ShouldKill>(source, format, std::forward<Args>(args)...);
	}
};
} // namespace log

using namespace log;
constexpr Generator<LogLevel::Debug, false, false> debug {};
constexpr Generator<LogLevel::Info, false, false> info {};
constexpr Generator<LogLevel::Warn, false, false> warn {};
constexpr Generator<LogLevel::Error, false, false> error {};
constexpr Generator<LogLevel::Error, true, false> errorno {};
constexpr Generator<LogLevel::Error, false, true> xerror {};
constexpr Generator<LogLevel::Error, true, true> xerrorno {};

template<typename... Args>
std::string combo_errno(std::format_string<Args...> format, Args&&... args) {
	return std::format("{}: {}", std::format(format, std::forward<Args>(args)...), std::strerror(errno));
}
} // namespace mayquill
