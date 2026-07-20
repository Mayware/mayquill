#pragma once

// clang-format off
#define MQ_DEBUG(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Debug) mayquill::debug(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_INFO(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Info) mayquill::info(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_WARNING(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Warning) mayquill::warning(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_ERROR(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::error(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_ERRNO(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::errorno(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_SERROR(source, ...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::error(source, __VA_ARGS__); } while (0)
#define MQ_XERROR(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::xerror(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_XERRNO(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::xerrorno(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_SXERROR(source, ...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::xerror(source, __VA_ARGS__); } while (0)

// S prefix:
//          S means Source. It takes the source information, rather than taking the source at that line
// X prefix:
//          X means exception. It throws an exception.
