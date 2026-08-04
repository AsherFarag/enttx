#include <iostream>
#include <string>

#include <entt/entity/registry.hpp>
#include <enttx/hierarchy.hpp>

using namespace enttx;

struct name_tag
{
    std::string value;
};

void print_tree( entt::registry& reg, entt::entity e, int depth = 0 )
{
    std::cout << std::string( depth * 2, ' ' )
        << reg.get<name_tag>( e ).value << '\n';

    hierarchy::for_each_child( reg, e, [&]( entt::entity child )
    {
        print_tree( reg, child, depth + 1 );
    } );
}

int main()
{
    entt::registry reg;

    auto make = [&]( std::string_view name )
    {
        auto e = reg.create();
        reg.emplace<name_tag>( e, std::string{ name } );
        return e;
    };

    // Build a simple scene hierarchy
    auto scene = make( "Scene" );

    auto player = make( "Player" );
    auto camera = make( "Camera" );
    auto weapon = make( "Weapon" );
    auto muzzle = make( "Muzzle" );
    auto light  = make( "Flashlight" );

    auto enemy = make( "Enemy" );
    auto sword = make( "Sword" );

    hierarchy::attach_child( reg, scene, player );
    hierarchy::attach_child( reg, scene, enemy );

    hierarchy::attach_child( reg, player, camera );
    hierarchy::attach_child( reg, player, weapon );

    hierarchy::attach_child( reg, weapon, muzzle );
    hierarchy::attach_child( reg, weapon, light );

    hierarchy::attach_child( reg, enemy, sword );

    std::cout << "=== Scene Hierarchy ===\n";
    print_tree( reg, scene );

    std::cout << "\nPlayer descendants:\n";
    hierarchy::for_each_descendant( reg, player, [&]( entt::entity e )
    {
        std::cout << " - " << reg.get<name_tag>( e ).value << '\n';
    } );

    // Move the flashlight from the weapon to the camera.
    std::cout << "\nReparenting Flashlight -> Camera\n";
    hierarchy::attach_child( reg, camera, light );

    std::cout << "\n=== Updated Hierarchy ===\n";
    print_tree( reg, scene );

    auto root = hierarchy::find_root( reg, light );
    std::cout << "\nRoot of Flashlight: "
        << reg.get<name_tag>( root ).value << '\n';

    // Wait for user input before exiting.
    std::cout << "\nPress Enter to exit...";
    std::cin.get();
}