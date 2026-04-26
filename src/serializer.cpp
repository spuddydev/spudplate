#include "spudplate/serializer.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace spudplate {

DeserializeError::DeserializeError(std::string message, std::string json_pointer,
                                   std::optional<int> line,
                                   std::optional<int> column)
    : std::runtime_error(message),
      message_(std::move(message)),
      json_pointer_(std::move(json_pointer)),
      line_(line),
      column_(column) {}

nlohmann::json program_to_json(const Program& /*program*/) {
    throw std::logic_error("program_to_json: not implemented yet");
}

Program program_from_json(const nlohmann::json& /*root*/) {
    throw std::logic_error("program_from_json: not implemented yet");
}

bool programs_equal(const Program& /*a*/, const Program& /*b*/) {
    throw std::logic_error("programs_equal: not implemented yet");
}

}  // namespace spudplate
