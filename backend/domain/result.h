#pragma once
#include <QString>
#include <optional>
#include <utility>
namespace nekotune {
// Error codes describe application failure categories, not HTTP status.
// Messages may contain contextual details or translation keys.
// The IPC layer chooses how these failures become JSON responses.
// Keeping this type independent of transport enables service tests.
enum class ErrorCode { InvalidArgument, NotFound, Storage, Io, Cancelled, Unavailable };
struct AppError {
    ErrorCode code = ErrorCode::Unavailable;
    QString message;
};
/// A result contains either a value or an application failure.
/// Check its boolean state before calling value() or error().
/// A present value can itself be an empty optional or an empty list;
/// those are valid domain results rather than operation failures.
/// Value storage also supports move-only transaction/removal handles.
/// The default AppError member is irrelevant for successful results.
/// Do not infer failure by inspecting that member on a success path.
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
/// Operations without a payload represent success with no error.
/// Default construction therefore means successful completion.
/// The boolean meaning matches Result<T> despite inverted storage.
/// Access error() only after the result has evaluated to false.
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
