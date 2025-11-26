#include "common/archiver_wrapper.h"

#include "util/export.h"
#include "util/registry.h"

namespace serialization
{
SERIALIZATION_API SERIALIZATION_DEFINE_FUNCTION_REGISTRY(
    JsonSerializationRegistry, json_serilaization_function_t);

SERIALIZATION_API SERIALIZATION_DEFINE_FUNCTION_REGISTRY(
    BinarySerializationRegistry, binary_serilaization_function_t);
}  // namespace serialization
