#include <enttx/change_mixin.hpp>
#include <enttx/observer.hpp>
#include <entt/entity/registry.hpp>

#include <deque>
#include <iostream>
#include <string>
#include <sstream>
#include <format>

struct transform {
    float x{};
    float y{};
};

std::ostream &operator<<(std::ostream &os, const transform &t) {
    return os << '(' << t.x << ", " << t.y << ')';
}

std::istream &operator>>(std::istream &is, transform &t) {
    char open{};
    char comma{};
    char close{};

    if (is >> open >> t.x >> comma >> t.y >> close;
        open == '(' && comma == ',' && close == ')') {
        return is;
    }

    is.setstate(std::ios::failbit);
    return is;
}

template<>
struct entt::storage_type<transform> {
    using type = enttx::change_mixin<entt::basic_storage<transform>>;
};

struct empty {
};

struct output_text_archive {
	std::ostringstream oss;
	template<typename T>
	void operator()(const T& value) {
		oss << value << std::endl;
	}

	void operator()(const std::uint8_t value) {
		oss << (std::uint32_t)value << std::endl;
	}
	void operator()(const entt::entity value) {
		oss << entt::to_integral(value) << std::endl;
	}

	template<typename T, typename... Rest>
	void operator()(const T& value, const Rest&... rest) {
		operator()(value);
		operator()(rest...);
	}
};

struct input_text_archive {
	std::istringstream iss;
	template<typename T>
	void operator()(T& value) {
		iss >> value;
	}

	void operator()(std::uint8_t& value) {
		std::uint32_t temp;
		iss >> temp;
		value = static_cast<std::uint8_t>(temp);
	}

	void operator()(entt::entity& value) {
		std::uint32_t temp;
		iss >> temp;
		value = entt::entity{ temp };
	}

	template<typename T, typename... Rest>
	void operator()(T& value, Rest&... rest) {
		operator()(value);
		operator()(rest...);
	}
};

int main()
{
	entt::registry reg;
	const auto entt = reg.create();

	enttx::observers observers;
	observers.emplace_back( enttx::observe<transform>( reg ) );
	observers.emplace_back( enttx::observe<empty>( reg ) );

	enttx::commit commit_a{}; 
	{
		reg.emplace<empty>( entt );
		reg.emplace<transform>( entt, 0.f, 0.f );
		reg.patch<transform>( entt, []( transform& t ) { t.x = 10.f; } );
		reg.patch<transform>( entt, []( transform& t ) { t.y = 5.f; } );

		for (auto& observer : observers) {
			observer->collect( commit_a ); 
		}
	}

	// Print the current state of the registry (changes made by `commit_a`)
	std::cout << "=== Commit A ===" << std::endl;
	{
		std::cout << std::format( "transform: x={}, y={}\n", reg.get<transform>( entt ).x, reg.get<transform>( entt ).y );
		std::cout << std::format( "has 'empty': {}\n", reg.all_of<empty>( entt ) ? "true" : "false" );
	}
	std::cout << std::endl;

	// Serialize `commit_a` to a string
	std::string serialized_commit_a; 
	{
		output_text_archive ar{};

		enttx::commit_snapshot ser{ commit_a };
		ser.get<transform>( ar );
		ser.get<empty>( ar );

		serialized_commit_a = ar.oss.str();
	}

	// Deserialize the serialized `commit_a` into `commit_b`
	enttx::commit commit_b{}; 
	{
		input_text_archive ar{ .iss{ serialized_commit_a } };

		// We must `get<>` in the same order as we serialized, otherwise the deserialization will fail.
		enttx::commit_loader des{ commit_b };
		des.get<transform>( ar );
		des.get<empty>( ar );
	}

	// invert `commit_b` and apply it to the registry, which should undo the changes made by `commit_a`
	std::cout << "=== After applying inverted commit_b ===" << std::endl;
	{
		auto inv_commit_b = commit_b.invert();
		inv_commit_b.apply( reg );

		std::cout << std::format( "has 'transform': {}\n", reg.all_of<transform>( entt ) ? "true" : "false" );
		std::cout << std::format( "has 'empty': {}\n", reg.all_of<empty>( entt ) ? "true" : "false" );
	}
	std::cout << std::endl;

	// Apply `commit_b` to the registry, which should redo the changes made by `commit_a`
	std::cout << "=== After applying commit_b ===" << std::endl;
	{
		commit_b.apply( reg );
		std::cout << std::format( "transform: x={}, y={}\n", reg.get<transform>( entt ).x, reg.get<transform>( entt ).y );
		std::cout << std::format( "has 'empty': {}\n", reg.all_of<empty>( entt ) ? "true" : "false" );
	}
	std::cout << std::endl;
}