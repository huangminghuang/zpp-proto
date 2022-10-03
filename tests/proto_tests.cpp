#include <boost/ut.hpp>
#include <zpp_bits.h>
#include <zpp_proto.h>
#include <map>

namespace ut = boost::ut;
using namespace zpp::bits::literals;

template <typename T>
std::string to_hex(const std::vector<T> &data)
{
    static const char qmap[] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string result;
    result.resize(data.size() * 2);
    int index = 0;
    for (auto b : data) {
        unsigned char c = static_cast<unsigned char>(b);
        result[index++] = qmap[c >> 4];
        result[index++] = qmap[c & '\x0F'];
    }
    return result;
}

struct example
{
    using serialize = zpp::bits::protocol<zpp::bits::pb{}>;
    zpp::bits::vint32_t i; // field number == 1

    constexpr auto operator<=>(const example &) const = default;
};

static_assert(
    zpp::proto::to_bytes<example{{150}}>() ==
    "089601"_decode_hex);

static_assert(zpp::proto::from_bytes<"089601"_decode_hex,
                                     example>()
                  .i == 150);

struct nested_example
{
    example nested; // field number == 1
};


static_assert(zpp::proto::to_bytes<nested_example{
                  .nested = example{150}}>() == "0a03089601"_decode_hex);

static_assert(zpp::proto::from_bytes<"0a03089601"_decode_hex,
                                     nested_example>()
                  .nested.i == 150);

struct nested_reserved_example
{
    [[no_unique_address]] zpp::bits::pb_reserved _1{}; // field number == 1
    [[no_unique_address]] zpp::bits::pb_reserved _2{}; // field number == 2
    example nested{};                                  // field number == 3
};


static_assert(sizeof(nested_reserved_example) == sizeof(example));

static_assert(
    zpp::proto::to_bytes<zpp::bits::unsized_t<nested_reserved_example>{
        {.nested = example{150}}}>() == "1a03089601"_decode_hex);

static_assert(
    zpp::proto::from_bytes<"1a03089601"_decode_hex,
                           zpp::bits::unsized_t<nested_reserved_example>>()
        .nested.i == 150);

struct nested_explicit_id_example
{
    example nested{}; // field number == 3
    friend constexpr auto field_numbers(nested_explicit_id_example *)
    {
        return std::array<std::uint32_t, 1>{3U};
    }
};

//// doesn't work with zpp::bits::unsized_t
static_assert(
    zpp::proto::to_bytes<nested_explicit_id_example{
        .nested = example{150}}>() ==
    "1a03089601"_decode_hex);
static_assert(
    zpp::proto::from_bytes<"1a03089601"_decode_hex,
                           nested_explicit_id_example>()
        .nested.i == 150);


struct repeated_integers
{
    std::vector<zpp::bits::vsint32_t> integers;
};

ut::suite test_repeated_integers = [] {
    auto [data, in, out] = zpp::proto::data_in_out();
    out(repeated_integers{.integers = {1, 2, 3, 4, -1, -2, -3, -4}})
        .or_throw();

    repeated_integers r;
    in(r).or_throw();

    ut::expect(
        r.integers == std::vector<zpp::bits::vsint32_t>{1, 2, 3, 4, -1, -2, -3, -4});
};

struct repeated_examples
{
    using serialize = zpp::bits::protocol<zpp::bits::pb{}>;
    std::vector<example> examples;
};

ut::suite test_repeated_example = [] {
    auto [data, in, out] = zpp::proto::data_in_out();
    out(repeated_examples{.examples = {{1}, {2}, {3}, {4}, {-1}, {-2}, {-3}, {-4}}})
        .or_throw();

    repeated_examples r;
    in(r).or_throw();

    ut::expect(r.examples ==
               (std::vector<example>{
                   {1}, {2}, {3}, {4}, {-1}, {-2}, {-3}, {-4}}));
};

struct monster
{
    enum color
    {
        red,
        blue,
        green
    };

    struct vec3
    {
        using serialize = zpp::bits::protocol<zpp::bits::pb{}>;
        float x;
        float y;
        float z;

        bool operator==(const vec3 &) const = default;
    };

    struct weapon
    {
        using serialize = zpp::bits::protocol<zpp::bits::pb{}>;
        std::string name;
        int damage;

        bool operator==(const weapon &) const = default;
    };

    using serialize = zpp::bits::protocol<zpp::bits::pb{}>;

    vec3 pos;
    zpp::bits::vint32_t mana;
    int hp;
    std::string name;
    std::vector<std::uint8_t> inventory;
    color color;
    std::vector<weapon> weapons;
    weapon equipped;
    std::vector<vec3> path;
    bool boss;

    bool operator==(const monster &) const = default;
};

ut::suite test_monster = [] {
    auto [data, in, out] = zpp::proto::data_in_out(zpp::bits::size4b{});
    monster m = {.pos = {1.0, 2.0, 3.0},
                 .mana = 200,
                 .hp = 1000,
                 .name = "mushroom",
                 .inventory = {1, 2, 3},
                 .color = monster::color::blue,
                 .weapons =
                     {
                         monster::weapon{.name = "sword", .damage = 55},
                         monster::weapon{.name = "spear", .damage = 150},
                     },
                 .equipped =
                     {
                         monster::weapon{.name = "none", .damage = 15},
                     },
                 .path = {monster::vec3{2.0, 3.0, 4.0},
                          monster::vec3{5.0, 6.0, 7.0}},
                 .boss = true};
    out(m).or_throw();

    monster m2;
    in(m2).or_throw();

    ut::expect(m.pos == m2.pos);
    ut::expect(m.mana == m2.mana);
    ut::expect(m.hp == m2.hp);
    ut::expect(m.name == m2.name);
    ut::expect(m.inventory == m2.inventory);
    ut::expect(m.color == m2.color);
    ut::expect(m.weapons == m2.weapons);
    ut::expect(m.equipped == m2.equipped);
    ut::expect(m.path == m2.path);
    ut::expect(m.boss == m2.boss);
    ut::expect(m == m2);
};

ut::suite test_monster_unsized = [] {
    auto [data, in, out] = zpp::proto::data_in_out(zpp::bits::no_size{});
    monster m = {.pos = {1.0, 2.0, 3.0},
                 .mana = 200,
                 .hp = 1000,
                 .name = "mushroom",
                 .inventory = {1, 2, 3},
                 .color = monster::color::blue,
                 .weapons =
                     {
                         monster::weapon{.name = "sword", .damage = 55},
                         monster::weapon{.name = "spear", .damage = 150},
                     },
                 .equipped =
                     {
                         monster::weapon{.name = "none", .damage = 15},
                     },
                 .path = {monster::vec3{2.0, 3.0, 4.0},
                          monster::vec3{5.0, 6.0, 7.0}},
                 .boss = true};

    ut::expect(success(out(m)));
    monster m2;
    ut::expect(success(in(m2)));

    ut::expect(m.pos == m2.pos);
    ut::expect(m.mana == m2.mana);
    ut::expect(m.hp == m2.hp);
    ut::expect(m.name == m2.name);
    ut::expect(m.inventory == m2.inventory);
    ut::expect(m.color == m2.color);
    ut::expect(m.weapons == m2.weapons);
    ut::expect(m.equipped == m2.equipped);
    ut::expect(m.path == m2.path);
    ut::expect(m.boss == m2.boss);
    ut::expect(m == m2);
};

struct person
{
    std::string name;       // = 1
    zpp::bits::vint32_t id; // = 2
    std::string email;      // = 3

    enum phone_type
    {
        mobile = 0,
        home = 1,
        work = 2,
    };

    struct phone_number
    {
        std::string number; // = 1
        phone_type type;    // = 2
    };

    std::vector<phone_number> phones; // = 4
};

struct address_book
{
    std::vector<person> people; // = 1
};


ut::suite test_person = [] {
    constexpr auto data =
        "\n\x08John Doe\x10\xd2\t\x1a\x10jdoe@example.com\"\x0c\n\x08"
        "555-4321\x10\x01"_b;
    static_assert(data.size() == 45);

    person p;
    ut::expect(success(zpp::proto::in{data}(p)));

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    ut::expect(p.name == "John Doe"sv);
    ut::expect(that % p.id == 1234);
    ut::expect(p.email == "jdoe@example.com"sv);
    ut::expect((p.phones.size() == 1_u) >> fatal);
    ut::expect(p.phones[0].number == "555-4321"sv);
    ut::expect(that % p.phones[0].type == person::home);

    std::array<std::byte, data.size()> new_data;
    ut::expect(success(zpp::proto::out{new_data}(p)));

    ut::expect(data == new_data);
};

ut::suite test_address_book = [] {
    constexpr auto data =
        "\n-\n\x08John Doe\x10\xd2\t\x1a\x10jdoe@example.com\"\x0c\n\x08"
        "555-4321\x10\x01\n>\n\nJohn Doe "
        "2\x10\xd3\t\x1a\x11jdoe2@example.com\"\x0c\n\x08"
        "555-4322\x10\x01\"\x0c\n\x08"
        "555-4323\x10\x02"_b;

    static_assert(data.size() == 111);

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    address_book b;
    expect(success(zpp::proto::in{data}(b)));

    expect(b.people.size() == 2_u);
    expect(b.people[0].name == "John Doe"sv);
    expect(that% b.people[0].id == 1234);
    expect(b.people[0].email == "jdoe@example.com"sv);
    expect((b.people[0].phones.size() == 1u) >> fatal);
    expect(b.people[0].phones[0].number == "555-4321"sv);
    expect(b.people[0].phones[0].type == person::home);
    expect(b.people[1].name == "John Doe 2"sv);
    expect(that % b.people[1].id == 1235);
    expect(b.people[1].email == "jdoe2@example.com"sv);
    expect((b.people[1].phones.size() == 2_u) >> fatal);
    expect(b.people[1].phones[0].number == "555-4322"sv);
    expect(b.people[1].phones[0].type == person::home);
    expect(b.people[1].phones[1].number == "555-4323"sv);
    expect(b.people[1].phones[1].type == person::work);

    std::array<std::byte, data.size()> new_data;
    zpp::proto::out out{new_data};
    expect(success(out(b)));
    expect(out.position() == data.size());
    expect(data == new_data);
};

struct person_explicit
{
    std::string extra;
    std::string name;
    zpp::bits::vint32_t id;
    std::string email;

    enum phone_type
    {
        mobile = 0,
        home = 1,
        work = 2,
    };

    struct phone_number
    {
        std::string number;
        phone_type type;

        friend constexpr auto field_numbers(phone_number *)
        {
            return std::array<std::uint32_t, 2>{1U, 2U};
        }
    };

    std::vector<phone_number> phones;

    friend constexpr auto field_numbers(person_explicit *)
    {
        return std::array<std::uint32_t, 5>{10U, 1U, 2U, 3U, 4U};
    }
};


ut::suite test_person_explicit = [] {
    constexpr auto data =
        "\n\x08John Doe\x10\xd2\t\x1a\x10jdoe@example.com\"\x0c\n\x08"
        "555-4321\x10\x01"_b;
    static_assert(data.size() == 45);

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    person_explicit p;
    expect(success(zpp::proto::in{data}(p)));

    expect(p.name == "John Doe"sv);
    expect(that% p.id == 1234);
    expect(p.email == "jdoe@example.com"sv);
    expect((p.phones.size() == 1_u)>> fatal);
    expect(p.phones[0].number == "555-4321"sv);
    expect(that% p.phones[0].type == person_explicit::home);

    person p1;
    p1.name = p.name;
    p1.id = p.id;
    p1.email = p.email;
    p1.phones.push_back({p.phones[0].number,
                         person::phone_type(p.phones[0].type)});

    std::array<std::byte, data.size()> new_data;
    expect(success(zpp::proto::out{new_data, zpp::bits::no_size{}}(p1)));

    expect(data == new_data);
};

struct person_map
{
    std::string name;       // = 1
    zpp::bits::vint32_t id; // = 2
    std::string email;      // = 3

    enum phone_type
    {
        mobile = 0,
        home = 1,
        work = 2,
    };

    std::map<std::string, phone_type> phones; // = 4
};

ut::suite test_person_map = [] {
    constexpr auto data =
        "\n\x08John Doe\x10\xd2\t\x1a\x10jdoe@example.com\"\x0c\n\x08"
        "555-4321\x10\x01"_b;
    static_assert(data.size() == 45);

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    person_map p;
    expect(success(zpp::proto::in{data}(p)));

    expect(p.name == "John Doe"sv);
    expect(that% p.id == 1234);
    expect(p.email == "jdoe@example.com"sv);
    expect((p.phones.size() == 1_u) >> fatal);
    expect((p.phones.contains("555-4321")) >> fatal);
    expect(that% p.phones["555-4321"] == person_map::home);

    std::array<std::byte, data.size()> new_data;
    expect(success(zpp::proto::out{new_data}(p)));

    expect(data == new_data);
};


ut::suite test_default_person_in_address_book = [] {
    constexpr auto data = "\n\x00"_b;

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    address_book b;
    expect(success(zpp::proto::in{data}(b)));

    expect(b.people.size() == 1_u);
    expect(b.people[0].name == ""sv);
    expect(that% b.people[0].id == 0);
    expect(b.people[0].email == ""sv);
    expect(b.people[0].phones.size() == 0_u);

    std::array<std::byte, "0a00"_decode_hex.size()> new_data;
    expect(success(zpp::proto::out{new_data}(b)));

    expect(new_data == "0a00"_decode_hex);
};

ut::suite test_empty_address_book = [] {
    constexpr auto data = ""_b;

    using namespace boost::ut;


    address_book b;
    expect(success(zpp::proto::in{data}(b)));

    expect(b.people.size() == 0_u);

    std::array<std::byte, 1> new_data;
    zpp::proto::out out{new_data};
    expect(success(out(b)));

    expect(out.position() == 0_u);
};

ut::suite test_empty_person = [] {
    constexpr auto data = ""_b;
    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    person p;
    expect(success(zpp::proto::in{data}(p)));


    expect(p.name.size() == 0_u);
    expect(p.name == ""sv);
    expect(that% p.id == 0);
    expect(p.email == ""sv);
    expect(p.phones.size() == 0_u);

    std::array<std::byte, 2> new_data;
    zpp::proto::out out{new_data};
    expect(success(out(p)));
    expect(out.position() == 0_u);
};

ut::suite test_decode_unknown_field = [] {
    person_explicit p1
    {
        .extra = "extra",
        .name = "John Doe",
        .id = 1234,
        .email = "jdoe@example.com",
        .phones = {{.number = "555-4321", .type = person_explicit::home}}
    };

    using namespace std::literals::string_view_literals;
    using namespace boost::ut;

    std::vector<char> data;
    expect(success(zpp::proto::out{data}(p1)));

    person p;
    expect(success(zpp::proto::in{data}(p)));

    expect(p.name == "John Doe"sv);
    expect(that % p.id == 1234);
    expect(p.email == "jdoe@example.com"sv);
    expect((p.phones.size() == 1_u) >> fatal);
    expect(p.phones[0].number == "555-4321"sv);
    expect(that % p.phones[0].type == person::home);
};

int
main()
{
    const auto result = ut::cfg<>.run(
        {.report_errors =
             true}); // explicitly run registered test suites and report errors
    return result;
}