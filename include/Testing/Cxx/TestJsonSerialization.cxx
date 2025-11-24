#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

#include "common/pointer.h"
#include "common/serialization_macros.h"
#include "serialization.h"
#include "serialization_impl.h"


namespace serialization
{
	class test_serialization
	{
	public:
		explicit test_serialization(double d) : d_(d) {}

		const auto d() const { return d_; }

		virtual ~test_serialization() = default;

	protected:
		void initialize() {};
		test_serialization() = default;
		SERIALIZATION_SERIALIZATION_EXPORT(SERIALIZATION_API, test_serialization, d_);

	protected:
		double d_{ 0 };
	};

	class test_derived_serialization final :public test_serialization
	{
	public:
		test_derived_serialization(double d, std::string n) : test_serialization(d), n_(std::move(n)) {}

		const auto& n() const { return n_; }

	private:
		void initialize() {};
		test_derived_serialization() = default;
		SERIALIZATION_SERIALIZATION_EXPORT(SERIALIZATION_API, test_derived_serialization, d_, n_);

		std::string n_;
	};

	SERIALIZATION_REGISTER_DERIVED_SERIALIZATION(test_derived_serialization);
}  // namespace serialization

#define SERIALIZATION_LOG_INFO(...)

int main()
{
	SERIALIZATION_LOG_INFO("==== JsonSerialization: string")
	{
		serialization::json buffer;
		std::string  a_in = "name";
		std::string  a_out;
		serialization::serialization_save(buffer, a_in);
		serialization::serialization_load(buffer, a_out);
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: pair")
	{
		serialization::json            buffer;
		std::pair<float, float> a_in = std::make_pair(1., 1.);
		std::pair<float, float> a_out;
		serialization::serialization_save(buffer, a_in);
		serialization::serialization_load(buffer, a_out);
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: vector")
	{
		serialization::json        buffer;
		std::vector<double> rhs = { 1, 2, 4, 6, 8 };
		std::vector<double> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: set")
	{
		serialization::json  buffer;
		std::set<int> rhs{ 1, 2, 3, 4, 5 };
		std::set<int> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		//EXPECT_TRUE(lhs.size() == rhs.size() && equal(lhs.begin(), lhs.end(), rhs.begin()));
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: array")
	{
		serialization::json                buffer;
		std::array<unsigned int, 5> rhs{ 1, 2, 3, 4, 5 };
		std::array<unsigned int, 5> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		//EXPECT_TRUE(lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin()));
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: map")
	{
		serialization::json           buffer;
		std::map<int64_t, int> rhs{ {1, 1}, {2, 2} };
		std::map<int64_t, int> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		//EXPECT_TRUE(lhs.size() == rhs.size() && equal(lhs.begin(), lhs.end(), rhs.begin()));
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: unordered_map")
	{
		serialization::json                        buffer;
		std::unordered_map<uint64_t, float> rhs{ {1, 1.}, {2, 2.}, {3, 1.3}, {4, 2.3} };
		std::unordered_map<uint64_t, float> lhs;

		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		//EXPECT_TRUE(lhs == rhs);
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: unique_ptr")
	{
		serialization::json buffer;
		auto         rhs = serialization::util::make_ptr_unique_mutable<serialization::test_serialization>(5.6);
		std::unique_ptr<serialization::test_serialization> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		///EXPECT_EQ(rhs->d(), lhs->d());
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: ptr_mutable")
	{
		serialization::json                                    buffer;
		serialization::ptr_mutable<serialization::test_serialization> rhs =
			std::make_shared<serialization::test_serialization>(6.7);
		serialization::ptr_const<serialization::test_serialization> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		///EXPECT_EQ(rhs->d(), lhs->d());
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: ptr_mutable")
	{
		serialization::json                                    buffer;
		const auto& rhs =
			std::make_shared<serialization::test_derived_serialization>(6.7, "me");

		serialization::ptr_const<serialization::test_serialization> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		auto lhs_derived = std::dynamic_pointer_cast<const serialization::test_derived_serialization>(lhs);
		if (rhs->d() != lhs->d())
			throw;
	}

	SERIALIZATION_LOG_INFO("==== JsonSerialization: variant")
	{
		serialization::json buffer;

		std::variant<int, float, std::string> rhs = 6.5F;

		std::variant<int, float, std::string> lhs;
		serialization::serialization_save(buffer, rhs);
		serialization::serialization_load(buffer, lhs);

		//EXPECT_EQ(rhs, lhs);
	}
	return 0;
}
