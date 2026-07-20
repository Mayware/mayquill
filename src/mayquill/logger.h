#pragma once

// clang-format off
#define MQ_DEBUG(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Debug) mayquill::debug(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_INFO(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Info) mayquill::info(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_WARNING(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Warning) mayquill::warning(mayquill::log::here(), __VA_ARGS__); } while (0)
#define MQ_ERROR(...) do { if constexpr (mayquill::log::level >= mayquill::log::LogLevel::Error) mayquill::error(mayquill::log::here(), __VA_ARGS__); } while (0)
