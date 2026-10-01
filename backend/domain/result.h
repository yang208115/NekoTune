#pragma once
#include <QString>
#include <optional>
#include <utility>
namespace nekotune {
enum class ErrorCode { InvalidArgument, NotFound, Storage, Io, Cancelled, Unavailable };
struct AppError {
    ErrorCode code = ErrorCode::Unavailable;
    QString message;
};
template <class T> class Result {
  public:
    Result(T value) : m_value(std::move(value)) {}
    Result(AppError error) : m_error(std::move(error)) {}
    explicit operator bool() const { return m_value.has_value(); }
    const T &value() const { return m_value.value(); }
    T &value() { return m_value.value(); }
    const AppError &error() const { return m_error; }

  private:
    std::optional<T> m_value;
    AppError m_error;
};
template <> class Result<void> {
  public:
    Result() = default;
    Result(AppError error) : m_error(std::move(error)) {}
    explicit operator bool() const { return !m_error.has_value(); }
    const AppError &error() const { return m_error.value(); }

  private:
    std::optional<AppError> m_error;
};
inline AppError failure(QString message, ErrorCode code = ErrorCode::InvalidArgument) {
    return {code, std::move(message)};
}
} // namespace nekotune
