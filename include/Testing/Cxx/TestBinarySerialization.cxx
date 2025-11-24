#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/pointer.h"
#include "common/serialization_macros.h"
#include "serialization_impl.h"
#include "util/multi_process_stream.h"

namespace serialization
{
class test_serialization
{
public:
    explicit test_serialization(double d) : d_(d) {}

    double d() const { return d_; }

private:
    void initialize() {};
    test_serialization() = default;
    SERIALIZATION_SERIALIZATION_EXPORT(SERIALIZATION_API, test_serialization, d_);

    double d_{0};
};
}  // namespace serialization

TEST(Serialization, BinarySerialization)
{
    {
        serialization::multi_process_stream buffer;
        std::string                  a_in = "name";
        std::string                  a_out;
        serialization::serialization_save(buffer, a_in);
        serialization::serialization_load(buffer, a_out);

        EXPECT_EQ(a_in, a_out);
    }
    {
        serialization::multi_process_stream buffer;
        std::pair<float, float>      a_in = std::make_pair(1., 1.);
        std::pair<float, float>      a_out;
        serialization::serialization_save(buffer, a_in);
        serialization::serialization_load(buffer, a_out);

        EXPECT_EQ(a_in, a_out);
    }
    {
        serialization::multi_process_stream buffer;
        std::vector<double>          rhs = {1, 2, 4, 6, 8};
        std::vector<double>          lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_TRUE(lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin()));
    }
    {
        serialization::multi_process_stream buffer;
        std::set<int>                rhs{1, 2, 3, 4, 5};
        std::set<int>                lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_TRUE(lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin()));
    }
    {
        serialization::multi_process_stream buffer;
        std::array<short, 5>         rhs{1, 2, 3, 4, 5};
        std::array<short, 5>         lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_TRUE(lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin()));
    }
    {
        serialization::multi_process_stream buffer;
        std::map<int64_t, int>       rhs{{1, 1}, {2, 2}};
        std::map<int64_t, int>       lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_TRUE(lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin()));
    }
    {
        serialization::multi_process_stream      buffer;
        std::unordered_map<size_t, float> rhs{{1, 1.}, {2, 2.}};
        std::unordered_map<size_t, float> lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_TRUE(lhs == rhs);
    }

    {
        serialization::multi_process_stream buffer;

        auto rhs = std::make_unique<serialization::test_serialization>(5.6);

        std::unique_ptr<serialization::test_serialization> lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_EQ(rhs->d(), lhs->d());
    }
    {
        serialization::multi_process_stream                    buffer;
        serialization::ptr_mutable<serialization::test_serialization> rhs =
            std::make_shared<serialization::test_serialization>(6.7);

        serialization::ptr_const<serialization::test_serialization> lhs;
        serialization::serialization_save(buffer, rhs);
        serialization::serialization_load(buffer, lhs);

        EXPECT_EQ(rhs->d(), lhs->d());
    }

    END_TEST();
}
