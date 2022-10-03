#ifndef ZPP_PROTO_H
#define ZPP_PROTO_H

namespace zpp
{
namespace proto
{
using bits::failure;

template <std::size_t... I>
constexpr auto create_field_number_array(std::index_sequence<I...>)
{
    return std::array<int32_t, std::index_sequence<I...>::size()>{{I + 1 ...}};
}

template <typename Type>
requires std::is_class_v<Type>
constexpr auto field_numbers(Type *)
{
    return create_field_number_array(
        std::make_index_sequence<bits::number_of_members<Type>()>());
}

template <typename Type>
constexpr auto unique_field_numbers()
{
    auto field_nums = field_numbers(static_cast<Type *>(nullptr));
    std::sort(field_nums.begin(), field_nums.end());
    return std::adjacent_find(std::find_if_not(field_nums.begin(),
                                               field_nums.end(),
                                               [](auto e) { return e == 0; }),
                              field_nums.end()) == field_nums.end();
}

namespace concepts
{
template <typename T>
concept is_length_delimited = !std::is_fundamental_v<T> &&
    !std::same_as<std::byte, T> &&
    !bits::concepts::varint<T>;
} // namespace concepts

template <typename Type>
constexpr auto check_type()
{
    using type = std::remove_cvref_t<Type>;
    if constexpr (!std::is_class_v<type> || bits::concepts::varint<type> ||
                  bits::concepts::empty<type>) {
        return true;
    } else if constexpr (bits::concepts::associative_container<type> &&
                         requires { typename type::mapped_type; }) {
        static_assert(
            requires { type{}.push_back(typename type::value_type{}); } ||
            requires { type{}.insert(typename type::value_type{}); });
        static_assert(check_type<typename type::key_type>());
        static_assert(check_type<typename type::mapped_type>());
        return true;
    } else if constexpr (bits::concepts::container<type>) {
        static_assert(
            requires { type{}.push_back(typename type::value_type{}); } ||
            requires { type{}.insert(typename type::value_type{}); });
        static_assert(check_type<typename type::value_type>());
        return true;
    } else if constexpr (bits::concepts::optional<type>) {
        static_assert(check_type<typename type::value_type>());
        return true;
    } else if constexpr (bits::concepts::owning_pointer<type>) {
        static_assert(check_type<typename type::element_type>());
        return true;
    } else if constexpr (requires {
                             field_numbers(static_cast<type *>(nullptr));
                         }) {
        static_assert(unique_field_numbers<type>());
        return true;
    } else {
        static_assert(!sizeof(Type));
        return true;
    }
}

enum class wire_type : unsigned int
{
    varint = 0,
    fixed_64 = 1,
    length_delimited = 2,
    fixed_32 = 5,
};

template <typename Type>
constexpr auto tag_type()
{
    using type = std::remove_cvref_t<Type>;
    if constexpr (bits::concepts::varint<type> ||
                  ( std::is_enum_v<type> && !std::same_as<type, std::byte> ) ||
                  std::same_as<type, bool>) {
        return wire_type::varint;
    } else if constexpr (std::is_integral_v<type> ||
                         std::is_floating_point_v<type>) {
        if constexpr (sizeof(type) == 4) {
            return wire_type::fixed_32;
        } else if constexpr (sizeof(type) == 8) {
            return wire_type::fixed_64;
        } else {
            static_assert(!sizeof(type));
        }
    } else {
        return wire_type::length_delimited;
    }
}

constexpr auto make_tag_explicit(wire_type type, auto field_number)
{
    return bits::varint{(field_number << 3) |
                        std::underlying_type_t<wire_type>(type)};
}

template <typename Type>
constexpr auto make_tag(auto field_number)
{
    if constexpr (!bits::concepts::empty<decltype(field_number)>)
        return make_tag_explicit(tag_type<Type>(), field_number);
    else
        return std::monostate{};
}

constexpr auto tag_type(auto tag)
{
    return wire_type(tag.value & 0x7);
}

constexpr auto tag_number(auto tag)
{
    return ( unsigned int ) (tag >> 3);
}

template <typename Type, std::size_t Index>
constexpr auto field_num()
{
    return field_numbers(( Type * ) nullptr)[Index];
}

namespace traits
{

constexpr auto get_default_size_type()
{
    return std::monostate{};
}

constexpr auto get_default_size_type(auto option, auto... options)
{
    if constexpr (requires { typename decltype(option)::default_size_type; }) {
        if constexpr (std::is_void_v<
                          typename decltype(option)::default_size_type>) {
            return std::monostate{};
        } else {
            return typename decltype(option)::default_size_type{};
        }
    } else {
        return get_default_size_type(options...);
    }
}

template <typename... Options>
using default_size_type_t = std::conditional_t<
    std::same_as<std::monostate,
                 decltype(get_default_size_type(std::declval<Options>()...))>,
    void, decltype(get_default_size_type(std::declval<Options>()...))>;
} // namespace traits

template <bits::concepts::byte_view ByteView, typename... Options>
constexpr auto make_out_archive(ByteView &&view, Options &&...options)
{
    constexpr auto enlarger = bits::traits::enlarger<Options...>();
    constexpr auto no_enlarge_overflow =
        (... || std::same_as<std::remove_cvref_t<Options>, bits::options::no_enlarge_overflow>);

    return bits::out{
        std::forward<ByteView>(view),
        bits::size_varint{},
        bits::no_fit_size{},
        bits::endian::little{},
        bits::enlarger<std::get<0>(enlarger), std::get<1>(enlarger)>{},
        std::conditional_t<no_enlarge_overflow, bits::no_enlarge_overflow,
                           bits::enlarge_overflow>{},
        bits::alloc_limit<bits::traits::alloc_limit<Options...>()>{}};
}

template <bits::concepts::byte_view ByteView = std::vector<std::byte>,
          typename... Options>
struct out
{
    using archive_type = decltype(make_out_archive(
        std::declval<ByteView &>(), std::declval<Options &&>()...));

    archive_type m_archive;

    using default_size_type = traits::default_size_type_t<Options...>;

    constexpr explicit out(ByteView &&view, Options &&...options) :
        m_archive(make_out_archive(std::move(view),
                                   std::forward<Options>(options)...))
    {}

    constexpr explicit out(ByteView &view, Options &&...options) :
        m_archive(make_out_archive(view, std::forward<Options>(options)...))
    {}

    constexpr static auto kind()
    {
        return bits::kind::out;
    }
    constexpr static bool resizable = archive_type::resizable;

    constexpr std::size_t
    position() const
    {
        return m_archive.position();
    }

    constexpr std::size_t &position()
    {
        return m_archive.position();
    }

    constexpr auto remaining_data()
    {
        return m_archive.remaining_data();
    }

    constexpr static auto no_fit_size =
        (... ||
         std::same_as<std::remove_cvref_t<Options>, bits::options::no_fit_size>);

    constexpr auto ZPP_BITS_INLINE operator()(auto &&item)
    {
        if constexpr (archive_type::resizable && !no_fit_size &&
                      archive_type::enlarger != std::tuple{1, 1}) {
            auto end = m_archive.data().size();
            auto result =
                serialize_one<default_size_type>(std::forward<decltype(item)>(item));
            if (m_archive.position() >= end) {
                m_archive.data().resize(m_archive.position());
            }
            return result;
        } else {
            return serialize_one<default_size_type>(
                std::forward<decltype(item)>(item));
        }
    }

    constexpr auto ZPP_BITS_INLINE serialize_unsized(auto &&item)
    {
        using type = std::remove_cvref_t<decltype(item)>;
        static_assert(check_type<type>());

        if constexpr (bits::concepts::self_referencing<type>) {
            return bits::visit_members(
                std::forward<decltype(item)>(item), [&](auto &&...items) constexpr {
                    static_assert((... && check_type<decltype(items)>()));
                    return serialize_many<type>(
                        std::make_index_sequence<sizeof...(items)>{},
                        std::forward<decltype(items)>(items)...);
                });
        } else {
            return bits::visit_members(
                item, [&](auto &&...items) ZPP_BITS_CONSTEXPR_INLINE_LAMBDA {
                    static_assert((... && check_type<decltype(items)>()));
                    return serialize_many<type>(
                        std::make_index_sequence<sizeof...(items)>{},
                        std::forward<decltype(items)>(items)...);
                });
        }
    }

    template <typename TagType, std::size_t FirstIndex, std::size_t... Indices>
    constexpr auto ZPP_BITS_INLINE
    serialize_many(std::index_sequence<FirstIndex, Indices...>, auto &&first_item,
                   auto &&...items)
    {
        if (auto result = serialize_field<field_num<TagType, FirstIndex>()>(
                std::forward<decltype(first_item)>(first_item));
            failure(result)) [[unlikely]] {
            return result;
        }

        return serialize_many<TagType>(std::index_sequence<Indices...>{},
                                       std::forward<decltype(items)>(items)...);
    }

    template <typename TagType>
    constexpr bits::errc ZPP_BITS_INLINE serialize_many(std::index_sequence<>)
    {
        return {};
    }

    template <auto FieldNum, typename TagType = void>
    constexpr bits::errc ZPP_BITS_INLINE serialize_field(auto &&item)
    {
        using type = std::remove_cvref_t<decltype(item)>;
        using tag_type = std::conditional_t<std::is_void_v<TagType>, type, TagType>;

        if constexpr (bits::concepts::empty<type>) {
            return {};
        } else if constexpr (std::is_enum_v<type> &&
                             !std::same_as<type, std::byte>) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);
            if (auto result =
                    m_archive(tag, bits::varint{std::underlying_type_t<type>(std::forward<decltype(item)>(item))});
                failure(result)) [[unlikely]] {
                return result;
            }
            return {};
        } else if constexpr (!concepts::is_length_delimited<type>) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);
            if (item != 0)
                return m_archive(tag, std::forward<decltype(item)>(item));
            return {};
        } else if constexpr (requires { type::serialize(*this, item); }) {
            return type::serialize(*this, std::forward<decltype(item)>(item));
        } else if constexpr (requires { serialize(*this, item); }) {
            return serialize(*this, std::forward<decltype(item)>(item));
        } else if constexpr (bits::concepts::optional<type>) {
            if (item.has_value()) {
                return serialize_field<FieldNum, TagType>(*item);
            }
            return {};
        } else if constexpr (!bits::concepts::container<type>) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);
            if (auto result = m_archive(tag); failure(result)) [[unlikely]] {
                return result;
            }
            return serialize_sized(std::forward<decltype(item)>(item));
        } else if constexpr (bits::concepts::associative_container<type> &&
                             requires { typename type::mapped_type; }) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);

            using key_type = std::conditional_t<
                std::is_enum_v<typename type::key_type> &&
                    !std::same_as<typename type::key_type, std::byte>,
                bits::varint<typename type::key_type>, typename type::key_type>;

            using mapped_type = std::conditional_t<
                std::is_enum_v<typename type::mapped_type> &&
                    !std::same_as<typename type::mapped_type, std::byte>,
                bits::varint<typename type::mapped_type>, typename type::mapped_type>;

            struct value_type
            {
                const key_type &key;
                const mapped_type &value;
            };

            for (auto &[key, value] : item) {
                if (auto result = m_archive(tag); failure(result)) [[unlikely]] {
                    return serialize_sized(value_type{.key = key, .value = value});
                }
            }

            return {};
        } else if constexpr (requires {
                                 requires std::is_fundamental_v<
                                     typename type::value_type> ||
                                     std::same_as<typename type::value_type,
                                                  std::byte>;
                             }) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);
            auto size = item.size();
            if (!size) [[unlikely]] {
                return {};
            }
            if (auto result = m_archive(
                    tag, bits::varint{size * sizeof(typename type::value_type)},
                    bits::unsized(std::forward<decltype(item)>(item)));
                failure(result)) [[unlikely]] {
                return result;
            }
            return {};
        } else if constexpr (requires {
                                 requires bits::concepts::varint<
                                     typename type::value_type>;
                             }) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);

            std::size_t size = {};
            for (auto &element : item) {
                size += bits::varint_size<type::value_type::encoding>(element.value);
            }
            if (!size) [[unlikely]] {
                return {};
            }
            if (auto result =
                    m_archive(tag, bits::varint{size},
                              bits::unsized(std::forward<decltype(item)>(item)));
                failure(result)) [[unlikely]] {
                return result;
            }
            return {};
        } else if constexpr (requires {
                                 requires std::is_enum_v<typename type::value_type>;
                             }) {
            constexpr auto tag = make_tag<tag_type>(FieldNum);

            using type = typename type::value_type;
            std::size_t size = {};
            for (auto &element : item) {
                size += bits::varint_size(std::underlying_type_t<type>(element));
            }
            if (!size) [[unlikely]] {
                return {};
            }
            if (auto result = m_archive(tag, bits::varint{size}); failure(result))
                [[unlikely]] {
                return result;
            }
            for (auto &element : item) {
                if (auto result =
                        m_archive(bits::varint{std::underlying_type_t<type>(element)});
                    failure(result)) [[unlikely]] {
                    return result;
                }
            }
            return {};
        } else {
            constexpr auto tag = make_tag<typename type::value_type>(FieldNum);
            for (auto &element : item) {
                if (auto result = m_archive(tag); failure(result)) [[unlikely]] {
                    return result;
                }
                if (auto result = serialize_sized(element); failure(result)) [[unlikely]] {
                    return result;
                }
            }
            return {};
        }
    }

    template <typename SizeType = bits::vsize_t>
    constexpr bits::errc ZPP_BITS_INLINE serialize_sized(auto &&item)
    {
        using type = std::remove_cvref_t<decltype(item)>;

        auto size_position = m_archive.position();
        if (auto result = m_archive(SizeType{}); failure(result)) [[unlikely]] {
            return result;
        }

        if (auto result = serialize_unsized(std::forward<decltype(item)>(item));
            failure(result)) [[unlikely]] {
            return result;
        }

        auto current_position = m_archive.position();
        std::size_t message_size =
            current_position - size_position - sizeof(SizeType);

        if constexpr (bits::concepts::varint<SizeType>) {
            constexpr auto preserialized_varint_size = 1;
            message_size =
                current_position - size_position - preserialized_varint_size;
            auto move_ahead_count =
                bits::varint_size(message_size) - preserialized_varint_size;
            if (move_ahead_count) {
                if constexpr (archive_type::resizable) {
                    if (auto result = m_archive.enlarge_for(move_ahead_count);
                        failure(result)) [[unlikely]] {
                        return result;
                    }
                } else if (move_ahead_count >
                           m_archive.data().size() - current_position) [[unlikely]] {
                    return std::errc::result_out_of_range;
                }
                auto data = m_archive.data().data();
                auto message_start = data + size_position + preserialized_varint_size;
                auto message_end = data + current_position;
                if (std::is_constant_evaluated()) {
                    for (auto p = message_end - 1; p >= message_start; --p) {
                        *(p + move_ahead_count) = *p;
                    }
                } else {
                    std::memmove(message_start + move_ahead_count, message_start,
                                 message_size);
                }
                m_archive.position() += move_ahead_count;
            }
        }

        auto message_length_span =
            std::span<typename archive_type::byte_type, sizeof(SizeType)>{
                m_archive.data().data() + size_position, sizeof(SizeType)};
        auto message_length_out = bits::out<
            std::span<typename archive_type::byte_type, sizeof(SizeType)>>{
            message_length_span};
        return message_length_out(SizeType(message_size));
    }

    template <typename SizeType = default_size_type>
    constexpr bits::errc ZPP_BITS_INLINE serialize_one(auto &&item)
    {
        if constexpr (!std::is_void_v<SizeType>) {
            return serialize_sized<SizeType>(std::forward<decltype(item)>(item));
        } else {
            return serialize_unsized(std::forward<decltype(item)>(item));
        }
    }
};

template <bits::concepts::byte_view ByteView, typename... Options>
constexpr auto make_in_archive(ByteView &&view, Options &&...options)
{
    return bits::in{std::forward<ByteView>(view), bits::size_varint{},
                    bits::endian::little{},
                    bits::alloc_limit<bits::traits::alloc_limit<Options...>()>{}};
}

template <bits::concepts::byte_view ByteView = std::vector<std::byte>,
          typename... Options>
class in {
    using archive_type = decltype(make_in_archive(std::declval<ByteView &>(),
                                                  std::declval<Options &&>()...));

    archive_type m_archive;

public:
    // using byte_type = std::add_const_t<typename ByteView::value_type>;

    using default_size_type = traits::default_size_type_t<Options...>;

    constexpr explicit in(ByteView &&view, Options &&...options) :
        m_archive(make_in_archive(std::move(view),
                                  std::forward<Options>(options)...))
    {}

    constexpr explicit in(ByteView &view, Options &&...options) :
        m_archive(make_in_archive(view, std::forward<Options>(options)...))
    {}

    ZPP_BITS_INLINE constexpr bits::errc
    operator()(auto &item)
    {
        return serialize_one(item);
    }

    constexpr std::size_t position() const
    {
        return m_archive.position();
    }


    constexpr std::size_t &position()
    {
        return m_archive.position();
    }

    constexpr auto remaining_data()
    {
        return m_archive.remaining_data();
    }

    constexpr static auto kind()
    {
        return bits::kind::in;
    }

    ZPP_BITS_INLINE constexpr bits::errc deserialize_fields(auto &item, std::size_t end_position)
    {
        using type = std::remove_cvref_t<decltype(item)>;
        static_assert(check_type<type>());

        bits::visit_members(item, [](auto &...members) ZPP_BITS_CONSTEXPR_INLINE_LAMBDA {
            (
                [](auto &member) ZPP_BITS_CONSTEXPR_INLINE_LAMBDA {
                    using type = std::remove_cvref_t<decltype(member)>;
                    if constexpr (bits::concepts::container<type> &&
                                  !std::is_fundamental_v<type> &&
                                  !std::same_as<type, std::byte> &&
                                  requires { member.clear(); }) {
                        member.clear();
                    } else if constexpr (bits::concepts::optional<type> || bits::concepts::owning_pointer<type>) {
                        member.reset();
                    } else if constexpr (std::is_fundamental_v<type>) {
                        member = 0;
                    } else if constexpr (bits::concepts::varint<type>) {
                        member = static_cast<typename type::value_type>(0);
                    }
                }(members),
                ...);
        });

        while (m_archive.position() < end_position) {
            bits::vuint32_t tag;
            if (auto result = m_archive(tag); failure(result)) [[unlikely]] {
                return result;
            }

            if (auto result =
                    deserialize_field(item, tag_number(tag), proto::tag_type(tag));
                failure(result)) [[unlikely]] {
                return result;
            }
        }

        return {};
    }

    template <std::size_t Index = 0>
    ZPP_BITS_INLINE constexpr auto
    deserialize_field(auto &&item, auto field_num,
                      wire_type field_type)
    {
        using type = std::remove_reference_t<decltype(item)>;
        if constexpr (Index >= bits::number_of_members<type>()) {
            // unknown field, we should skip it
            return bits::errc{};
        } else if (proto::field_num<type, Index>() != field_num) {
            return deserialize_field<Index + 1>(item, field_num, field_type);
        } else if constexpr (bits::concepts::self_referencing<type>) {
            return bits::visit_members(
                item, [&](auto &&...items) constexpr {
                    std::tuple<decltype(items) &...> refs = {items...};
                    auto &item = std::get<Index>(refs);
                    using type = std::remove_reference_t<decltype(item)>;
                    static_assert(check_type<type>());
                    return deserialize_field(field_type, item);
                });
        } else {
            return bits::visit_members(
                item, [&](auto &&...items) ZPP_BITS_CONSTEXPR_INLINE_LAMBDA {
                    std::tuple<decltype(items) &...> refs = {items...};
                    auto &item = std::get<Index>(refs);
                    using type = std::remove_reference_t<decltype(item)>;
                    static_assert(check_type<type>());

                    return deserialize_field(field_type, item);
                });
        }
    }

    ZPP_BITS_INLINE constexpr auto
    deserialize_field(wire_type field_type, auto &item)
    {
        using type = std::remove_reference_t<decltype(item)>;
        static_assert(check_type<type>());

        if constexpr (std::is_enum_v<type>) {
            bits::varint<type> value;
            if (auto result = m_archive(value); failure(result)) [[unlikely]] {
                return result;
            }
            item = value;
            return bits::errc{};
        } else if constexpr (!concepts::is_length_delimited<type>) {
            return m_archive(item);
        } else if constexpr (requires { type::serialize(*this, item); }) {
            return type::serialize(*this, item);
        } else if constexpr (requires { serialize(*this, item); }) {
            return serialize(*this, item);
        } else if constexpr (bits::concepts::optional<type>) {
            return deserialize_field(field_type, item.emplace());
        } else if constexpr (!bits::concepts::container<type>) {
            return serialize_one<bits::varint<uint32_t>>(item);
        } else if constexpr (bits::concepts::associative_container<type> &&
                             requires { typename type::mapped_type; }) {
            using key_type = std::conditional_t<
                std::is_enum_v<typename type::key_type> &&
                    !std::same_as<typename type::key_type, std::byte>,
                bits::varint<typename type::key_type>, typename type::key_type>;

            using mapped_type = std::conditional_t<
                std::is_enum_v<typename type::mapped_type> &&
                    !std::same_as<typename type::mapped_type, std::byte>,
                bits::varint<typename type::mapped_type>, typename type::mapped_type>;

            struct value_type
            {
                key_type key;
                mapped_type value;
            };

            std::aligned_storage_t<sizeof(value_type), alignof(value_type)> storage;

            auto object = bits::access::placement_new<value_type>(std::addressof(storage));
            bits::destructor_guard guard{*object};
            if (auto result = serialize_one<bits::varint<uint32_t>>(*object);
                failure(result)) [[unlikely]] {
                return result;
            }

            item.emplace(std::move(object->key), std::move(object->value));
            return bits::errc{};
        } else {
            using orig_value_type = typename type::value_type;
            using value_type =
                std::conditional_t<std::is_enum_v<orig_value_type> &&
                                       !std::same_as<orig_value_type, std::byte>,
                                   bits::varint<orig_value_type>, orig_value_type>;

            if constexpr (!concepts::is_length_delimited<value_type>) {
                auto fetch = [&]() ZPP_BITS_CONSTEXPR_INLINE_LAMBDA {
                    value_type value;
                    if (auto result = m_archive(value); failure(result)) [[unlikely]] {
                        return result;
                    }

                    if constexpr (requires { item.push_back(orig_value_type(value)); }) {
                        item.push_back(orig_value_type(value));
                    } else {
                        item.insert(orig_value_type(value));
                    }

                    return bits::errc{};
                };
                if (field_type != wire_type::length_delimited) [[unlikely]] {
                    return fetch();
                }
                bits::vsize_t length;
                if (auto result = m_archive(length); failure(result)) [[unlikely]] {
                    return result;
                }

                if constexpr (requires { item.resize(1); } &&
                              ( std::is_fundamental_v<value_type> ||
                                std::same_as<value_type, std::byte> ) ) {
                    if constexpr (archive_type::allocation_limit !=
                                  std::numeric_limits<std::size_t>::max()) {
                        if (length > archive_type::allocation_limit) [[unlikely]] {
                            return bits::errc{std::errc::message_size};
                        }
                    }
                    item.resize(length / sizeof(value_type));
                    return m_archive(bits::unsized(item));
                } else {
                    if constexpr (requires { item.reserve(1); }) {
                        item.reserve(length);
                    }

                    auto end_position = length + m_archive.position();
                    while (m_archive.position() < end_position) {
                        if (auto result = fetch(); failure(result)) [[unlikely]] {
                            return result;
                        }
                    }

                    return bits::errc{};
                }
            } else {
                std::aligned_storage_t<sizeof(value_type), alignof(value_type)> storage;

                auto object =
                    bits::access::placement_new<value_type>(std::addressof(storage));
                bits::destructor_guard guard{*object};
                if (auto result = serialize_one< bits::varint<uint32_t>>(*object);
                    failure(result)) [[unlikely]] {
                    return result;
                }

                if constexpr (requires { item.push_back(std::move(*object)); }) {
                    item.push_back(std::move(*object));
                } else {
                    item.insert(std::move(*object));
                }

                return bits::errc{};
            }
        }
    }

    template <typename SizeType = default_size_type>
    ZPP_BITS_INLINE constexpr bits::errc serialize_one(auto &item)
    {
        // using type = std::remove_cvref_t<decltype(item)>;
        if constexpr (!std::is_void_v<SizeType>) {
            SizeType size{};
            if (auto result = m_archive(size); failure(result)) [[unlikely]] {
                return result;
            }
            if (size > m_archive.remaining_data().size()) [[unlikely]]
                return bits::errc{std::errc::message_size};

            return deserialize_fields(item, m_archive.position() + size);
        } else
            return deserialize_fields(item, m_archive.data().size());
    }
};
template <typename Type, std::size_t Size, typename... Options>
in(Type (&)[Size], Options &&...) -> in<std::span<Type, Size>, Options...>;

template <typename Type, typename SizeType, typename... Options>
in(bits::sized_item<Type, SizeType> &, Options &&...)
    -> in<Type, Options...>;

template <typename Type, typename SizeType, typename... Options>
in(const bits::sized_item<Type, SizeType> &, Options &&...)
    -> in<const Type, Options...>;

template <typename Type, typename SizeType, typename... Options>
in(bits::sized_item<Type, SizeType> &&, Options &&...)
    -> in<Type, Options...>;

constexpr auto input(auto &&view, auto &&...option)
{
    return in(std::forward<decltype(view)>(view),
              std::forward<decltype(option)>(option)...);
}

constexpr auto output(auto &&view, auto &&...option)
{
    return out(std::forward<decltype(view)>(view),
               std::forward<decltype(option)>(option)...);
}

constexpr auto in_out(auto &&view, auto &&...option)
{
    return std::tuple{
        in<std::remove_reference_t<typename decltype(in{view})::view_type>,
           decltype(option) &...>(view, option...),
        out(std::forward<decltype(view)>(view),
            std::forward<decltype(option)>(option)...)};
}

template <typename ByteType = std::byte>
constexpr auto data_in_out(auto &&...option)
{
    struct data_in_out
    {
        data_in_out(decltype(option) &&...option) :
            input(data, option...),
            output(data,
                   std::forward<decltype(option)>(option)...)
        {
        }

        std::vector<ByteType> data;
        in<decltype(data), decltype(option) &...> input;
        out<decltype(data), decltype(option)...> output;
    };
    return data_in_out{std::forward<decltype(option)>(option)...};
}

template <typename ByteType = std::byte>
constexpr auto data_in(auto &&...option)
{
    struct data_in
    {
        data_in(decltype(option) &&...option) :
            input(data,
                  std::forward<decltype(option)>(option)...)
        {
        }

        std::vector<ByteType> data;
        in<decltype(data), decltype(option)...> input;
    };
    return data_in{std::forward<decltype(option)>(option)...};
}

template <typename ByteType = std::byte>
constexpr auto data_out(auto &&...option)
{
    struct data_out
    {
        data_out(decltype(option) &&...option) :
            output(data,
                   std::forward<decltype(option)>(option)...)
        {
        }

        std::vector<ByteType> data;
        out<decltype(data), decltype(option)...> output;
    };
    return data_out{std::forward<decltype(option)>(option)...};
}

template <auto Object, std::size_t MaxSize = 0x1000>
constexpr auto to_bytes()
{
    constexpr auto size = [] {
        std::array<std::byte, MaxSize> data;
        out out{data};
        out(Object).or_throw();
        return out.position();
    }();

    if constexpr (!size) {
        return bits::string_literal<std::byte, 0>{};
    } else {
        std::array<std::byte, size> data;
        out{data}(Object).or_throw();
        return data;
    }
}

template <auto Data, typename Type>
constexpr auto from_bytes()
{
    Type object;
    in{Data}(object).or_throw();
    return object;
}


} // namespace proto
} // namespace zpp

#endif
