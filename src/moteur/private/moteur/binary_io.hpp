#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace moteur {

// Little-endian binary writing and reading for the engine's prepared formats (milestone 9, part 4).
// The reader checks every size against what is left: a damaged or hostile file throws
// std::runtime_error, never reads out of bounds (the formats are fuzzed).

class BinaryWriter {
public:
    std::vector<std::uint8_t>& bytes() { return bytes_; }

    void raw(const void* data, std::size_t size) {
        const auto* p = static_cast<const std::uint8_t*>(data);
        bytes_.insert(bytes_.end(), p, p + size);
    }
    template <typename T>
        requires std::is_arithmetic_v<T>
    void value(T v) {
        // The engine's platforms are little-endian (x86-64, Apple Silicon): stored as in memory.
        raw(&v, sizeof(T));
    }
    void u32(std::uint32_t v) { value(v); }
    void f32(float v) { value(v); }
    void text(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        raw(s.data(), s.size());
    }
    void vec2(const glm::vec2& v) { raw(&v, sizeof(v)); }
    void vec3(const glm::vec3& v) { raw(&v, sizeof(v)); }
    void vec4(const glm::vec4& v) { raw(&v, sizeof(v)); }
    void quat(const glm::quat& q) { raw(&q, sizeof(q)); }
    void mat4(const glm::mat4& m) { raw(&m, sizeof(m)); }
    /// A count, then the elements as they are in memory (trivially copyable types).
    template <typename T>
    void array(const std::vector<T>& v) {
        static_assert(std::is_trivially_copyable_v<T>);
        u32(static_cast<std::uint32_t>(v.size()));
        raw(v.data(), v.size() * sizeof(T));
    }
    void blob(const std::vector<std::uint8_t>& v) { array(v); }

private:
    std::vector<std::uint8_t> bytes_;
};

class BinaryReader {
public:
    BinaryReader(const std::uint8_t* data, std::size_t size, std::string name) : data_(data), size_(size), name_(std::move(name)) {}

    std::size_t left() const { return size_ - at_; }
    void need(std::size_t bytes) const {
        if (bytes > left()) {
            fail("truncated");
        }
    }
    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error(name_ + ": " + what + " (at byte " + std::to_string(at_) + ")");
    }
    void raw(void* out, std::size_t size) {
        need(size);
        std::memcpy(out, data_ + at_, size);
        at_ += size;
    }
    template <typename T>
        requires std::is_arithmetic_v<T>
    T value() {
        T v;
        raw(&v, sizeof(T));
        return v;
    }
    std::uint32_t u32() { return value<std::uint32_t>(); }
    float f32() { return value<float>(); }
    /// A count no larger than `limit` (and than what the bytes left could hold, `element` bytes each).
    std::uint32_t count(std::size_t element, std::uint32_t limit = 1u << 28) {
        const std::uint32_t n = u32();
        if (n > limit || static_cast<std::uint64_t>(n) * element > left()) {
            fail("count " + std::to_string(n) + " beyond the file");
        }
        return n;
    }
    std::string text() {
        const std::uint32_t n = count(1, 1u << 20);
        std::string s(reinterpret_cast<const char*>(data_ + at_), n);
        at_ += n;
        return s;
    }
    glm::vec2 vec2() { glm::vec2 v; raw(&v, sizeof(v)); return v; }
    glm::vec3 vec3() { glm::vec3 v; raw(&v, sizeof(v)); return v; }
    glm::vec4 vec4() { glm::vec4 v; raw(&v, sizeof(v)); return v; }
    glm::quat quat() { glm::quat q; raw(&q, sizeof(q)); return q; }
    glm::mat4 mat4() { glm::mat4 m; raw(&m, sizeof(m)); return m; }
    template <typename T>
    std::vector<T> array() {
        static_assert(std::is_trivially_copyable_v<T>);
        const std::uint32_t n = count(sizeof(T));
        std::vector<T> v(n);
        raw(v.data(), static_cast<std::size_t>(n) * sizeof(T));
        return v;
    }
    std::vector<std::uint8_t> blob() { return array<std::uint8_t>(); }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t at_ = 0;
    std::string name_;
};

}  // namespace moteur
