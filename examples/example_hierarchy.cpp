#include <iostream>
#include <string>

#include <entt/entity/registry.hpp>
#include <enttx/hierarchy.hpp>

using namespace enttx;

struct name_tag {
  std::string value;
};

void print_tree(entt::registry &reg, entt::entity e, int depth = 0) {
  std::cout << std::string(depth * 2, ' ') << reg.get<name_tag>(e).value
            << '\n';

  for (entt::entity child : hierarchy::children(reg, e)) {
    print_tree(reg, child, depth + 1);
  }
}

int main() {
  entt::registry reg;

  auto make = [&](std::string_view name) {
    auto e = reg.create();
    reg.emplace<name_tag>(e, std::string{name});
    return e;
  };

  // Build a simple scene hierarchy
  auto scene = make("Scene");

  auto player = make("Player");
  auto camera = make("Camera");
  auto weapon = make("Weapon");
  auto muzzle = make("Muzzle");
  auto light = make("Flashlight");

  auto enemy = make("Enemy");
  auto sword = make("Sword");

  hierarchy::push_back(reg, scene, player);
  hierarchy::push_back(reg, scene, enemy);

  hierarchy::push_back(reg, player, camera);
  hierarchy::push_back(reg, player, weapon);

  hierarchy::push_back(reg, weapon, muzzle);
  hierarchy::push_back(reg, weapon, light);

  hierarchy::push_back(reg, enemy, sword);

  std::cout << "=== Scene Hierarchy ===\n";
  print_tree(reg, scene);

  std::cout << "\nPlayer descendants:\n";
  hierarchy::for_each_descendant(reg, player, [&](entt::entity e) {
    std::cout << " - " << reg.get<name_tag>(e).value << '\n';
  });

  // Move the flashlight from the weapon to the camera.
  std::cout << "\nReparenting Flashlight -> Camera\n";
  hierarchy::push_back(reg, camera, light);

  std::cout << "\n=== Updated Hierarchy ===\n";
  print_tree(reg, scene);

  auto root = hierarchy::find_root(reg, light);
  std::cout << "\nRoot of Flashlight: " << reg.get<name_tag>(root).value
            << '\n';

  std::cout << "\nDeleting Player and its descendants...\n";
  reg.destroy(player);

  std::cout << "\n=== Updated Hierarchy ===\n";
  print_tree(reg, scene);

  // Wait for user input before exiting.
  std::cout << "\nPress Enter to exit...";
  std::cin.get();
}