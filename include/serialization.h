#pragma once

#ifndef __SERIALIZATION_WRAP__

#include <filesystem>
#include <string>
#include <vector>

#include "common/export.h"
#include "common/archiver_wrapper.h"
#include "common/pointer.h"
#include "serialization_impl.h"
#include "util/multi_process_stream.h"
#include "util/registry.h"

namespace serialization
{
#define COMMA ,
#define SERIALIZATION_REGISTER_DERIVED_SERIALIZATION(type)                                  \
    SERIALIZATION_REGISTER_FUNCTION(                                                        \
        JsonSerializationRegistry, type, &register_serilizer_impl<json COMMA type>); \
    SERIALIZATION_REGISTER_FUNCTION(                                                        \
        BinarySerializationRegistry,                                                 \
        type,                                                                        \
        &register_serilizer_impl<serialization::multi_process_stream COMMA type>);

namespace serialization_impl
{
class SERIALIZATION_VISIBILITY access
{
public:
    //==========================
    // Binary
    //==========================
    template <typename T>
    static std::vector<unsigned char> binary_serialize(const ptr_const<T>& obj)
    {
        serialization::multi_process_stream buffer;
        serialization::serialization_save<serialization::multi_process_stream, ptr_const<T>>(buffer, obj);
        return buffer.GetRawData();
    };

    template <typename T>
    static auto binary_deserialize(const std::vector<unsigned char>& buffer_ref)
    {
        serialization::multi_process_stream buffer;
        buffer.SetRawData(buffer_ref);
        ptr_const<T> ptr_t;
        serialization::serialization_load<serialization::multi_process_stream, ptr_const<T>>(buffer, ptr_t);
        return ptr_t;
    };

    SERIALIZATION_API static void write_binary(
        const std::string& fn, const std::vector<unsigned char>& buffer);

    SERIALIZATION_API static void read_binary(
        const std::string& fn, std::vector<unsigned char>& buffer);

    template <typename T>
    static void write_to_binary(const std::string& fn, const ptr_const<T>& obj)
    {
        const std::vector<unsigned char>& buffer = binary_serialize(obj);
        write_binary(fn, buffer);
    }

    template <typename T>
    static ptr_const<T> read_from_binary(const std::string& path)
    {
        std::vector<unsigned char> buffer;
        read_binary(path, buffer);
        return binary_deserialize<T>(buffer);
    }

    //==========================
    // Json
    //==========================
    template <typename T>
    static std::string print(const ptr_const<T>& obj)
    {
        json value;
        serialization::serialization_save(value, obj);

        return value.dump(2);
    };

    template <typename T>
    static void json_serialize(json& value, const ptr_const<T>& obj)
    {
        auto& data = value["root"];
        serialization::serialization_save(data, obj);
    };

    template <typename T>
    static void json_deserialize(const json& value, ptr_const<T>& obj)
    {
        auto& data = const_cast<json&>(value["root"]);
        serialization::serialization_load(data, obj);
    };

    SERIALIZATION_API static void read_json(const std::string& path, json& root);

    SERIALIZATION_API static void write_json(const std::string& path, const json& root);

    template <typename T>
    static auto read_from_json(const std::string& path)
    {
        //SERIALIZATION_CHECK(std::filesystem::exists(path), "File does not exist: " + path);
        json root;
        read_json(path, root);
        ptr_const<T> obj;
        json_deserialize(root, obj);
        return obj;
    }

    template <typename T>
    static void write_to_json(const std::string& path, const ptr_const<T>& obj)
    {
        json root;
        json_serialize(root, obj);
        write_json(path, root);
    }
};  // access
}  // namespace serialization_impl
}  // namespace serialization
#endif
