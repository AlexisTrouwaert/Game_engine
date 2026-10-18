#pragma once

/// @file
/// Reflection of the engine's and the game's types (milestone 9, part 3; decision 7.1 of the review:
/// home-made, a `constexpr` description next to the type).
///
/// The fields of a struct are declared **once**, next to it, and everything that used to be written
/// by hand for each type follows from that description: reading the data tables (with the same
/// messages as DataReader), saving and loading components, the inspector of the debug tools, the
/// prefabs, and a JSON schema for the tools.
///
/// @code{.cpp}
/// struct Health {
///     int value = 100;
///     int max = 100;
///     float regeneration = 0.0f;
///     DamageKind weakness = DamageKind::None;
/// };
/// MOTEUR_REFLECT_ENUM(DamageKind, "none", "fire", "cold");
/// MOTEUR_REFLECT(Health,
///     MOTEUR_FIELD(value, {.min = 0, .doc = "points of life left"}),
///     MOTEUR_FIELD(max, {.min = 1, .max = 1000000}),
///     MOTEUR_FIELD(regeneration, {.min = 0, .unit = "points/s", .aliases = {"regen"}}),
///     MOTEUR_FIELD(weakness));
/// @endcode
///
/// The macros go at namespace scope, in the type's namespace (they define a function found by
/// argument-dependent lookup), after the type. `MOTEUR_FIELD_AS(member, "key", ...)` gives the field
/// another key than the member's name (the data of a game is in the game's language). An old key is
/// still read through `aliases` (a renamed field keeps reading the old files and saves).
///
/// A described type is turned once into a TypeInfo (type_info<T>()), a description the engine reads
/// at run time without templates; the algorithms (reflection_io.hpp, the inspector, the prefabs) work
/// on it. Supported field types: `bool`, integers (8 to 64 bits), `float`, `double`, `std::string`,
/// `glm::vec2/3/4`, `glm::ivec2/3`, `glm::quat`, described enumerations (saved **by name**), described
/// structs (nested), `std::vector` and `std::optional` of any of them, DataRef (a row of a data table, by identifier),
/// `entt::entity` (a reference to another entity: by persistent identifier in saves, by index in a
/// prefab) and Asset (a loaded asset, by its path). A member of another type is simply not
/// described (a cache, a pose): it is neither read, saved nor edited.
///
/// Determinism: reading gives exactly the values a hand-written reader gives (floats go through the
/// same double-to-float conversion), and writing is exact (a float read back is the same bit for
/// bit). Main thread for the registry by name; type_info<T>() itself is thread-safe.

#include <entt/entity/entity.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

#include "moteur/asset_cache.hpp"
#include "moteur/data_table.hpp"

namespace moteur {

/// What a field is for (FieldOptions::flags, combined with |).
enum FieldFlags : std::uint32_t {
    kFieldEdit = 1u << 0,          ///< shown and editable in the inspector and the editor
    kFieldSave = 1u << 1,          ///< written in saves (ComponentSerializers::add_reflected())
    kFieldData = 1u << 2,          ///< read from data (tables, prefabs, levels)
    kFieldLogic = 1u << 3,         ///< part of the simulation: changing it by hand breaks determinism
    kFieldPresentation = 1u << 4,  ///< only for what is seen or heard (an editor may change it freely)
    kFieldColor = 1u << 5,         ///< a vec3 / vec4 that is a colour: a colour picker in the inspector
    kFieldReadOnly = 1u << 6,      ///< shown but not editable (a result the systems write)
    kFieldDefault = kFieldEdit | kFieldSave | kFieldData,  ///< what a field is when nothing is said
};

/// The options of one field, written with designated initializers in MOTEUR_FIELD():
/// `{.min = 0, .max = 30, .unit = "m/s", .doc = "...", .flags = kFieldDefault | kFieldLogic}`.
struct FieldOptions {
    double min = -std::numeric_limits<double>::infinity();  ///< numbers (and each component of vectors): the lowest value read or edited
    double max = std::numeric_limits<double>::infinity();   ///< the highest
    const char* unit = "";  ///< for the inspector and the schema ("m", "m/s", "s")
    const char* doc = "";   ///< one sentence, for the inspector's tooltip and the schema
    std::uint32_t flags = kFieldDefault;  ///< FieldFlags
    bool required = false;  ///< data: a missing field is an error (otherwise it keeps the struct's default)
    std::array<const char*, 4> aliases{};  ///< older keys still read (migrations); null entries end the list
};

/// What a TypeInfo describes.
enum class TypeKind : std::uint8_t {
    Bool,       ///< bool
    Int,        ///< an integer of TypeInfo::bytes bytes, TypeInfo::is_signed
    Float,      ///< float (bytes 4) or double (bytes 8)
    String,     ///< std::string (UTF-8)
    FloatVector,  ///< glm::vec2/3/4: TypeInfo::components floats
    IntVector,  ///< glm::ivec2/3: TypeInfo::components ints
    Quat,       ///< glm::quat, written w, x, y, z
    Enum,       ///< a described enumeration: TypeInfo::enum_names, values 0..n-1
    Struct,     ///< a described struct: TypeInfo::fields
    Array,      ///< std::vector of TypeInfo::element
    Optional,   ///< std::optional of TypeInfo::element: `null` when empty
    DataRef,    ///< DataRef<T>: a row of the data table of T
    Entity,     ///< entt::entity: another entity
    Asset,      ///< Asset<T>: a loaded asset, of the type TypeInfo::asset_type
};

struct TypeInfo;

/// One field of a described struct, at run time.
struct FieldInfo {
    std::string name;                  ///< the key in data and saves
    std::vector<std::string> aliases;  ///< older keys still read
    std::string unit;                  ///< see FieldOptions::unit
    std::string doc;                   ///< see FieldOptions::doc
    double min = -std::numeric_limits<double>::infinity();  ///< see FieldOptions::min
    double max = std::numeric_limits<double>::infinity();   ///< see FieldOptions::max
    std::uint32_t flags = kFieldDefault;  ///< FieldFlags
    bool required = false;             ///< see FieldOptions::required
    const TypeInfo* type = nullptr;    ///< the field's type
    void* (*access)(void* owner) = nullptr;  ///< the field inside an object of the owner's type

    /// The field of `owner` (an object of the struct that has this field).
    void* in(void* owner) const { return access(owner); }
    /// The field of `owner`, read-only.
    const void* in(const void* owner) const { return access(const_cast<void*>(owner)); }
    /// True when the key `key` is this field's (its name or one of its aliases).
    bool answers_to(std::string_view key) const;
};

/// A type as the reflection sees it at run time: what it is, how to make, copy and compare one, and
/// for a struct its fields. Made once per C++ type by type_info<T>(), never copied (compare their
/// addresses).
struct TypeInfo {
    std::string name;  ///< the struct's or enumeration's name, as given to the macro; else a C++-like name ("float", "std::vector<Mover>")
    TypeKind kind = TypeKind::Struct;  ///< what it is
    std::size_t size = 0;   ///< sizeof
    std::size_t align = 0;  ///< alignof
    std::uint8_t bytes = 0;       ///< Int, Float: the size of the value; Enum: of its underlying integer
    bool is_signed = false;       ///< Int (and Enum's underlying integer)
    std::uint8_t components = 0;  ///< FloatVector, IntVector: 2, 3 or 4
    std::vector<std::string> enum_names;  ///< Enum: the name of each value, by value
    std::vector<FieldInfo> fields;        ///< Struct: the fields, in the order of the description
    const TypeInfo* element = nullptr;    ///< Array: the type of its elements
    std::type_index asset_type = typeid(void);  ///< Asset: the asset's type (Model, Texture...); DataRef: the row's type

    /// Lifetime of an object of this type in raw storage (of `size` bytes aligned on `align`).
    ///@{
    void (*construct)(void* storage) = nullptr;           ///< a default-constructed object
    void (*destroy)(void* object) = nullptr;
    void (*copy)(void* to, const void* from) = nullptr;   ///< copy-assigns `from` to `to`
    ///@}
    /// Array: the number of elements, a new size (new elements default-constructed), an element.
    ///@{
    std::size_t (*array_size)(const void* array) = nullptr;
    void (*array_resize)(void* array, std::size_t size) = nullptr;
    void* (*array_at)(void* array, std::size_t index) = nullptr;
    ///@}
    /// Optional: whether it holds a value, the value (made default if it held none), emptied.
    ///@{
    bool (*optional_has)(const void* optional) = nullptr;
    void* (*optional_emplace)(void* optional) = nullptr;
    void (*optional_reset)(void* optional) = nullptr;
    ///@}
    /// DataRef: the identifier, and the link step (DataLinker::resolve() for the row's type).
    ///@{
    std::string* (*ref_id)(void* ref) = nullptr;
    bool (*ref_resolved)(const void* ref) = nullptr;
    void (*ref_resolve)(void* ref, DataLinker& linker, std::string_view field, bool optional) = nullptr;
    /// Binds the reference to its row in `tables` now (a save read after the tables were loaded);
    /// false if the table or the row is missing.
    bool (*ref_bind)(void* ref, const DataTables& tables) = nullptr;
    ///@}
    /// Asset: whether two handles hold the same asset.
    bool (*same_asset)(const void* a, const void* b) = nullptr;

    /// The field named `key` (or with that alias), or null. Struct only.
    const FieldInfo* field(std::string_view key) const;
    /// True for a struct without fields (a tag such as Hidden).
    bool empty_struct() const { return kind == TypeKind::Struct && fields.empty(); }
};

/// The struct or enumeration named `name` among those whose type_info<T>() was made, or null.
/// Thread-safe.
const TypeInfo* find_type(std::string_view name);

/// An object of a described type in storage of its own, default-constructed, destroyed with it:
/// what a tool edits before giving it to an entity, the defaults a prefab's overrides are compared
/// with. Not copyable.
class ScratchObject {
public:
    /// A default-constructed object of `type`.
    explicit ScratchObject(const TypeInfo& type);
    ~ScratchObject();
    ScratchObject(const ScratchObject&) = delete;
    ScratchObject& operator=(const ScratchObject&) = delete;
    /// The object.
    ///@{
    void* get() { return storage_; }
    const void* get() const { return storage_; }
    ///@}
    /// Its type.
    const TypeInfo& type() const { return *type_; }

private:
    const TypeInfo* type_;
    void* storage_ = nullptr;
};

namespace reflection_detail {

void register_type(const TypeInfo& type);

template <typename Owner, typename Member>
struct FieldDescription {
    const char* name;
    Member Owner::*member;
    FieldOptions options;
};

template <typename T>
struct TypeDescription;

template <typename T, typename... Fields>
struct StructDescription {
    const char* name;
    std::tuple<Fields...> fields;
};

template <typename T, std::size_t N>
struct EnumDescription {
    const char* name;
    std::array<const char*, N> names;
};

template <typename T>
concept Reflected = requires { moteur_reflect(static_cast<const T*>(nullptr)); };

template <typename T>
concept ReflectedEnum = std::is_enum_v<T> && Reflected<T>;

template <typename T>
concept ReflectedStruct = std::is_class_v<T> && Reflected<T>;

template <typename T>
struct IsVector : std::false_type {};
template <typename T, typename A>
struct IsVector<std::vector<T, A>> : std::true_type {
    using Element = T;
};

template <typename T>
struct IsOptional : std::false_type {};
template <typename T>
struct IsOptional<std::optional<T>> : std::true_type {
    using Element = T;
};

template <typename T>
struct IsDataRef : std::false_type {};
template <typename T>
struct IsDataRef<moteur::DataRef<T>> : std::true_type {
    using Row = T;
};

template <typename T>
struct IsAsset : std::false_type {};
template <typename T>
struct IsAsset<entt::resource<T>> : std::true_type {
    using Value = T;
};

template <typename T>
void construct_object(void* storage) {
    ::new (storage) T();
}
template <typename T>
void destroy_object(void* object) {
    static_cast<T*>(object)->~T();
}
template <typename T>
void copy_object(void* to, const void* from) {
    *static_cast<T*>(to) = *static_cast<const T*>(from);
}

template <typename T>
TypeInfo make_type_info();

}  // namespace reflection_detail

/// The run-time description of `T` (see the top of this file): made the first time it is asked for,
/// then the same object for the whole run. `T` must be a supported type (a static_assert says so).
/// Thread-safe.
template <typename T>
const TypeInfo& type_info() {
    static const TypeInfo info = reflection_detail::make_type_info<T>();
    // The registry by name keeps a pointer to the final object.
    static const bool registered = [] {
        if (info.kind == TypeKind::Struct || info.kind == TypeKind::Enum) {
            reflection_detail::register_type(info);
        }
        return true;
    }();
    static_cast<void>(registered);
    return info;
}

/// The name of an enumeration value as its description gives it ("to_point"); empty for a value
/// outside the description.
template <reflection_detail::ReflectedEnum E>
std::string_view enum_name(E value) {
    const TypeInfo& info = type_info<E>();
    const auto index = static_cast<std::size_t>(value);
    return index < info.enum_names.size() ? std::string_view(info.enum_names[index]) : std::string_view();
}

/// The value of an enumeration named `name`, or false.
template <reflection_detail::ReflectedEnum E>
bool enum_from_name(std::string_view name, E& value) {
    const TypeInfo& info = type_info<E>();
    for (std::size_t i = 0; i < info.enum_names.size(); ++i) {
        if (info.enum_names[i] == name) {
            value = static_cast<E>(i);
            return true;
        }
    }
    return false;
}

/// For the macros: a field description (`name` its key, `member` the member pointer).
template <typename Owner, typename Member>
constexpr reflection_detail::FieldDescription<Owner, Member> describe_field(const char* name, Member Owner::*member,
                                                                           FieldOptions options = {}) {
    return {name, member, options};
}

/// For the macros: a struct's description.
template <typename T, typename... Fields>
constexpr reflection_detail::StructDescription<T, Fields...> describe_struct(const char* name, Fields... fields) {
    return {name, std::tuple<Fields...>(fields...)};
}

/// For the macros: an enumeration's description.
template <typename T, typename... Names>
constexpr reflection_detail::EnumDescription<T, sizeof...(Names)> describe_enum(const char* name, Names... names) {
    return {name, {names...}};
}

namespace reflection_detail {

template <typename T>
std::string scalar_name() {
    if constexpr (std::is_same_v<T, bool>) {
        return "bool";
    } else if constexpr (std::is_floating_point_v<T>) {
        return sizeof(T) == 4 ? "float" : "double";
    } else {
        return std::string(std::is_signed_v<T> ? "int" : "uint") + std::to_string(sizeof(T) * 8);
    }
}

template <typename T, std::size_t I>
void* access_field(void* owner) {
    static constexpr auto kDescription = moteur_reflect(static_cast<const T*>(nullptr));
    return &(static_cast<T*>(owner)->*(std::get<I>(kDescription.fields).member));
}

template <typename T, std::size_t I>
FieldInfo make_field() {
    static constexpr auto kDescription = moteur_reflect(static_cast<const T*>(nullptr));
    constexpr auto description = std::get<I>(kDescription.fields);
    using Member = std::remove_cvref_t<decltype(std::declval<T&>().*(description.member))>;
    FieldInfo field;
    field.name = description.name;
    for (const char* alias : description.options.aliases) {
        if (alias != nullptr) {
            field.aliases.emplace_back(alias);
        }
    }
    field.unit = description.options.unit;
    field.doc = description.options.doc;
    field.min = description.options.min;
    field.max = description.options.max;
    field.flags = description.options.flags;
    field.required = description.options.required;
    field.type = &type_info<Member>();
    field.access = &access_field<T, I>;
    return field;
}

template <typename T, std::size_t... I>
std::vector<FieldInfo> make_fields(std::index_sequence<I...>) {
    return {make_field<T, I>()...};
}

template <typename T>
TypeInfo make_type_info() {
    TypeInfo info;
    info.size = sizeof(T);
    info.align = alignof(T);
    info.construct = &construct_object<T>;
    info.destroy = &destroy_object<T>;
    info.copy = &copy_object<T>;
    if constexpr (std::is_same_v<T, bool>) {
        info.kind = TypeKind::Bool;
        info.name = "bool";
    } else if constexpr (std::is_integral_v<T>) {
        info.kind = TypeKind::Int;
        info.bytes = static_cast<std::uint8_t>(sizeof(T));
        info.is_signed = std::is_signed_v<T>;
        info.name = scalar_name<T>();
    } else if constexpr (std::is_floating_point_v<T>) {
        static_assert(sizeof(T) == 4 || sizeof(T) == 8, "float or double");
        info.kind = TypeKind::Float;
        info.bytes = static_cast<std::uint8_t>(sizeof(T));
        info.name = scalar_name<T>();
    } else if constexpr (std::is_same_v<T, std::string>) {
        info.kind = TypeKind::String;
        info.name = "string";
    } else if constexpr (std::is_same_v<T, glm::vec2> || std::is_same_v<T, glm::vec3> || std::is_same_v<T, glm::vec4>) {
        info.kind = TypeKind::FloatVector;
        info.components = static_cast<std::uint8_t>(T::length());
        info.name = "vec" + std::to_string(T::length());
    } else if constexpr (std::is_same_v<T, glm::ivec2> || std::is_same_v<T, glm::ivec3>) {
        info.kind = TypeKind::IntVector;
        info.components = static_cast<std::uint8_t>(T::length());
        info.name = "ivec" + std::to_string(T::length());
    } else if constexpr (std::is_same_v<T, glm::quat>) {
        info.kind = TypeKind::Quat;
        info.name = "quat";
    } else if constexpr (std::is_same_v<T, entt::entity>) {
        info.kind = TypeKind::Entity;
        info.name = "entity";
    } else if constexpr (ReflectedEnum<T>) {
        static constexpr auto kDescription = moteur_reflect(static_cast<const T*>(nullptr));
        info.kind = TypeKind::Enum;
        info.name = kDescription.name;
        info.bytes = static_cast<std::uint8_t>(sizeof(T));
        info.is_signed = std::is_signed_v<std::underlying_type_t<T>>;
        for (const char* name : kDescription.names) {
            info.enum_names.emplace_back(name);
        }
    } else if constexpr (ReflectedStruct<T>) {
        static_assert(std::is_default_constructible_v<T> && std::is_copy_assignable_v<T>,
                      "a described struct must be default-constructible and copyable");
        static constexpr auto kDescription = moteur_reflect(static_cast<const T*>(nullptr));
        info.kind = TypeKind::Struct;
        info.name = kDescription.name;
        info.fields =
            make_fields<T>(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<decltype(kDescription.fields)>>>{});
    } else if constexpr (IsVector<T>::value) {
        using Element = typename IsVector<T>::Element;
        info.kind = TypeKind::Array;
        info.element = &type_info<Element>();
        info.name = "std::vector<" + info.element->name + ">";
        info.array_size = [](const void* array) { return static_cast<const T*>(array)->size(); };
        info.array_resize = [](void* array, std::size_t size) { static_cast<T*>(array)->resize(size); };
        info.array_at = [](void* array, std::size_t index) -> void* { return &(*static_cast<T*>(array))[index]; };
    } else if constexpr (IsOptional<T>::value) {
        using Element = typename IsOptional<T>::Element;
        info.kind = TypeKind::Optional;
        info.element = &type_info<Element>();
        info.name = "std::optional<" + info.element->name + ">";
        info.optional_has = [](const void* optional) { return static_cast<const T*>(optional)->has_value(); };
        info.optional_emplace = [](void* optional) -> void* {
            auto& o = *static_cast<T*>(optional);
            if (!o) {
                o.emplace();
            }
            return &*o;
        };
        info.optional_reset = [](void* optional) { static_cast<T*>(optional)->reset(); };
    } else if constexpr (IsDataRef<T>::value) {
        using Row = typename IsDataRef<T>::Row;
        info.kind = TypeKind::DataRef;
        info.name = "DataRef";
        info.asset_type = typeid(Row);
        info.ref_id = [](void* ref) { return &static_cast<T*>(ref)->id; };
        info.ref_resolved = [](const void* ref) { return static_cast<const T*>(ref)->resolved(); };
        info.ref_resolve = [](void* ref, DataLinker& linker, std::string_view field, bool optional) {
            linker.resolve(*static_cast<T*>(ref), field, optional);
        };
        info.ref_bind = [](void* ref, const DataTables& tables) {
            auto& r = *static_cast<T*>(ref);
            if (!tables.has<Row>()) {
                return false;
            }
            r = tables.get<Row>().ref(r.id);
            return r.resolved();
        };
    } else if constexpr (IsAsset<T>::value) {
        info.kind = TypeKind::Asset;
        info.name = "Asset";
        info.asset_type = typeid(typename IsAsset<T>::Value);
        info.same_asset = [](const void* a, const void* b) {
            return static_cast<const T*>(a)->handle() == static_cast<const T*>(b)->handle();
        };
    } else {
        static_assert(sizeof(T) == 0, "this type cannot be described: see the supported types in reflection.hpp");
    }
    return info;
}

}  // namespace reflection_detail

}  // namespace moteur

/// Describes the struct `Type`: its fields, each by MOTEUR_FIELD() or MOTEUR_FIELD_AS(). At
/// namespace scope, in the type's namespace, after the type (see the top of reflection.hpp).
#define MOTEUR_REFLECT(Type, ...)                                                                \
    [[maybe_unused]] constexpr auto moteur_reflect(const Type*) {                                \
        using MoteurSelf [[maybe_unused]] = Type;                                                             \
        return ::moteur::describe_struct<Type>(#Type __VA_OPT__(, ) __VA_ARGS__);               \
    }

/// Describes the struct `Type` under the name `name` (a string literal): the name prefabs and saves
/// use, when it must differ from the C++ one.
#define MOTEUR_REFLECT_NAMED(Type, name, ...)                                                    \
    [[maybe_unused]] constexpr auto moteur_reflect(const Type*) {                                \
        using MoteurSelf [[maybe_unused]] = Type;                                                             \
        return ::moteur::describe_struct<Type>(name __VA_OPT__(, ) __VA_ARGS__);                 \
    }

/// A field of the struct being described, its key the member's name; the optional second argument
/// is its FieldOptions (`{.min = 0, .doc = "..."}`).
#define MOTEUR_FIELD(member, ...) ::moteur::describe_field(#member, &MoteurSelf::member __VA_OPT__(, ) __VA_ARGS__)

/// A field whose key in data and saves is `key` (a string literal) rather than the member's name.
#define MOTEUR_FIELD_AS(member, key, ...) ::moteur::describe_field(key, &MoteurSelf::member __VA_OPT__(, ) __VA_ARGS__)

/// Describes the enumeration `Type` by the names of its values 0, 1, 2... (string literals, in
/// order): what data and saves write. A value without a name cannot be written.
#define MOTEUR_REFLECT_ENUM(Type, ...)                                                           \
    [[maybe_unused]] constexpr auto moteur_reflect(const Type*) {                                \
        return ::moteur::describe_enum<Type>(#Type, __VA_ARGS__);                                \
    }
